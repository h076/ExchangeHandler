// Live smoke test for the connection layer against Kraken's public v2 book feed.
//
// Usage: kraken_test [symbol] [seconds]
//   symbol  - Kraken v2 pair, default "BTC/USD"
//   seconds - run time, default 0 (run until Ctrl-C)

#include "ws_session.hpp"
#include "session_state_machine.hpp"

#include <iostream>

int main(int argc, char* argv[])
{
  const std::string symbol = argc > 1 ? argv[1] : "BTC/USD";
  const int seconds = argc > 2 ? std::stoi(argv[2]) : 0;
  const std::string host = "ws.kraken.com";

  net::io_context ioc;

  // Verify the server certificate and host name
  net::ssl::context tls(net::ssl::context::tls_client);
  tls.set_default_verify_paths();
  tls.set_verify_mode(net::ssl::verify_peer);
  tls.set_verify_callback(net::ssl::host_name_verification(host));

  // ws_session's flags and write channel rely on a strand
  auto strand = net::make_strand(ioc);
  auto session = std::make_shared<ws_session>(strand,
      connection_config{host, 443, "/v2", std::move(tls)});

  subscription_manager subs([](const channel_id& c) {
    return R"({"method":"subscribe","params":{"channel":"book","symbol":[")"
           + c.id + R"("],"depth":10}})";
  });
  subs.set_channels({channel_id{symbol}});

  // Count every payload and keep a copy of the latest one for the periodic report.
  // assign() reuses the string's capacity, so this doesn't allocate per message.
  std::size_t msg_count = 0;
  std::size_t byte_count = 0;
  std::string last_msg;
  auto sink = [&](std::span<const std::byte> data) {
    ++msg_count;
    byte_count += data.size();
    last_msg.assign(reinterpret_cast<const char*>(data.data()), data.size());
  };

  auto sm = std::make_shared<session_state_machine>(std::move(subs), sink);

  // Stop on Ctrl-C or after `seconds`. Cancel the other waiter so ioc.run() can return.
  net::signal_set signals(ioc, SIGINT, SIGTERM);
  net::steady_timer timer(ioc);
  net::steady_timer report_timer(ioc);

  auto shutdown = [&] {
    signals.cancel();
    timer.cancel();
    report_timer.cancel();
    net::post(strand, [&] { session->disconnect(); });
  };

  signals.async_wait([&](const boost::system::error_code& ec, int) {
    if (!ec) shutdown();
  });

  if (seconds > 0)
  {
    timer.expires_after(std::chrono::seconds(seconds));
    timer.async_wait([&](const boost::system::error_code& ec) {
      if (!ec) shutdown();
    });
  }

  // Every 5s: running count, messages since last report, and the most recent payload.
  // Single-threaded ioc.run(), so reading the sink's variables here is race-free.
  net::co_spawn(ioc, [&]() -> net::awaitable<void> {
    constexpr auto interval = std::chrono::seconds(1);
    std::size_t prev_count = 0;
    for (int elapsed = 5;; elapsed += 5)
    {
      report_timer.expires_after(interval);
      auto [ec] = co_await report_timer.async_wait(net::as_tuple(net::use_awaitable));
      if (ec)
        co_return;   // cancelled by shutdown()

      std::cout << "[" << elapsed << "s] " << msg_count << " msgs (+"
                << msg_count - prev_count << "), " << byte_count << " bytes\n"
                << "  last: " << last_msg.substr(0, 300)
                << (last_msg.size() > 300 ? "..." : "") << std::endl;
      prev_count = msg_count;
    }
  }, net::detached);   // handles its own errors, ends before ioc.run() returns

  std::cout << "Connecting to wss://" << host << "/v2, book " << symbol
            << (seconds > 0 ? " for " + std::to_string(seconds) + "s" : " (Ctrl-C to stop)")
            << std::endl;

  net::post(strand, [&] { sm->run(session); });
  ioc.run();

  std::cout << "Received " << msg_count << " messages, " << byte_count << " bytes" << std::endl;
  return 0;
}
