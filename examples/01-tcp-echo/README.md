# TCP Client Example

Despite the directory name, this is a TCP **client**: it connects to
`127.0.0.1:40000`, sends 1 MB of patterned data and closes the connection.

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target tcp-echo
```

## Running

Start something that listens on port 40000 first:

```bash
nc -l 40000 > received.bin        # netcat (BSD/macOS syntax; GNU netcat: nc -l -p 40000)
ncat -l 40000 > received.bin      # Windows (nmap's ncat)
```

Then run the client:

```bash
./build/examples/01-tcp-echo/tcp-echo
```

## What it demonstrates

- Creating a TCP socket from `SocketParams{AF_INET, SOCK_STREAM, 0}`
- Connecting with `Socket::connect(SocketAddr{"127.0.0.1:40000"})`
- Sending with `Socket::send()` (a single call; it reports how many bytes the kernel
  accepted)
- Closing with `Socket::close()`

## Expected output

```
Connecting to 127.0.0.1:40000...
Connected!
Sending 1048576 bytes...
Successfully sent 1048576 bytes
Connection closed
```

The byte count on the "Successfully sent" line can be lower than 1048576 if the
kernel accepts only part of the buffer in one `send()`.
