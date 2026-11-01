#pragma once

// Venue-independent building blocks shared by all layers: fixed-point numbers,
// symbols and price levels. Venue message types (e.g. kraken_messages.hpp) and
// later the canonical messages are built from these.

#include <array>
#include <cstdint>
#include <string_view>

namespace mb {

// Fixed-point: price 65001.5 stored as 6500150000000 with a 1e8 scale.
// Exact, unlike double.
using fixed = std::int64_t;
inline constexpr int   fixed_decimals = 8;
inline constexpr fixed fixed_scale    = 100'000'000;
static_assert(fixed_scale == 100'000'000 && fixed_decimals == 8,
              "fixed_decimals must match fixed_scale");

// Fixed-size symbol like "BTC/USD", so copying it never allocates
struct symbol {
    std::array<char, 16> chars{};
    std::uint8_t len = 0;
    std::string_view view() const { return {chars.data(), len}; }
};

struct level {
    fixed price;
    fixed qty;        // 0 means "remove this price level"
};

} // namespace mb
