# Connection Layer (Layer One)

This layer will handle the following:
- WSS handshake
- TLS
- auth
- ping/pong
- disconnect
- reconnect with backoff
- resubscribe

## Contents 

### WSS Session Interface

An abstract class that defines:
- connect()
- disconnect()
- send()
- on_message()

### WSS Session

This is a concrete implementation of the WSS session interface.

This class will touch sockets, handle upgrades, TLS, and WS framing

### Reconnection Policy

Pure logic that will compute backoff delay. 

### Auth Manager

Builds auth payloads for the venue if required

### Subscription Manager

Holds a list of what we are subscribing to, resending the subscriptions on reconnect

### Heartbeat Monitor

Listens for Ping/Pong or app-level heartbeats while live. 

Ensures that the connection does not go stale.

### Session State Machine

This object encapsulates all other members that have been described.

It drives state transitions, and decides what to do on each event. Containing decision logic while delegating work to helpers
