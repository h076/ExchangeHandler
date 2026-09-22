#include "ws_session.hpp"

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

class session_state_machine : std::enable_shared_from_this<session_state_machine>
{
  public:

    session_state_machine(std::unique_ptr<ws_session_interface> s);
    ~session_state_machine();

    // Make the state machine non-copyable and non-movable
    session_state_machine(const session_state_machine& p) = delete;
    session_state_machine &operator=(const session_state_machine& p) = delete;
    session_state_machine(session_state_machine&& p) = delete;
    session_state_machine &operator=(session_state_machine&& p) = delete;

  private:

    void on_connection_status(connection_status status);
    void on_message(std::span<const std::byte> payload);

    void on_connected();
    void on_disconnect();
    void on_error();
    void on_write_failed();

  private:

    std::unique_ptr<ws_session_interface> session;

    connection_status curr_state;

};
