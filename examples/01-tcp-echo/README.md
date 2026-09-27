# TCP Echo Example

A TCP echo server and client in one process. The program starts the library's
`TcpServer` on an ephemeral port on `127.0.0.1`, connects a client `Socket` to it,
sends 1 MiB of patterned data, reads the echo back and checks that it matches. It
needs no other tools and exits with status 0 on success, 1 on failure.

## Building

From the repository root:

```bash
cmake -S . -B build -DSOCKETSHPP_BUILD_EXAMPLES=ON
cmake --build build --target tcp-echo
```

## Running

```bash
./build/examples/01-tcp-echo/tcp-echo
```

## What it demonstrates

- `net::tcp::TcpServer(0, "127.0.0.1")` (from `<SocketsHpp/net/tcp/tcp.h>`, which
  `sockets.hpp` does not include): port 0 picks a free port, `address()` reports it,
  and without an `onMessage()` handler the server echoes every chunk it receives.
  `Start()` runs the reactor thread; `Stop()` closes everything.
- A client `Socket` from `SocketParams{AF_INET, SOCK_STREAM, 0}`, with the result of
  `connect()` checked
- `Socket::writeall()` (loops over `send()` until everything is sent or an error
  occurs) on a second thread while the main thread reads the echo with a `recv()` loop.
  Sending everything before reading could deadlock once both sides' socket buffers are
  full.
- TCP is a byte stream: the echo arrives in chunks of arbitrary size, so the client
  reads until it has as many bytes as it sent.

## Expected output

```
Echo server listening on 127.0.0.1:41933
Sent 1048576 bytes, received 1048576 bytes
OK: echo matches
```

The port differs from run to run.
