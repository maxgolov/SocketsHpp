# UDP Echo Client Example

A simple UDP client that sends a datagram to a server.

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Running

First, start a UDP server:

```bash
# Using netcat
nc -u -l -p 40000

# Or on Windows
ncat -u -l -p 40000
```

Then run the example:

```bash
./udp-echo
```

## What it demonstrates

- Creating a UDP socket with `SOCK_DGRAM`
- Using `SocketAddr` for addressing
- Sending datagrams with `Socket::send()`
- UDP is connectionless but `connect()` sets default destination

## Expected output

```
Sending to 127.0.0.1:40000
Sent 35 bytes: Hello from SocketsHpp UDP client!
```

The server should receive:
```
Hello from SocketsHpp UDP client!
```
