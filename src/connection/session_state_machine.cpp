#include "session_state_machine.hpp"

session_state_machine::session_state_machine(std::unique_ptr<ws_session_interface> s)
  : session(std::move(s)), curr_state(connection_status::disconnected)
{
  // Use a weak ptr for the call back to prevent nullptr ref if
  // state machine is destroyed before session
  session->set_message_handler([weak = weak_from_this()] (std::span<const std::byte> data)
      {
        if (auto self = weak.lock())
        {
          self->on_message(data);
        }
      });

  session->set_state_handler([weak = weak_from_this()] (connection_status status)
      { 
        if (auto self = weak.lock())
        {
          self->on_connection_status(status);
        }
      });
}

session_state_machine::~session_state_machine()
{}

void session_state_machine::on_connection_status(connection_status status)
{
  switch (status)
  {
    case connection_status::connected:
      on_connected();
      break;
    case connection_status::disconnected:
      on_disconnect();
      break;
    case connection_status::error:
      on_error();
      break;
  }
}

void session_state_machine::on_connected()
{
  // handle connection established event
  // get subsciption messages from sub manager
  // Send out sub messages
}

void session_state_machine::on_disconnect()
{
  // Reconnect the session (should use a reconnect policy here)
  session->connect();
}

void session_state_machine::on_error()
{
  // Teardown session
  session->disconnect();
}

void session_state_machine::on_message(std::span<const std::byte> data)
{
}
