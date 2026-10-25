#pragma once

#include <functional>
#include <string>
#include <span>

enum class connection_status
{
  connected,
  disconnected,
  error
};

class ws_session_interface
{
  public:

    virtual ~ws_session_interface() = default;
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual void send(std::string payload) = 0;

    // handles application data
    virtual void set_message_handler(std::function<void(std::span<const std::byte>)> handler) = 0;

    // handles an event that is independant of any application payload
    virtual void set_state_handler(std::function<void(connection_status)> handler) = 0;
};
