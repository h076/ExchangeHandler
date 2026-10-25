#pragma once

#include <memory>

#include "ws_session_interface.hpp"
#include "subscription_manager.hpp"

/* state machine should expose a message source interface 
 * to allow decode layer to pop from the queue of received data */ 

enum class message_type
{
  auth,
  ping,
  subscribe,
  reconnect,
  disconnect,
  unknown
};

class session_state_machine : public std::enable_shared_from_this<session_state_machine>
{
  public:

    // Receives every application payload. The span is only valid for the
    // duration of the call: copy it if it needs to outlive the callback.
    using message_sink = std::function<void(std::span<const std::byte>)>;

    session_state_machine(subscription_manager subs, message_sink sink);
    ~session_state_machine();

    // Make the state machine non-copyable and non-movable
    session_state_machine(const session_state_machine& p) = delete;
    session_state_machine &operator=(const session_state_machine& p) = delete;
    session_state_machine(session_state_machine&& p) = delete;
    session_state_machine &operator=(session_state_machine&& p) = delete;

    void run(std::shared_ptr<ws_session_interface> s);

  private:

    void on_connection_status(connection_status status);
    void on_message(std::span<const std::byte> payload);

    void on_connected();
    void on_disconnect();
    void on_error();
    void on_write_failed();

  private:

    std::shared_ptr<ws_session_interface> session;

    connection_status curr_state;

    subscription_manager subs;
    message_sink sink;

};
