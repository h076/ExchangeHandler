#include <iostream>
#include <deque>
#include <string>
#include <vector>
#include <chrono>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include "session_state_machine.hpp"
#include "message_source_deque.hpp"
#include "../check.hpp"

using state_machine = session_state_machine<message_source_deque>;

// Payload of a popped message, or nullopt if the pop was empty
static std::optional<std::string> payload(std::optional<received_message> m)
{
  if (!m) return std::nullopt;
  return std::move(m->payload);
}

namespace net = boost::asio;
using namespace std::chrono_literals;


// ---------------------------------------------------------------------------
// Mock session: each connect() consumes one scripted outcome. Completions are
// posted, like the real ws_session, so nothing re-enters the state machine
// synchronously from connect().

class mock_session : public ws_session_interface
{
  public:
    enum class outcome { ok, fail, manual };

    explicit mock_session(net::any_io_executor e) : ex(std::move(e)) {}

    void connect() override
    {
      ++connects;
      running = true;

      const outcome o = script.empty() ? outcome::manual : script.front();
      if (!script.empty()) script.pop_front();

      if (o == outcome::ok)
      {
        net::post(ex, [this] { state_handler(connection_status::connected); });
      }
      else if (o == outcome::fail)
      {
        net::post(ex, [this] { running = false; state_handler(connection_status::error); });
      }
    }

    void disconnect() override
    {
      ++disconnects;
      if (!running)
      {
        state_handler(connection_status::disconnected);
        return;
      }
      net::post(ex, [this] { running = false; state_handler(connection_status::disconnected); });
    }

    void send(std::string payload) override { sent.push_back(std::move(payload)); }

    void set_message_handler(std::function<void(std::span<const std::byte>)> h) override { message_handler = std::move(h); }
    void set_state_handler(std::function<void(connection_status)> h) override { state_handler = std::move(h); }

    // Test hooks: simulate the live connection dropping, or a late status
    void drop()                     { running = false; state_handler(connection_status::error); }
    void deliver(const std::string& payload)
    {
      // Like ws_session: the span only lives for the duration of the call
      message_handler(std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(payload.data()), payload.size()));
    }
    void report(connection_status s){ running = false; state_handler(s); }

    std::deque<outcome> script;
    int connects = 0;
    int disconnects = 0;
    bool running = false;
    std::vector<std::string> sent;

  private:
    net::any_io_executor ex;
    std::function<void(std::span<const std::byte>)> message_handler;
    std::function<void(connection_status)> state_handler;
};

// ---------------------------------------------------------------------------

struct fixture
{
  net::io_context ioc;
  std::shared_ptr<mock_session> session;
  std::shared_ptr<state_machine> sm;

  explicit fixture(reconnect_config cfg = fast())
  {
    session = std::make_shared<mock_session>(ioc.get_executor());

    subscription_manager subs([](const channel_id& c) { return "sub:" + c.id; });
    subs.set_channels({channel_id{"A"}, channel_id{"B"}});

    sm = std::make_shared<state_machine>(
        ioc.get_executor(), std::move(subs), reconnect_policy(cfg, 1));
  }

  // Millisecond backoff, no jitter, so tests run quickly and deterministically
  static reconnect_config fast()
  {
    reconnect_config c;
    c.initial_delay = 1ms;
    c.max_delay = 4ms;
    c.jitter = 0.0;
    return c;
  }

  // Run until the queue is idle, bounded so a bug can't hang the test
  void drain() { ioc.restart(); ioc.run_for(2s); }
};

// ---------------------------------------------------------------------------

static void connects_and_subscribes()
{
  fixture f;
  f.session->script = {mock_session::outcome::ok};
  f.sm->run(f.session);
  CHECK(f.sm->state() == session_state::connecting);

  f.drain();
  CHECK(f.sm->state() == session_state::live);
  CHECK(f.session->connects == 1);
  CHECK((f.session->sent == std::vector<std::string>{"sub:A", "sub:B"}));
}

static void failed_attempts_retry_until_connected()
{
  fixture f;
  using o = mock_session::outcome;
  f.session->script = {o::fail, o::fail, o::fail, o::ok};
  f.sm->run(f.session);
  f.drain();

  CHECK(f.sm->state() == session_state::live);
  CHECK(f.session->connects == 4);
  CHECK(f.session->sent.size() == 2);   // subscribed once, on the successful attempt
}

static void gives_up_after_max_attempts()
{
  reconnect_config c = fixture::fast();
  c.max_attempts = 3;
  fixture f(c);
  using o = mock_session::outcome;
  f.session->script = {o::fail, o::fail, o::fail, o::fail, o::ok};
  f.sm->run(f.session);
  f.drain();

  CHECK(f.sm->state() == session_state::failed);
  CHECK(f.session->connects == 4);      // initial attempt + 3 retries
  CHECK(f.session->sent.empty());
}

