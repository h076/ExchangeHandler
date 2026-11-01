// Live test for the decode layer: connection + kraken_decoder against Kraken's
// public v2 book feed. A resync_standin_sink takes the place of resync.
//
// Usage: kraken_decode_live [-t seconds] [symbol ...]
//   -t seconds - run time, default 0 (run until Ctrl-C)
//   symbol     - one or more Kraken v2 pairs, default "BTC/USD"
//
// Example: kraken_decode_live -t 10 BTC/USD ETH/USD SOL/USD
//
// Exit code 1 if any message was malformed, a subscribed symbol got no
// snapshot, an update arrived before its snapshot on a connection, or a
// message was for a symbol that wasn't subscribed.

#include "live_common.hpp"
#include "resync_standin_sink.hpp"

#include "kraken_decoder.hpp"

#include <iostream>
#include <string>
#include <string_view>

// Prints the start of a payload the decoder rejected
static void dump(std::string_view label, const std::string& payload)
{
  std::cout << "  " << label << ": " << payload.substr(0, 300)
            << (payload.size() > 300 ? "..." : "") << std::endl;
}

// whole_run: latency over the whole run instead of the current interval
static void print_stats(const decoder& dec, const resync_standin_sink& sink, bool whole_run = false)
{
  const auto avg = whole_run ? sink.total_avg_latency() : sink.interval_avg_latency();
  const auto max = whole_run ? sink.total_max_latency() : sink.interval_max_latency();

  const auto& st = dec.stats();
  std::cout << "decode ok=" << st.ok << " ignored=" << st.ignored
            << " malformed=" << st.malformed << " unknown=" << st.unknown_type
            << " | epoch " << sink.epoch() << " reconnects " << sink.reconnects()
            << " | latency avg " << avg.count() << "us max " << max.count() << "us\n";

  for (const auto& s : sink.symbols())
  {
    std::cout << "  " << s.name << " snap=" << s.snapshots << " upd=" << s.updates
              << " levels=" << s.levels;
    if (s.has_top)
    {
      std::cout << " bid=" << format_fixed(s.best_bid) << " ask=" << format_fixed(s.best_ask);
    }
    std::cout << "\n";
  }

  if (sink.update_before_snapshot() || sink.unknown_symbol())
  {
    std::cout << "  update_before_snapshot=" << sink.update_before_snapshot()
              << " unknown_symbol=" << sink.unknown_symbol() << "\n";
  }
  std::cout << std::flush;
}

int main(int argc, char* argv[])
{
  const auto opts = live::parse_args(argc, argv);
  if (!opts || opts->help)
  {
    live::usage(argv[0]);
    return opts ? 0 : 2;
  }

  // Only used on the consumer thread, then read here after it is joined
  resync_standin_sink sink(opts->symbols);
  kraken_decoder<resync_standin_sink> dec(sink);

  constexpr int max_dumps = 5;   // rejected payloads printed in full-ish
  int dumps = 0;

  live::run_kraken_book_feed(*opts, [&](live::kraken_source& source,
                                        const std::atomic<bool>& running) {
    auto take = [&](received_message&& msg) {
      const auto status = dec.decode(msg);
      if ((status == decode_status::malformed || status == decode_status::unknown_type)
          && dumps < max_dumps)
      {
        ++dumps;
        dump(status == decode_status::malformed ? "malformed" : "unknown_type", msg.payload);
      }
    };

    auto report = [&](long long elapsed) {
      std::cout << "[" << elapsed << "s] ";
      print_stats(dec, sink);
      sink.reset_interval();
    };

    live::poll(source, running, take, report);
  });

  std::cout << "\nFinal: ";
  print_stats(dec, sink, true);

  // Pass / fail
  bool ok = true;
  auto fail = [&](const std::string& why) {
    std::cout << "FAIL: " << why << "\n";
    ok = false;
  };

  if (dec.stats().malformed)
  {
    fail(std::to_string(dec.stats().malformed) + " malformed message(s)");
  }
  for (const auto& s : sink.symbols())
  {
    if (s.snapshots == 0) fail("no snapshot for " + s.name);
  }
  if (sink.update_before_snapshot())
  {
    fail(std::to_string(sink.update_before_snapshot()) + " update(s) before snapshot");
  }
  if (sink.unknown_symbol())
  {
    fail(std::to_string(sink.unknown_symbol()) + " message(s) for unsubscribed symbols");
  }

  std::cout << (ok ? "PASS" : "FAILED") << std::endl;
  return ok ? 0 : 1;
}
