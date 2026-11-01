#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

// Receive-side metadata, without the payload. Decoders copy this onto every
// message they produce so later layers know when and on which connection it
// arrived.
struct receive_info
{
  using clock = std::chrono::steady_clock;

  clock::time_point recv_time;
  std::uint64_t connection_epoch = 0;
};

// One application payload as received from the venue, plus the receive-side
// metadata the decode / resync layers need.
struct received_message
{
  using clock = receive_info::clock;

  // Spare capacity reserved after the payload. SIMD JSON parsers read past the
  // end of the input: simdjson needs capacity() >= size() + 64 or it rejects
  // the document. Reserving it here avoids a second copy in the decoder.
  static constexpr std::size_t payload_padding = 64;

  // Raw WebSocket payload (JSON text for Kraken), copied out of the read buffer.
  // capacity() >= size() + payload_padding.
  std::string payload;

  // When the connection layer received it, for latency measurement
  clock::time_point recv_time;

  // Which connection it arrived on: starts at 1 and increments on every
  // successful connect. A change tells decode / resync that the previous
  // connection's state is gone and a fresh snapshot should follow.
  std::uint64_t connection_epoch = 0;

  receive_info info() const { return {recv_time, connection_epoch}; }
};
