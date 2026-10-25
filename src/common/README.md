# Common

Types shared between layers.

## `kraken_messages.hpp`

Internal Kraken book types, produced by decode and consumed by the layers after it.

| Type | Contents |
|---|---|
| `kraken::fixed` | `int64_t` fixed point with 8 decimals (`fixed_scale = 1e8`): `65001.5` → `6500150000000`. Exact, unlike `double` |
| `kraken::symbol` | Fixed 16-char buffer + length, so copying never allocates. `view()` gives a `string_view` |
| `kraken::level` | `price`, `qty` (both `fixed`). `qty == 0` means remove the level |
| `kraken::book_snapshot` | `sym`, `bids`, `asks`, `checksum` |
| `kraken::book_update` | `sym`, `bids`, `asks`, `checksum`, `timestamp_ns` (ns since the Unix epoch, UTC) |
