#include "session_state_machine.hpp"
#include <iostream>

session_state_machine::session_state_machine(subscription_manager s, message_sink m)
  : curr_state(connection_status::disconnected), subs(std::move(s)), sink(std::move(m))
{}

session_state_machine::~session_state_machine()
{}

void session_state_machine::run(std::shared_ptr<ws_session_interface> s)
{
  session = std::move(s);

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

  session->connect();
}

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
  curr_state = connection_status::connected;

  for (auto& msg : subs.get_subscription_messages())
  {
    session->send(std::move(msg));
  }
}

void session_state_machine::on_disconnect()
{
  // Requested close completed, nothing to do
  curr_state = connection_status::disconnected;
}

void session_state_machine::on_error()
{
  // TODO: reconnect via a backoff policy once the stream is rebuilt per connect().
  // Reconnecting immediately on the same SSL stream fails and spins in a tight loop.
  curr_state = connection_status::error;
  std::cerr << "session_state_machine: session lost, reconnect not implemented yet" << std::endl;
}

void session_state_machine::on_message(std::span<const std::byte> data)
{
  if (sink)
  {
    sink(data);
  }
}
