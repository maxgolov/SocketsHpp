# TCP Echo Client Example

A simple TCP client that connects to a server and sends 1MB of data.

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Running

First, start a TCP server to receive data:

```bash
# Using netcat
nc -l -p 40000 > received.bin

# Or on Windows
ncat -l -p 40000 > received.bin
```

Then run the example:

```bash
./tcp-echo
```

## What it demonstrates

- Creating a TCP socket with `SocketParams`
- Connecting to a server with `SocketAddr`
- Sending data with `Socket::send()`
- Proper socket cleanup with `Socket::close()`

## Expected output

```
Connecting to 127.0.0.1:40000...
Connected!
Sending 1048576 bytes...
Successfully sent 1048576 bytes
Connection closed
```
