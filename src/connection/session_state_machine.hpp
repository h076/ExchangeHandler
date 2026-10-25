#pragma once

#include <cstdint>
#include <memory>
#include <type_traits>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>

#include "ws_session_interface.hpp"
#include "subscription_manager.hpp"
#include "reconnect_policy.hpp"
#include "message_source_interface.hpp"

enum class session_state
{
  idle,          // constructed, run() not called yet
  connecting,    // connect() issued, waiting for connected / error
  live,          // connected and subscribed
  backing_off,   // waiting on the retry timer
  stopping,      // stop() requested, waiting for the session to close
  stopped,       // closed on request
  failed         // reconnect policy gave up
};

const char* to_string(session_state s);

// The state machine *is* the message queue: it inherits from Queue, pushes every
// received payload into itself, and the decode layer calls pop() on it.
// Swapping the queue (e.g. for a lock-free one) is just a different template
// argument: session_state_machine<message_source_deque>.
//
// Queue must derive from message_source_interface and be default constructible.
// Implemented in session_state_machine.cpp, which explicitly instantiates the
// supported queue types: add a line there for each new Queue.
//
// Threading: push() and pop() only touch the queue, so they are as thread safe
// as Queue (the producer is the session's strand, the consumer is the decode
// thread). Everything else must run on the session's strand: the timer, state
// and policy are not thread safe.
template <class Queue>
class session_state_machine : public Queue,
                              public std::enable_shared_from_this<session_state_machine<Queue>>
{
  static_assert(std::is_base_of_v<message_source_interface, Queue>,
                "Queue must derive from message_source_interface");

  public:

    session_state_machine(boost::asio::any_io_executor ex,
                          subscription_manager subs,
                          reconnect_policy policy = reconnect_policy{});
    ~session_state_machine();

    // Make the state machine non-copyable and non-movable
    session_state_machine(const session_state_machine& p) = delete;
    session_state_machine &operator=(const session_state_machine& p) = delete;
    session_state_machine(session_state_machine&& p) = delete;
    session_state_machine &operator=(session_state_machine&& p) = delete;

    void run(std::shared_ptr<ws_session_interface> s);

    // Intentional shutdown: cancels any pending retry and closes the session
    void stop();

    session_state state() const { return curr_state; }

    // push() / pop() are inherited from Queue

  private:

    void on_connection_status(connection_status status);
    void on_message(std::span<const std::byte> payload);

    void on_connected();
    void on_disconnect();
    void on_error();

    void schedule_reconnect(reconnect_policy::duration delay);

  private:

    std::shared_ptr<ws_session_interface> session;

    session_state curr_state;

    subscription_manager subs;

    // Stamped on every received_message; incremented on each successful connect
    std::uint64_t connection_epoch = 0;

    reconnect_policy policy;
    boost::asio::steady_timer retry_timer;
};
