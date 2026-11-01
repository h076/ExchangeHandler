#pragma once

// Kraken v2 venue-native book messages, produced by the Kraken decoder.
// Kraken-specific because of the checksum (CRC32 of the top 10 levels), which
// resync uses instead of sequence numbers.

#include <cstdint>
#include <vector>

#include "market_types.hpp"
#include "received_message.hpp"

namespace kraken {

struct book_snapshot {
    mb::symbol sym;
    std::vector<mb::level> bids;
    std::vector<mb::level> asks;
    std::uint32_t checksum;
    receive_info rx;              // set by the decoder from the received_message
};

struct book_update {
    mb::symbol sym;
    std::vector<mb::level> bids;
    std::vector<mb::level> asks;
    std::uint32_t checksum;
    std::int64_t  timestamp_ns;   // parsed from Kraken's timestamp string
    receive_info rx;              // set by the decoder from the received_message
};

} // namespace kraken
