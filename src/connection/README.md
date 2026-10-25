# Connection Layer (Layer One)

Maintains a live WebSocket feed to a venue: TLS, the WebSocket handshake, keep-alive pings, subscriptions, and reconnecting with backoff. Every received payload goes into a queue for the decode layer.

## Components

| File | Role |
|---|---|
| `ws_session_interface.hpp` | Abstract session: `connect`, `disconnect`, `send`, message and state handlers |
| `ws_session` | Beast/Asio implementation: resolve → TCP → TLS (SNI) → WS upgrade, then read/write loops. Builds a fresh stream on every `connect()` |
| `session_state_machine<Queue>` | Drives the session: subscribes on connect, reconnects on errors, pushes payloads into itself (it *is* the queue) |
| `reconnect_policy` | Pure backoff logic, no timers: returns the next delay or gives up |
| `subscription_manager` | Channel list + venue-specific formatter; produces the subscribe messages sent on every connect |
| `received_message.hpp` | Queued unit: `payload`, `recv_time`, `connection_epoch` |
| `message_source_interface.hpp` | Queue contract: `push(received_message)` / `pop()` |
| `message_source_deque` | Mutex-guarded `std::deque` implementation of the queue |
| `kraken_test.cpp` | Live smoke test against Kraken v2 `book` (`kraken_test [-t seconds] [symbol ...]`) |

## Session state machine

| State | Meaning |
|---|---|
| `idle` | Constructed, `run()` not called |
| `connecting` | `connect()` issued |
| `live` | Connected, subscriptions sent |
| `backing_off` | Waiting on the retry timer |
| `stopping` | `stop()` called, waiting for the session to close |
| `stopped` | Closed on request |
| `failed` | Reconnect policy gave up (`max_attempts` reached) |

- **Error while `connecting` or `live`:** asks the policy for a delay, goes to `backing_off`, then `connecting` when the timer fires. If the policy gives up, goes to `failed`.
- **Error while `stopping`:** treated as a stop (`stopped`); never reconnects.
- **Connected:** goes to `live`, increments `connection_epoch` and resends all subscriptions.
- **`stop()`:** works from any state. It cancels a pending retry or closes the live session.

## Reconnect policy defaults

The first retry is immediate. After that, delays are 250 ms × 2ⁿ, capped at 30 s, with 0.5 jitter (each delay is somewhere between half and all of its value). `max_attempts = 0` means retry forever. A connection that stayed up for ≥ 30 s resets the backoff, so a connection that keeps dropping straight away still backs off.

## Usage

```cpp
auto strand  = net::make_strand(ioc);
auto session = std::make_shared<ws_session>(strand, connection_config{host, 443, "/v2", std::move(tls)});
auto sm      = std::make_shared<session_state_machine<message_source_deque>>(strand, std::move(subs));

net::post(strand, [&] { sm->run(session); });

// decode thread
while (auto msg = sm->pop()) { /* msg->payload, msg->recv_time, msg->connection_epoch */ }
```

## Threading

- **The strand:** the session, the state machine's state, the timer and the policy all live on one strand. Call `run()` and `stop()` through it.
- **The queue:** `push()` (io strand) and `pop()` (decode thread) only touch the queue. They're as thread safe as `Queue`: the deque is, a future lock-free queue must be for one producer and one consumer.
- **Copying:** payloads are copied out of Beast's read buffer, because the span is only valid during the callback.

## Adding a queue type

`Queue` must derive from `message_source_interface` and be default constructible. The state machine is explicitly instantiated in `session_state_machine.cpp`, so add a line there for each new type:

```cpp
template class session_state_machine<my_lock_free_queue>;
```

## Not yet implemented

- **Auth manager:** not needed for public feeds.
- **App-level heartbeat monitor:** WS pings detect a dead socket, not a feed that has gone quiet.
- **Subscribe ack/error handling:** a rejected subscription still shows as `live`.
- **Queue limits:** the queue has no size limit, and `push()` can't report that it's full.
- **Notification on `failed`:** the only way to see it today is `state()`.
- **Control messages** (heartbeat, status, acks) currently go to the queue alongside data. They'll be classified by decode.