static void live_drop_reconnects_and_resubscribes()
{
  fixture f;
  using o = mock_session::outcome;
  f.session->script = {o::ok, o::ok};
  f.sm->run(f.session);
  f.drain();

  f.session->drop();
  CHECK(f.sm->state() == session_state::backing_off);
  f.drain();

  CHECK(f.sm->state() == session_state::live);
  CHECK(f.session->connects == 2);
  CHECK((f.session->sent == std::vector<std::string>{"sub:A", "sub:B", "sub:A", "sub:B"}));
}

static void stable_drops_reset_backoff()
{
  // With max_attempts = 1, only a reset lets every drop reconnect
  reconnect_config c = fixture::fast();
  c.max_attempts = 1;
  c.stable_after = 0ms;
  fixture f(c);
  f.session->script.assign(4, mock_session::outcome::ok);
  f.sm->run(f.session);
  f.drain();

  for (int i = 0; i < 3; ++i)
  {
    f.session->drop();
    f.drain();
    CHECK(f.sm->state() == session_state::live);
  }
  CHECK(f.session->connects == 4);
}

static void flapping_drops_exhaust_attempts()
{
  reconnect_config c = fixture::fast();
  c.max_attempts = 1;
  c.stable_after = 1h;
  fixture f(c);
  f.session->script.assign(4, mock_session::outcome::ok);
  f.sm->run(f.session);
  f.drain();

  f.session->drop();                    // uses the one attempt
  f.drain();
  CHECK(f.sm->state() == session_state::live);

  f.session->drop();                    // not stable, no reset: give up
  f.drain();
  CHECK(f.sm->state() == session_state::failed);
  CHECK(f.session->connects == 2);
}

static void stop_while_live()
{
  fixture f;
  f.session->script = {mock_session::outcome::ok};
  f.sm->run(f.session);
  f.drain();

  f.sm->stop();
  CHECK(f.sm->state() == session_state::stopping);
  f.drain();

  CHECK(f.sm->state() == session_state::stopped);
  CHECK(f.session->disconnects == 1);
  CHECK(f.session->connects == 1);
}

static void stop_during_backoff_cancels_retry()
{
  reconnect_config c = fixture::fast();
  c.initial_delay = 1h;                 // 2nd retry would wait an hour
  c.max_delay = 1h;
  fixture f(c);
  using o = mock_session::outcome;
  f.session->script = {o::fail, o::fail, o::ok};
  f.sm->run(f.session);

  f.ioc.run_for(50ms);                  // initial + immediate retry both fail
  CHECK(f.sm->state() == session_state::backing_off);
  CHECK(f.session->connects == 2);

  f.sm->stop();
  CHECK(f.sm->state() == session_state::stopped);

  // Timer cancelled: the queue empties straight away instead of waiting an hour
  const auto t0 = std::chrono::steady_clock::now();
  f.drain();
  CHECK(std::chrono::steady_clock::now() - t0 < 1s);
  CHECK(f.session->connects == 2);
  CHECK(f.session->disconnects == 0);   // nothing in flight to disconnect
}

static void stop_while_connecting()
{
  fixture f;                            // empty script: connect() hangs (manual)
  f.sm->run(f.session);
  f.sm->stop();
  CHECK(f.sm->state() == session_state::stopping);
  f.drain();

  CHECK(f.sm->state() == session_state::stopped);
  CHECK(f.session->connects == 1);
}

static void error_while_stopping_does_not_reconnect()
{
  fixture f;
  f.sm->run(f.session);
  f.sm->stop();
  f.session->report(connection_status::error);   // close failed rather than completed

  CHECK(f.sm->state() == session_state::stopped);
  f.drain();
  CHECK(f.session->connects == 1);
}

static void connected_after_stop_does_not_subscribe()
{
  fixture f;
  f.sm->run(f.session);
  f.sm->stop();
  f.session->report(connection_status::connected);   // handshake finished after stop()

  CHECK(f.sm->state() == session_state::stopping);
  CHECK(f.session->sent.empty());
}

static void received_messages_are_queued_in_order()
{
  fixture f;
  f.session->script = {mock_session::outcome::ok};
  f.sm->run(f.session);
  f.drain();

  CHECK(!f.sm->pop().has_value());      // nothing received yet

  {
    // The payload buffer dies straight after delivery, like Beast's read buffer
    std::string a = "msg-1", b = "msg-2", c = "msg-3";
    f.session->deliver(a);
    f.session->deliver(b);
    f.session->deliver(c);
  }

  CHECK(payload(f.sm->pop()) == std::optional<std::string>("msg-1"));
  CHECK(payload(f.sm->pop()) == std::optional<std::string>("msg-2"));
  CHECK(payload(f.sm->pop()) == std::optional<std::string>("msg-3"));
  CHECK(!f.sm->pop().has_value());
}

