# UDP Client Example

Despite the directory name, this is a UDP **client**: it sends one datagram,
`Hello from SocketsHpp UDP client!`, to `127.0.0.1:40000`.

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target udp-echo
```

## Running

Start a UDP listener first:

```bash
nc -u -l 40000        # netcat (BSD/macOS syntax; GNU netcat: nc -u -l -p 40000)
ncat -u -l 40000      # Windows (nmap's ncat)
```

Then run the client:

```bash
./build/examples/02-udp-echo/udp-echo
```

## What it demonstrates

- Creating a UDP socket from `SocketParams{AF_INET, SOCK_DGRAM, 0}`
- Addressing with `SocketAddr("127.0.0.1:40000")` and `SocketAddr::toString()`
- `connect()` on a UDP socket only sets the default destination for `send()`
- Sending the datagram with `Socket::send()`

## Expected output

```
Sending to 127.0.0.1:40000
Sent 33 bytes: Hello from SocketsHpp UDP client!
```

The listener prints:

```
Hello from SocketsHpp UDP client!
```
