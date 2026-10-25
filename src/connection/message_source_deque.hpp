#pragma once

#include "message_source_interface.hpp"

#include <atomic>
#include <deque>
#include <mutex>

class message_source_deque : public message_source_interface
{
  public:

    message_source_deque();

    void push(received_message msg) override;
    std::optional<received_message> pop() override;

  private:

    std::deque<received_message> q;
    std::mutex q_mtx;
    std::atomic<bool> empty;
};
