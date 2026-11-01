#pragma once

// Stands in for the resync layer (layer three) in the live decode test.
//
// kraken_decoder<resync_standin_sink> calls it exactly where it will call the
// real resync stage. It doesn't keep a book: it only records what arrives so
// the test can report on it and check:
//   - per symbol: snapshot / update / level counts, top of book from the last snapshot
//   - connection epochs: counts reconnects, and flags any update that arrives
//     on an epoch before that symbol's snapshot (resync will need this guarantee)
//   - messages for symbols that were never subscribed
//   - latency from recv_time (connection layer) to the sink: queue wait + decode
//
// Only used on the consumer thread, so nothing is synchronised.

// Relative paths: this header has no compile command of its own, and clangd may
// borrow one without src/common on the include path
#include "../../src/common/kraken_messages.hpp"
#include "../../src/common/market_types.hpp"
#include "../../src/common/received_message.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

// Formats a fixed-point value for display: 6500150000000 -> "65001.5"
inline std::string format_fixed(mb::fixed v)
{
  const bool negative = v < 0;
  const std::uint64_t mag = negative ? 0 - static_cast<std::uint64_t>(v)
                                     : static_cast<std::uint64_t>(v);
  const std::uint64_t scale = static_cast<std::uint64_t>(mb::fixed_scale);

  char frac[mb::fixed_decimals + 1];
  std::snprintf(frac, sizeof frac, "%0*llu", mb::fixed_decimals,
                static_cast<unsigned long long>(mag % scale));

  // Trim trailing zeros, keeping at least one decimal
  int len = mb::fixed_decimals;
  while (len > 1 && frac[len - 1] == '0') --len;

  return (negative ? "-" : "") + std::to_string(mag / scale) + "." + std::string(frac, len);
}

class resync_standin_sink
{
  public:

    struct symbol_stats
    {
      std::string name;
      std::uint64_t snapshots = 0;
      std::uint64_t updates = 0;
      std::uint64_t levels = 0;            // across snapshots and updates
      std::uint64_t snapshot_epoch = 0;    // epoch of the last snapshot, 0 = none yet
      bool has_top = false;                // best_bid / best_ask valid
      mb::fixed best_bid = 0;              // from the last snapshot
      mb::fixed best_ask = 0;
    };

    explicit resync_standin_sink(const std::vector<std::string>& symbols)
    {
      for (const auto& s : symbols)
      {
        m_symbols.push_back(symbol_stats{.name = s});
      }
    }

    void on_book_snapshot(const kraken::book_snapshot& snap)
    {
      on_message(snap.rx);
      symbol_stats* s = find(snap.sym.view());
      if (!s) return;

      ++s->snapshots;
      s->levels += snap.bids.size() + snap.asks.size();
      s->snapshot_epoch = snap.rx.connection_epoch;

      // Kraken sends bids best (highest) first and asks best (lowest) first
      s->has_top = !snap.bids.empty() && !snap.asks.empty();
      if (s->has_top)
      {
        s->best_bid = snap.bids.front().price;
        s->best_ask = snap.asks.front().price;
      }
    }

    void on_book_update(const kraken::book_update& upd)
    {
      on_message(upd.rx);
      symbol_stats* s = find(upd.sym.view());
      if (!s) return;

      ++s->updates;
      s->levels += upd.bids.size() + upd.asks.size();
      if (s->snapshot_epoch != upd.rx.connection_epoch)
      {
        ++m_update_before_snapshot;
      }
    }

    const std::vector<symbol_stats>& symbols() const { return m_symbols; }
    std::uint64_t epoch() const { return m_epoch; }
    std::uint64_t reconnects() const { return m_reconnects; }
    std::uint64_t update_before_snapshot() const { return m_update_before_snapshot; }
    std::uint64_t unknown_symbol() const { return m_unknown_symbol; }

    // Latency over the current interval; reset_interval() starts a new one
    std::uint64_t interval_count() const { return m_lat_count; }
    std::chrono::microseconds interval_avg_latency() const
    {
      return m_lat_count ? std::chrono::duration_cast<std::chrono::microseconds>(m_lat_sum / m_lat_count)
                         : std::chrono::microseconds{0};
    }
    std::chrono::microseconds interval_max_latency() const
    {
      return std::chrono::duration_cast<std::chrono::microseconds>(m_lat_max);
    }
    // Latency over the whole run
    std::chrono::microseconds total_avg_latency() const
    {
      return m_total_count ? std::chrono::duration_cast<std::chrono::microseconds>(m_total_sum / m_total_count)
                           : std::chrono::microseconds{0};
    }
    std::chrono::microseconds total_max_latency() const
    {
      return std::chrono::duration_cast<std::chrono::microseconds>(m_total_max);
    }

    void reset_interval()
    {
      m_lat_sum = {};
      m_lat_max = {};
      m_lat_count = 0;
    }

  private:

    using clock = receive_info::clock;

    void on_message(const receive_info& rx)
    {
      const auto latency = clock::now() - rx.recv_time;
      m_lat_sum += latency;
      m_lat_max = std::max(m_lat_max, latency);
      ++m_lat_count;
      m_total_sum += latency;
      m_total_max = std::max(m_total_max, latency);
      ++m_total_count;

      if (rx.connection_epoch != m_epoch)
      {
        if (m_epoch != 0) ++m_reconnects;
        m_epoch = rx.connection_epoch;
      }
    }

    // A handful of symbols: a linear search is fine
    symbol_stats* find(std::string_view sym)
    {
      for (auto& s : m_symbols)
      {
        if (s.name == sym) return &s;
      }
      ++m_unknown_symbol;
      return nullptr;
    }

    std::vector<symbol_stats> m_symbols;
    std::uint64_t m_epoch = 0;
    std::uint64_t m_reconnects = 0;
    std::uint64_t m_update_before_snapshot = 0;
    std::uint64_t m_unknown_symbol = 0;

    clock::duration m_lat_sum{};
    clock::duration m_lat_max{};
    std::uint64_t m_lat_count = 0;
    clock::duration m_total_sum{};
    clock::duration m_total_max{};
    std::uint64_t m_total_count = 0;
};
