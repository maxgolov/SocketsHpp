# UDP Echo Example

A UDP echo server and client in one process. The program binds a `SocketServer` to a
UDP socket on an ephemeral port on `127.0.0.1`, installs an `onRequest` handler that
sends each datagram back, then sends three datagrams from a client `Socket` and checks
every reply. It needs no other tools and exits with status 0 on success, 1 on failure.

## Building

From the repository root:

```bash
cmake -S . -B build -DSOCKETSHPP_BUILD_EXAMPLES=ON
cmake --build build --target udp-echo
```

## Running

```bash
./build/examples/02-udp-echo/udp-echo
```

## What it demonstrates

- `net::common::SocketServer(SocketAddr("127.0.0.1:0"), SocketParams{AF_INET, SOCK_DGRAM, 0})`:
  binds in the constructor (check `is_bound`); `address()` reports the actual port
- An `onRequest` handler: it runs on the reactor thread for each datagram, copies
  `request_buffer` to `response_buffer` and inserts `Connection::Responding`, and the
  server sends the reply to the datagram's sender. (The default handler sends nothing.)
- `connect()` on a UDP client only sets the default peer for `send()` / `recv()`
- A receive timeout (`SO_RCVTIMEO`): UDP does not guarantee delivery, so the client
  never waits for a reply forever

## Expected output

```
Echo server listening on udp://127.0.0.1:56113
echoed: Hello
echoed: from the
echoed: SocketsHpp UDP client!
OK: all datagrams echoed
```

The port differs from run to run.
