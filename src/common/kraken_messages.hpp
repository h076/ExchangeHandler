#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kraken {

// Fixed-point: price 65001.5 stored as 6500150000000 with a 1e8 scale.
// Exact, unlike double.
using fixed = std::int64_t;
inline constexpr fixed fixed_scale = 100'000'000;

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

struct book_snapshot {
    symbol sym;
    std::vector<level> bids;
    std::vector<level> asks;
    std::uint32_t checksum;
};

struct book_update {
    symbol sym;
    std::vector<level> bids;
    std::vector<level> asks;
    std::uint32_t checksum;
    std::int64_t  timestamp_ns;   // parsed from Kraken's timestamp string
};

} // namespace kraken
