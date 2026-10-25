# Connection Layer Unit Tests

| Suite | Tests | Covers |
|---|---|---|
| `subscription_manager_test` | 4 | Formatting, empty/updated channel lists, null formatter |
| `reconnect_policy_test` | 13 | Immediate first retry, exponential schedule and cap, jitter bounds, determinism with a fixed seed, `max_attempts`, stable vs flapping reset, config sanitising |
| `session_state_machine_test` | 17 | Connect/subscribe, retry until connected, give up, reconnect and resubscribe, `stop()` from each state, message queuing, `connection_epoch`, `recv_time` |
| `message_source_deque_test` | 9 | FIFO order, empty/refill, metadata round-trip, single-producer/single-consumer order, multi-consumer no loss |

`session_state_machine_test` drives the state machine through a mock `ws_session_interface` on a real `io_context`, using millisecond backoff delays. No network is needed.

Tests use `CHECK()` from `tests/unit/check.hpp` rather than `assert()`, so they still run in Release builds.

## Build and run

From the project root:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# single suite, with per-test PASSED/FAILED output
./build/session_state_machine_test
```
