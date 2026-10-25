
#include "message_source_deque.hpp"

message_source_deque::message_source_deque()
  : empty(true) {}

void message_source_deque::push(received_message msg)
{
  std::lock_guard lk{q_mtx};

  q.push_back(std::move(msg));
  if (empty)
  {
    empty.store(false, std::memory_order_relaxed);
  }
}

std::optional<received_message> message_source_deque::pop()
{
  if (empty.load(std::memory_order_relaxed))
  {
    return {};
  }

  std::lock_guard lk{q_mtx};

  if (q.empty())
  {
    empty.store(true, std::memory_order_relaxed);
    return {};
  }

  std::optional<received_message> msg{std::move(q.front())};
  q.pop_front();
  empty.store(q.empty(), std::memory_order_relaxed);

  return msg;
}
