# Common

Types shared between layers.

## `market_types.hpp`

Venue-independent building blocks (namespace `mb`), used by the venue message types and later the canonical messages.

| Type | Contents |
|---|---|
| `mb::fixed` | `int64_t` fixed point with 8 decimals (`fixed_decimals = 8`, `fixed_scale = 1e8`): `65001.5` → `6500150000000`. Exact, unlike `double` |
| `mb::symbol` | Fixed 16-char buffer + length, so copying never allocates. `view()` gives a `string_view` |
| `mb::level` | `price`, `qty` (both `fixed`). `qty == 0` means remove the level |

## `kraken_messages.hpp`

Kraken venue-native book types, produced by decode and consumed by the layers after it. Kraken-specific because of `checksum` (CRC32 of the top 10 levels); other venues use sequence numbers instead.

| Type | Contents |
|---|---|
| `kraken::book_snapshot` | `sym`, `bids`, `asks`, `checksum`, `rx` |
| `kraken::book_update` | `sym`, `bids`, `asks`, `checksum`, `timestamp_ns` (ns since the Unix epoch, UTC), `rx` |

## `received_message.hpp`

| Type | Contents |
|---|---|
| `received_message` | Raw payload (padded for simdjson), `recv_time`, `connection_epoch`. Produced by connection, consumed by decode |
| `receive_info` | Just `recv_time` + `connection_epoch`; decoders attach it to every message as `rx` |
