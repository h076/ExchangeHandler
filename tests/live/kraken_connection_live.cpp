// Live smoke test for the connection layer against Kraken's public v2 book feed.
//
// Usage: kraken_connection_live [-t seconds] [symbol ...]
//   -t seconds - run time, default 0 (run until Ctrl-C)
//   symbol     - one or more Kraken v2 pairs, default "BTC/USD"
//
// Example: kraken_connection_live -t 10 BTC/USD ETH/USD SOL/USD
//
// The consumer stands in for the decode layer: it pops raw payloads from the
// state machine and reports counts, without decoding them.

#include "live_common.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
  const auto opts = live::parse_args(argc, argv);
  if (!opts || opts->help)
  {
    live::usage(argv[0]);
    return opts ? 0 : 2;
  }

  const std::vector<std::string>& symbols = opts->symbols;

  // Counters are only touched by the consumer thread (and read after it is
  // joined), so they need no synchronisation.
  std::size_t msg_count = 0;
  std::size_t byte_count = 0;

  // Per-symbol message counts, matched on the "symbol":"X" field. Anything
  // without a subscribed symbol (heartbeats, status, ...) counts as "other".
  // A plain substring search: good enough for a smoke test, decode does it properly.
  std::vector<std::string> symbol_tags;
  for (const auto& sym : symbols)
  {
    symbol_tags.push_back("\"symbol\":\"" + sym + "\"");
  }
  std::vector<std::size_t> symbol_counts(symbols.size(), 0);
  std::size_t other_count = 0;

  auto print_symbol_counts = [&] {
    std::cout << "  per symbol:";
    for (std::size_t i = 0; i < symbols.size(); ++i)
    {
      std::cout << " " << symbols[i] << "=" << symbol_counts[i];
    }
    std::cout << " other=" << other_count << std::endl;
  };

  live::run_kraken_book_feed(*opts, [&](live::kraken_source& source,
                                        const std::atomic<bool>& running) {
    std::size_t prev_count = 0;
    std::string last_msg;
    std::uint64_t epoch = 0;
    live::clock::duration max_queue_delay{};   // recv_time -> pop, per report interval

    auto take = [&](received_message&& msg) {
      ++msg_count;
      byte_count += msg.payload.size();
      epoch = msg.connection_epoch;

      bool matched = false;
      for (std::size_t i = 0; i < symbol_tags.size(); ++i)
      {
        if (msg.payload.find(symbol_tags[i]) != std::string::npos)
        {
          ++symbol_counts[i];
          matched = true;
        }
      }
      if (!matched)
      {
        ++other_count;
      }

      max_queue_delay = std::max(max_queue_delay, live::clock::now() - msg.recv_time);
      last_msg = std::move(msg.payload);
    };

    auto report = [&](long long elapsed) {
      std::cout << "[" << elapsed << "s] " << msg_count << " msgs (+"
                << msg_count - prev_count << "), " << byte_count << " bytes, epoch "
                << epoch << ", max queue delay "
                << std::chrono::duration_cast<std::chrono::microseconds>(max_queue_delay).count()
                << "us\n"
                << "  last: " << last_msg.substr(0, 300)
                << (last_msg.size() > 300 ? "..." : "") << std::endl;
      print_symbol_counts();
      prev_count = msg_count;
      max_queue_delay = {};
    };

    live::poll(source, running, take, report);
  });

  std::cout << "Received " << msg_count << " messages, " << byte_count << " bytes" << std::endl;
  print_symbol_counts();
  return 0;
}
