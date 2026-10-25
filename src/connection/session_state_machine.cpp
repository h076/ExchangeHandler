#include "session_state_machine.hpp"
#include "message_source_deque.hpp"

#include <iostream>

const char* to_string(session_state s)
{
  switch (s)
  {
    case session_state::idle:        return "idle";
    case session_state::connecting:  return "connecting";
    case session_state::live:        return "live";
    case session_state::backing_off: return "backing_off";
    case session_state::stopping:    return "stopping";
    case session_state::stopped:     return "stopped";
    case session_state::failed:      return "failed";
  }
  return "unknown";
}

template <class Queue>
session_state_machine<Queue>::session_state_machine(boost::asio::any_io_executor ex,
                                                    subscription_manager s,
                                                    reconnect_policy p)
  : curr_state(session_state::idle),
    subs(std::move(s)),
    policy(std::move(p)),
    retry_timer(std::move(ex))
{}

template <class Queue>
session_state_machine<Queue>::~session_state_machine()
{}

template <class Queue>
void session_state_machine<Queue>::run(std::shared_ptr<ws_session_interface> s)
{
  session = std::move(s);

  // Use a weak ptr for the call back to prevent nullptr ref if
  // state machine is destroyed before session
  session->set_message_handler([weak = this->weak_from_this()] (std::span<const std::byte> data)
      {
        if (auto self = weak.lock())
        {
          self->on_message(data);
        }
      });

  session->set_state_handler([weak = this->weak_from_this()] (connection_status status)
      { 
        if (auto self = weak.lock())
        {
          self->on_connection_status(status);
        }
      });

  curr_state = session_state::connecting;
  session->connect();
}

template <class Queue>
void session_state_machine<Queue>::stop()
{
  switch (curr_state)
  {
    case session_state::connecting:
    case session_state::live:
      // Session is in flight: it reports disconnected once closed
      curr_state = session_state::stopping;
      session->disconnect();
      break;

    case session_state::backing_off:
    case session_state::idle:
    case session_state::failed:
      // Nothing in flight, the pending retry (if any) is all there is to cancel
      retry_timer.cancel();
      policy.reset();
      curr_state = session_state::stopped;
      break;

    case session_state::stopping:
    case session_state::stopped:
      break;
  }
}

template <class Queue>
void session_state_machine<Queue>::on_connection_status(connection_status status)
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

template <class Queue>
void session_state_machine<Queue>::on_connected()
{
  // New connection: anything received from here on belongs to a new epoch
  ++connection_epoch;
  policy.on_connected(reconnect_policy::clock::now());

  // stop() raced the handshake: the session will close itself, don't subscribe
  if (curr_state == session_state::stopping)
  {
    return;
  }

  curr_state = session_state::live;

  for (auto& msg : subs.get_subscription_messages())
  {
    session->send(std::move(msg));
  }
}

template <class Queue>
void session_state_machine<Queue>::on_disconnect()
{
  // Requested close completed
  retry_timer.cancel();
  policy.reset();
  curr_state = session_state::stopped;
}

template <class Queue>
void session_state_machine<Queue>::on_error()
{
  if (curr_state == session_state::stopping)
  {
    // Failed while closing on request: still a stop, not a reconnect
    on_disconnect();
    return;
  }

  // Only a live session can be "stable": a failed attempt never reached live
  if (curr_state == session_state::live)
  {
    policy.on_connection_lost(reconnect_policy::clock::now());
  }

  auto delay = policy.next_delay();
  if (!delay)
  {
    std::cerr << "session_state_machine: giving up after "
              << policy.attempts() << " reconnect attempts" << std::endl;
    curr_state = session_state::failed;
    return;
  }

  schedule_reconnect(*delay);
}

template <class Queue>
void session_state_machine<Queue>::schedule_reconnect(reconnect_policy::duration delay)
{
  std::cerr << "session_state_machine: reconnect attempt " << policy.attempts()
            << " in " << delay.count() << "ms" << std::endl;

  curr_state = session_state::backing_off;

  // Always go through the timer, even for 0ms: connect() then runs from a
  // fresh handler rather than re-entering the session's completion handler.
  retry_timer.expires_after(delay);
  retry_timer.async_wait([weak = this->weak_from_this()] (boost::system::error_code ec)
      {
        if (ec)
        {
          return;   // cancelled by stop()
        }

        if (auto self = weak.lock())
        {
          // stop() may have run between expiry and this handler
          if (self->curr_state != session_state::backing_off)
          {
            return;
          }

          self->curr_state = session_state::connecting;
          self->session->connect();
        }
      });
}

template <class Queue>
void session_state_machine<Queue>::on_message(std::span<const std::byte> data)
{
  // The span points into ws_session's read buffer and is only valid for this
  // call, so the payload is copied before it crosses to the decode thread.
  received_message msg;
  msg.recv_time = received_message::clock::now();
  msg.payload.assign(reinterpret_cast<const char*>(data.data()), data.size());
  msg.connection_epoch = connection_epoch;

  // Qualified call: goes straight to Queue's push, no virtual dispatch
  Queue::push(std::move(msg));
}

// Explicit instantiations: the member definitions above live in this .cpp, so
// each Queue type used with the state machine must be listed here.
template class session_state_machine<message_source_deque>;
