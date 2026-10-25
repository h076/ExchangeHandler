# Decode Layer (Layer Two)

Parses raw venue payloads (JSON) into typed venue-native messages (`src/common/kraken_messages.hpp`) using simdjson on-demand, then hands each one to the next layer's sink.

## Components

| File | Role |
|---|---|
| `decoder.hpp` | Venue-independent base: `decode(payload)` → `decode_status` (`ok`, `ignored`, `malformed`, `unknown_type`) and running `decode_stats` |
| `kraken_decoder.hpp` / `.tpp` | Kraken v2 decoder: routes by `channel`, decodes `book` snapshots and updates. `heartbeat` → `ignored` |
| `kraken_parse.hpp` | Field parsers: exact decimal text → `fixed`, RFC 3339 timestamp → ns, symbol, level arrays |

## Sink (C++20 concept)

The decoder is templated on the next stage, constrained by a concept:

```cpp
template <typename S>
concept kraken_sink = requires(S s, const kraken::book_snapshot& snap, const kraken::book_update& upd)
{
  s.on_book_snapshot(snap);
  s.on_book_update(upd);
};

template <kraken_sink Sink>
class kraken_decoder final : public decoder { ... Sink& m_next; };
```

- **What counts as a sink:** any type with those two methods. It doesn't need a base class, and calls aren't virtual, so they can be inlined. A type that's missing a method fails to compile with a clear concept error.
- **Ownership:** the decoder holds a **reference** to the caller's sink, so the sink must outlive the decoder.
- **Lifetime of the message:** each message is passed by `const&` to a struct the decoder reuses (no allocation once its vectors have grown). It's only valid during the call, so copy anything you keep.
- **Where the code lives:** because it's a template, the implementation is in `kraken_decoder.tpp`, included at the end of the header.

```cpp
struct book_builder {
  void on_book_snapshot(const kraken::book_snapshot& s);
  void on_book_update(const kraken::book_update& u);
};

book_builder builder;
kraken_decoder<book_builder> dec(builder);

while (auto msg = sm->pop())        // from the connection layer's queue
  dec.decode(msg->payload);
```

## Notes

- **Padding:** simdjson needs `payload.capacity() >= size() + 64`. The connection layer reserves this (`received_message::payload_padding`). Payloads built any other way must do the same, or they decode as `malformed`.
- **Exact numbers:** prices and quantities are parsed from the raw number text, never through `double`. More than 8 decimals, or overflow, gives `malformed`.
- **Threading:** a decoder is not thread safe. Use one per decode thread.
- **Not handled yet:**
  - Subscribe acks/errors (`{"method": ...}`, no `channel`) are counted as `malformed`.
  - The `status` channel is counted as `unknown_type`.
  - The snapshot timestamp is dropped, because `book_snapshot` has no field for it.

## Tests

`tests/unit/decode/`: `kraken_decoder_test` (routing, snapshot/update decoding, malformed input) and `kraken_parse_test` (field parsers).
