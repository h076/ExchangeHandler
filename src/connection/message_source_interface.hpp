#pragma once

#include <optional>

#include "received_message.hpp"

class message_source_interface
{
  public:

    // Virtual destructor to allow inherited class destructor to be called
    virtual ~message_source_interface() = default;

    virtual void push(received_message msg) = 0;
    virtual std::optional<received_message> pop() = 0;

  protected:

    message_source_interface() = default;

    message_source_interface(const message_source_interface&) = default;
    message_source_interface(message_source_interface&&) = default;
    message_source_interface& operator=(const message_source_interface&) = default;
    message_source_interface& operator=(message_source_interface&&) = default;

};
