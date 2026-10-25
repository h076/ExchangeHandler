#include <algorithm>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "message_source_deque.hpp"
#include "../check.hpp"

static received_message msg(std::string payload, std::uint64_t epoch = 1)
{
  return received_message{std::move(payload), received_message::clock::now(), epoch};
}

// Payload of a popped message, or nullopt if the pop was empty
static std::optional<std::string> payload(std::optional<received_message> m)
{
  if (!m) return std::nullopt;
  return std::move(m->payload);
}

static void empty_queue_pops_nothing()
{
  message_source_deque q;
  CHECK(!q.pop().has_value());
  CHECK(!q.pop().has_value());
}

static void pops_in_fifo_order()
{
  message_source_deque q;
  q.push(msg("a"));
  q.push(msg("b"));
  q.push(msg("c"));
  CHECK(payload(q.pop()) == std::optional<std::string>("a"));
  CHECK(payload(q.pop()) == std::optional<std::string>("b"));
  CHECK(payload(q.pop()) == std::optional<std::string>("c"));
  CHECK(!q.pop().has_value());
}

static void refills_after_draining()
{
  // The empty flag must flip back to false on the next push
  message_source_deque q;
  q.push(msg("1"));
  CHECK(payload(q.pop()) == std::optional<std::string>("1"));
  CHECK(!q.pop().has_value());

  q.push(msg("2"));
  CHECK(payload(q.pop()) == std::optional<std::string>("2"));
  CHECK(!q.pop().has_value());
}

static void interleaved_push_pop()
{
  message_source_deque q;
  q.push(msg("1"));
  q.push(msg("2"));
  CHECK(payload(q.pop()) == std::optional<std::string>("1"));
  q.push(msg("3"));
  CHECK(payload(q.pop()) == std::optional<std::string>("2"));
  CHECK(payload(q.pop()) == std::optional<std::string>("3"));
  CHECK(!q.pop().has_value());
}

static void usable_through_interface()
{
  message_source_deque impl;
  message_source_interface& q = impl;
  q.push(msg("x"));
  CHECK(payload(q.pop()) == std::optional<std::string>("x"));
}

static void metadata_round_trips()
{
  message_source_deque q;
  const auto t = received_message::clock::now();
  q.push(received_message{"p", t, 42});

  auto m = q.pop();
  CHECK(m.has_value());
  if (!m) return;
  CHECK(m->payload == "p");
  CHECK(m->recv_time == t);
  CHECK(m->connection_epoch == 42);
}

static void large_payload_round_trips()
{
  message_source_deque q;
  const std::string big(1 << 20, 'z');
  q.push(msg(big));
  CHECK(payload(q.pop()) == std::optional<std::string>(big));
}

// One producer, one consumer: the production shape (session strand -> decode thread).
// Every message must arrive exactly once and in order.
static void spsc_threads_preserve_order()
{
  constexpr int n = 200'000;
  message_source_deque q;

  std::thread producer([&] {
    for (int i = 0; i < n; ++i) q.push(msg(std::to_string(i)));
  });

  int expected = 0;
  bool in_order = true;
  while (expected < n)
  {
    if (auto m = q.pop())
    {
      if (m->payload != std::to_string(expected)) in_order = false;
      ++expected;
    }
  }
  producer.join();

  CHECK(in_order);
  CHECK(expected == n);
  CHECK(!q.pop().has_value());
}

// Multiple consumers exercise the re-check under the lock: no message may be
// lost, duplicated, or popped from an empty deque.
static void multiple_consumers_lose_nothing()
{
  constexpr int n = 100'000;
  constexpr int consumers = 4;
  message_source_deque q;
  std::atomic<int> popped{0};
  std::vector<std::vector<int>> seen(consumers);

  std::vector<std::thread> threads;
  for (int c = 0; c < consumers; ++c)
  {
    threads.emplace_back([&, c] {
      while (popped.load() < n)
      {
        if (auto m = q.pop())
        {
          seen[c].push_back(std::stoi(m->payload));
          ++popped;
        }
      }
    });
  }

  for (int i = 0; i < n; ++i) q.push(msg(std::to_string(i)));
  for (auto& t : threads) t.join();

  std::vector<int> all;
  for (auto& s : seen) all.insert(all.end(), s.begin(), s.end());
  std::sort(all.begin(), all.end());

  CHECK(static_cast<int>(all.size()) == n);
  bool exact = static_cast<int>(all.size()) == n;
  for (int i = 0; exact && i < n; ++i) exact = all[i] == i;
  CHECK(exact);
}

int main()
{
  std::cout << "Running message_source_deque tests...\n\n";

  test::run("EmptyQueuePopsNothing", empty_queue_pops_nothing);
  test::run("PopsInFifoOrder", pops_in_fifo_order);
  test::run("RefillsAfterDraining", refills_after_draining);
  test::run("InterleavedPushPop", interleaved_push_pop);
  test::run("UsableThroughInterface", usable_through_interface);
  test::run("MetadataRoundTrips", metadata_round_trips);
  test::run("LargePayloadRoundTrips", large_payload_round_trips);
  test::run("SpscThreadsPreserveOrder", spsc_threads_preserve_order);
  test::run("MultipleConsumersLoseNothing", multiple_consumers_lose_nothing);

  return test::summary();
}
