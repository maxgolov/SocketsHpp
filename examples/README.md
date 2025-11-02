# SocketsHpp Examples

Standalone examples demonstrating SocketsHpp features. Each example is self-contained with its own CMakeLists.txt and can be built independently.

## Available Examples

### [01-tcp-echo](01-tcp-echo/)
Simple TCP client that sends 1MB of data to a server.

**Demonstrates:**
- Creating TCP sockets
- Connecting to servers
- Sending data
- Socket cleanup

**Run:**
```bash
cd 01-tcp-echo && mkdir build && cd build
cmake .. && cmake --build .
./tcp-echo  # or tcp-echo.exe on Windows
```

---

### [02-udp-echo](02-udp-echo/)
UDP client that sends a datagram to a server.

**Demonstrates:**
- Creating UDP sockets
- Datagram transmission
- Address configuration
- Connectionless communication

**Run:**
```bash
cd 02-udp-echo && mkdir build && cd build
cmake .. && cmake --build .
./udp-echo
```

---

### [03-http-server](03-http-server/)
HTTP/1.1 server with routing, query parameters, and JSON responses.

**Demonstrates:**
- HTTP server creation
- Route handlers
- Query parameter parsing
- Multiple content types (HTML, JSON, plain text)
- Different HTTP methods (GET, POST)

**Run:**
```bash
cd 03-http-server && mkdir build && cd build
cmake .. && cmake --build .
./http-server
# Visit http://localhost:8080
```

---

### [04-http-sse](04-http-sse/)
Server-Sent Events (SSE) for real-time streaming.

**Demonstrates:**
- SSE event streams
- `text/event-stream` content type
- Event IDs and custom event types
- JSON data in events
- Browser client integration
- Chunked responses

**Run:**
```bash
cd 04-http-sse && mkdir build && cd build
cmake .. && cmake --build .
./http-sse
# Visit http://localhost:8080 or curl -N http://localhost:8080/events
```

---

### [05-mcp-server](05-mcp-server/)
Model Context Protocol (MCP) server with HTTP+SSE transport.

**Demonstrates:**
- MCP transport layer
- CORS configuration
- Session management
- DELETE/OPTIONS methods
- Base64 encoding
- JSON-RPC message format (simplified)
- SSE for bidirectional communication

**Run:**
```bash
cd 05-mcp-server && mkdir build && cd build
cmake .. && cmake --build .
./mcp-server
# curl -N http://localhost:8080/sse
```

---

## Building All Examples

To build all examples at once:

```bash
# From the examples directory
for dir in */; do
    cd "$dir"
    mkdir -p build && cd build
    cmake .. && cmake --build .
    cd ../..
done
```

Or on Windows PowerShell:

```powershell
Get-ChildItem -Directory | ForEach-Object {
    cd $_.FullName
    if (!(Test-Path build)) { mkdir build }
    cd build
    cmake ..
    cmake --build .
    cd ../..
}
```

## Requirements

- C++17 compatible compiler
- CMake 3.14 or newer
- Windows: MSVC, MinGW, or Clang
- Linux: GCC 7+, Clang 5+
- macOS: Xcode Command Line Tools

## Platform-Specific Notes

### Windows
The examples automatically link `ws2_32.lib` for Winsock2.

### Linux/macOS
No special libraries needed - standard POSIX sockets are used.

### ARM64
All examples build successfully for ARM64 (tested via cross-compilation).

## Example Structure

Each example follows this structure:

```
XX-example-name/
├── main.cpp         # Source code
├── CMakeLists.txt   # Build configuration
└── README.md        # Documentation
```

Examples are designed to be:
- **Standalone** - Build and run independently
- **Educational** - Clear comments and documentation
- **Practical** - Real-world usage patterns
- **Cross-platform** - Windows, Linux, macOS, ARM64

## Testing

Each example includes testing instructions in its README.md. Most examples can be tested with:

- `netcat` (nc) for TCP/UDP servers
- `curl` for HTTP endpoints
- Web browsers for SSE and HTML
- MCP clients for the MCP server

## Additional Resources

- [Main README](../README.md) - Library overview
- [Features Documentation](../docs/FEATURES.md) - Implementation status
- [Test Documentation](../test/README.md) - Test suite information
- [MCP Specification](https://spec.modelcontextprotocol.io/) - MCP protocol details

## Contributing

When adding new examples:

1. Create a new numbered directory (e.g., `06-new-example/`)
2. Include `main.cpp`, `CMakeLists.txt`, and `README.md`
3. Follow the existing structure and style
4. Test on Windows and Linux
5. Update this main README.md
6. Ensure examples are self-contained (no external dependencies except SocketsHpp)

## License

All examples are licensed under Apache-2.0, same as SocketsHpp library.

Copyright Max Golovanov. SPDX-License-Identifier: Apache-2.0