static void binary_payload_is_copied_intact()
{
  fixture f;
  f.session->script = {mock_session::outcome::ok};
  f.sm->run(f.session);
  f.drain();

  const std::string bytes("a\0b\xff", 4);   // embedded NUL and high byte
  f.session->deliver(bytes);
  CHECK(payload(f.sm->pop()) == std::optional<std::string>(bytes));
}

static void queued_messages_survive_reconnect()
{
  // Messages pushed before a drop are still there for decode after reconnecting
  fixture f;
  f.session->script = {mock_session::outcome::ok, mock_session::outcome::ok};
  f.sm->run(f.session);
  f.drain();

  f.session->deliver("before");
  f.session->drop();
  f.drain();
  CHECK(f.sm->state() == session_state::live);
  f.session->deliver("after");

  CHECK(payload(f.sm->pop()) == std::optional<std::string>("before"));
  CHECK(payload(f.sm->pop()) == std::optional<std::string>("after"));
}

static void messages_carry_connection_epoch()
{
  // Epoch starts at 1 and increments on every successful connect, but not on
  // failed attempts
  fixture f;
  using o = mock_session::outcome;
  f.session->script = {o::ok, o::fail, o::fail, o::ok};
  f.sm->run(f.session);
  f.drain();

  f.session->deliver("first");
  f.session->drop();                    // -> fail, fail, ok
  f.drain();
  CHECK(f.sm->state() == session_state::live);
  CHECK(f.session->connects == 4);
  f.session->deliver("second");

  auto a = f.sm->pop();
  auto b = f.sm->pop();
  CHECK(a && b);
  if (!a || !b) return;
  CHECK(a->payload == "first");
  CHECK(a->connection_epoch == 1);
  CHECK(b->payload == "second");
  CHECK(b->connection_epoch == 2);
}

static void messages_carry_receive_time()
{
  fixture f;
  f.session->script = {mock_session::outcome::ok};
  f.sm->run(f.session);
  f.drain();

  const auto before = received_message::clock::now();
  f.session->deliver("x");
  const auto after = received_message::clock::now();

  auto m = f.sm->pop();
  CHECK(m.has_value());
  if (!m) return;
  CHECK(m->recv_time >= before);
  CHECK(m->recv_time <= after);
}

static void payload_has_parser_padding()
{
  // Decode (simdjson) needs spare capacity after the payload
  fixture f;
  f.session->script = {mock_session::outcome::ok};
  f.sm->run(f.session);
  f.drain();

  for (const std::string& p : {std::string("x"), std::string(R"({"channel":"heartbeat"})"), std::string(5000, 'a')})
  {
    f.session->deliver(p);
    auto m = f.sm->pop();
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->payload == p);
    CHECK(m->payload.capacity() >= m->payload.size() + received_message::payload_padding);
  }
}

static void usable_as_message_source_interface()
{
  // The decode layer can hold the state machine through the base interface
  fixture f;
  f.session->script = {mock_session::outcome::ok};
  f.sm->run(f.session);
  f.drain();

  f.session->deliver("via-interface");
  message_source_interface& source = *f.sm;
  CHECK(payload(source.pop()) == std::optional<std::string>("via-interface"));
}

int main()
{
  std::cout << "Running session_state_machine tests...\n\n";

  test::run("ConnectsAndSubscribes", connects_and_subscribes);
  test::run("FailedAttemptsRetryUntilConnected", failed_attempts_retry_until_connected);
  test::run("GivesUpAfterMaxAttempts", gives_up_after_max_attempts);
  test::run("LiveDropReconnectsAndResubscribes", live_drop_reconnects_and_resubscribes);
  test::run("StableDropsResetBackoff", stable_drops_reset_backoff);
  test::run("FlappingDropsExhaustAttempts", flapping_drops_exhaust_attempts);
  test::run("StopWhileLive", stop_while_live);
  test::run("StopDuringBackoffCancelsRetry", stop_during_backoff_cancels_retry);
  test::run("StopWhileConnecting", stop_while_connecting);
  test::run("ErrorWhileStoppingDoesNotReconnect", error_while_stopping_does_not_reconnect);
  test::run("ConnectedAfterStopDoesNotSubscribe", connected_after_stop_does_not_subscribe);
  test::run("ReceivedMessagesAreQueuedInOrder", received_messages_are_queued_in_order);
  test::run("BinaryPayloadIsCopiedIntact", binary_payload_is_copied_intact);
  test::run("QueuedMessagesSurviveReconnect", queued_messages_survive_reconnect);
  test::run("MessagesCarryConnectionEpoch", messages_carry_connection_epoch);
  test::run("MessagesCarryReceiveTime", messages_carry_receive_time);
  test::run("UsableAsMessageSourceInterface", usable_as_message_source_interface);
  test::run("PayloadHasParserPadding", payload_has_parser_padding);

  return test::summary();
}
