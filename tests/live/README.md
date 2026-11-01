# Live Tests

Executables that run against Kraken's public v2 WebSocket feed (`wss://ws.kraken.com/v2`, `book` channel, depth 10). They need network access and are not run by `ctest`. Built when `BUILD_LIVE_TESTS=ON` (the default).

| File | Role |
|---|---|
| `live_common.hpp` | Shared code: `-t seconds [symbol ...]` parsing, `run_kraken_book_feed()` (ws_session → session_state_machine, TLS, Ctrl-C / timeout shutdown, consumer thread), `poll()` consumer loop |
| `kraken_connection_live.cpp` | Connection layer only: raw payload counts per symbol, queue delay, last payload |
| `kraken_decode_live.cpp` | Connection + `kraken_decoder`: decode stats, per-symbol snapshot/update counts, top of book, latency. Exit code 1 on failure |
| `resync_standin_sink.hpp` | Sink standing in for the resync layer: records what the decoder delivers, checks epochs (no update before its snapshot on a connection) and unknown symbols. Keeps no book |

Each layer gets its own live test, so a failure points at one layer. The next one (resync) will replace the stand-in sink with the real stage.

## Run

```bash
cmake -S . -B build
cmake --build build -j

./build/kraken_connection_live -t 10 BTC/USD ETH/USD
./build/kraken_decode_live -t 10 BTC/USD ETH/USD     # PASS / FAILED, exit code 0 / 1
```

`kraken_decode_live` fails if any message is `malformed`, a subscribed symbol got no snapshot, an update arrived before its snapshot on a connection, or a message was for an unsubscribed symbol. The first 5 `malformed` / `unknown_type` payloads are printed.

## Notes

- Threads: the main thread runs the `io_context`; a consumer thread pops from the state machine.
- Reported latency is `recv_time` → sink (queue wait + decode). The consumer sleeps 1 ms when the queue is empty, so this is mostly that sleep; a pinned spinning consumer would remove it.
- Best bid/ask is from the last snapshot only (the stand-in keeps no book).
