#pragma once

// Shared code for the live Kraken tests in tests/live:
//   - command line parsing (-t seconds, symbols)
//   - the live Kraken v2 book feed: ws_session -> session_state_machine (queue)
//   - shutdown on Ctrl-C / SIGTERM / timeout
//   - the consumer thread's poll loop
//
// Threads:
//   main     - runs the io_context (ws_session + state machine, which push to the queue)
//   consumer - the test's own code: pops from the state machine and reports

#include "ws_session.hpp"
#include "session_state_machine.hpp"
#include "message_source_deque.hpp"
#include "subscription_manager.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace live {

  using kraken_source = session_state_machine<message_source_deque>;
  using clock = std::chrono::steady_clock;

  struct options
  {
    int seconds = 0;
    std::vector<std::string> symbols;
    bool help = false;
  };

  inline void usage(const char* prog)
  {
    std::cerr << "Usage: " << prog << " [-t seconds] [symbol ...]\n"
              << "  -t seconds  run time, default 0 (run until Ctrl-C)\n"
              << "  symbol      one or more Kraken v2 pairs, default BTC/USD\n"
              << "Example: " << prog << " -t 10 BTC/USD ETH/USD SOL/USD\n";
  }

  // Returns nullopt on bad arguments
  inline std::optional<options> parse_args(int argc, char* argv[])
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

  // Consumer loop: pops until `running` goes false, then drains what's left.
  //   on_message(received_message&&) - once per message
  //   on_tick(elapsed_seconds)       - about once a second
  template <class OnMessage, class OnTick>
  void poll(kraken_source& source, const std::atomic<bool>& running,
            OnMessage&& on_message, OnTick&& on_tick)
  {
    const auto start = clock::now();
    auto next_tick = start + std::chrono::seconds(1);

    while (running.load(std::memory_order_acquire))
    {
      if (auto msg = source.pop())
      {
        on_message(std::move(*msg));
      }
      else
      {
        // Nothing queued: back off briefly. A pinned decode thread would spin instead.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }

      if (const auto now = clock::now(); now >= next_tick)
      {
        on_tick(std::chrono::duration_cast<std::chrono::seconds>(now - start).count());
        next_tick = now + std::chrono::seconds(1);
      }
    }

    while (auto msg = source.pop())
    {
      on_message(std::move(*msg));
    }
  }

  // Runs a live Kraken v2 book feed (depth 10) for opts.symbols.
  // The io_context runs on the calling thread; consumer(kraken_source&, const
  // std::atomic<bool>& running) runs on a second thread and must return once
  // running goes false (live::poll does this). Returns after Ctrl-C / SIGTERM or
  // opts.seconds, once the consumer has finished.
  template <class Consumer>
  void run_kraken_book_feed(const options& opts, Consumer&& consumer)
  {
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
    for (const auto& sym : opts.symbols)
    {
      channels.push_back(channel_id{sym});
    }
    subs.set_channels(channels);

    // The state machine is the queue: received messages are pushed into it, and
    // the consumer pops from it
    auto sm = std::make_shared<kraken_source>(strand, std::move(subs));

    std::atomic<bool> running{true};
    std::thread consumer_thread([&] { consumer(*sm, running); });

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

    if (opts.seconds > 0)
    {
      timer.expires_after(std::chrono::seconds(opts.seconds));
      timer.async_wait([&](const boost::system::error_code& ec) {
        if (!ec) shutdown();
      });
    }

    std::cout << "Connecting to wss://" << host << "/v2, book";
    for (const auto& sym : opts.symbols)
    {
      std::cout << " " << sym;
    }
    std::cout << (opts.seconds > 0 ? " for " + std::to_string(opts.seconds) + "s"
                                   : " (Ctrl-C to stop)")
              << std::endl;

    net::post(strand, [&] { sm->run(session); });
    ioc.run();

    // Producer is done: stop the consumer and wait for it to drain the queue
    running.store(false, std::memory_order_release);
    consumer_thread.join();
  }

} // namespace live
