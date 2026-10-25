// Live smoke test for the connection layer against Kraken's public v2 book feed.
//
// Usage: kraken_test [-t seconds] [symbol ...]
//   -t seconds - run time, default 0 (run until Ctrl-C)
//   symbol     - one or more Kraken v2 pairs, default "BTC/USD"
//
// Example: kraken_test -t 10 BTC/USD ETH/USD SOL/USD
//
// Threads:
//   main     - runs the io_context (ws_session + state machine, which push to the queue)
//   consumer - stands in for the decode layer: pops from the state machine and reports

#include "ws_session.hpp"
#include "session_state_machine.hpp"
#include "message_source_deque.hpp"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

struct options
{
  int seconds = 0;
  std::vector<std::string> symbols;
  bool help = false;
};

static void usage(const char* prog)
{
  std::cerr << "Usage: " << prog << " [-t seconds] [symbol ...]\n"
            << "  -t seconds  run time, default 0 (run until Ctrl-C)\n"
            << "  symbol      one or more Kraken v2 pairs, default BTC/USD\n"
            << "Example: " << prog << " -t 10 BTC/USD ETH/USD SOL/USD\n";
}

// Returns nullopt on bad arguments
static std::optional<options> parse_args(int argc, char* argv[])
{
  options opts;
  for (int i = 1; i < argc; ++i)
  {
    const std::string_view arg = argv[i];
    if (arg == "-h" || arg == "--help")
    {
      opts.help = true;
      return opts;
    }
    if (arg == "-t" || arg == "--seconds")
    {
      if (i + 1 >= argc)
      {
        std::cerr << "error: " << arg << " needs a value\n";
        return std::nullopt;
      }
      try
      {
        std::size_t used = 0;
        opts.seconds = std::stoi(argv[++i], &used);
        if (used != std::string_view(argv[i]).size() || opts.seconds < 0)
          throw std::invalid_argument("bad");
      }
      catch (const std::exception&)
      {
        std::cerr << "error: invalid seconds '" << argv[i] << "'\n";
        return std::nullopt;
      }
      continue;
    }
    if (arg.starts_with("-"))
    {
      std::cerr << "error: unknown option '" << arg << "'\n";
      return std::nullopt;
    }
    // Skip duplicates so each pair is subscribed once
    if (std::find(opts.symbols.begin(), opts.symbols.end(), arg) == opts.symbols.end())
    {
      opts.symbols.emplace_back(arg);
    }
  }

  if (opts.symbols.empty())
  {
    opts.symbols.emplace_back("BTC/USD");
  }
  return opts;
}

int main(int argc, char* argv[])
{
  const auto opts = parse_args(argc, argv);
  if (!opts || opts->help)
  {
    usage(argv[0]);
    return opts ? 0 : 2;
  }

  const int seconds = opts->seconds;
  const std::vector<std::string>& symbols = opts->symbols;
  const std::string host = "ws.kraken.com";

  net::io_context ioc;

  // Verify the server certificate and host name
  net::ssl::context tls(net::ssl::context::tls_client);
  tls.set_default_verify_paths();
  tls.set_verify_mode(net::ssl::verify_peer);
  tls.set_verify_callback(net::ssl::host_name_verification(host));

  // ws_session's flags and write channel rely on a strand
  auto strand = net::make_strand(ioc);
  auto session = std::make_shared<ws_session>(strand,
      connection_config{host, 443, "/v2", std::move(tls)});

  subscription_manager subs([](const channel_id& c) {
    return R"({"method":"subscribe","params":{"channel":"book","symbol":[")"
           + c.id + R"("],"depth":10}})";
  });
  // One subscribe message per symbol
  std::vector<channel_id> channels;
  for (const auto& sym : symbols)
  {
    channels.push_back(channel_id{sym});
  }
  subs.set_channels(channels);

  // The state machine is the queue: received messages are pushed into it, and
  // the consumer below pops from it
  auto sm = std::make_shared<session_state_machine<message_source_deque>>(
      strand, std::move(subs));

  // Consumer thread: pops until told to stop, then drains what's left.
  // Counters are only touched by this thread, so they need no synchronisation.
  std::atomic<bool> consuming{true};
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

  std::thread consumer([&] {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    auto next_report = start + std::chrono::seconds(1);
    std::size_t prev_count = 0;
    std::string last_msg;
    std::uint64_t epoch = 0;
    clock::duration max_queue_delay{};   // recv_time -> pop, per report interval

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

      max_queue_delay = std::max(max_queue_delay, clock::now() - msg.recv_time);
      last_msg = std::move(msg.payload);
    };

    while (consuming.load(std::memory_order_acquire))
    {
      if (auto msg = sm->pop())
      {
        take(std::move(*msg));
      }
      else
      {
        // Nothing queued: back off briefly. A pinned decode thread would spin instead.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }

      if (const auto now = clock::now(); now >= next_report)
      {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
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
        next_report = now + std::chrono::seconds(1);
      }
    }

    while (auto msg = sm->pop())
    {
      take(std::move(*msg));
    }
  });

  // Stop on Ctrl-C or after `seconds`. Cancel the other waiter so ioc.run() can return.
  net::signal_set signals(ioc, SIGINT, SIGTERM);
  net::steady_timer timer(ioc);

  auto shutdown = [&] {
    signals.cancel();
    timer.cancel();
    net::post(strand, [&] { sm->stop(); });
  };

  signals.async_wait([&](const boost::system::error_code& ec, int) {
    if (!ec) shutdown();
  });

  if (seconds > 0)
  {
    timer.expires_after(std::chrono::seconds(seconds));
    timer.async_wait([&](const boost::system::error_code& ec) {
      if (!ec) shutdown();
    });
  }

  std::cout << "Connecting to wss://" << host << "/v2, book";
  for (const auto& sym : symbols)
  {
    std::cout << " " << sym;
  }
  std::cout << (seconds > 0 ? " for " + std::to_string(seconds) + "s" : " (Ctrl-C to stop)")
            << std::endl;

  net::post(strand, [&] { sm->run(session); });
  ioc.run();

  // Producer is done: stop the consumer and wait for it to drain the queue
  consuming.store(false, std::memory_order_release);
  consumer.join();

  std::cout << "Received " << msg_count << " messages, " << byte_count << " bytes" << std::endl;
  print_symbol_counts();
  return 0;
}
