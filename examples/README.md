# SocketsHpp Examples

Small programs showing how to use the library. Each directory has a `main.cpp` (or
two sources for example 10), a `CMakeLists.txt` and a README.

## Building

Build the examples from the repository root together with the library:

```bash
git submodule update --init --recursive      # nlohmann-json, needed by sockets.hpp
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --parallel               # or: --target http-server
```

Executables are placed in `build/examples/<directory>/` (on Visual Studio generators,
in `build/examples/<directory>/<Config>/`), for example
`build/examples/03-http-server/http-server`.

Each example's own `CMakeLists.txt` can also be configured directly from its directory
inside a checkout of this repository. Example 11 is not part of the top-level build: it
consumes SocketsHpp as an installed vcpkg package (see its README).

## Examples

| Directory | Target | What it does |
|-----------|--------|--------------|
| [01-tcp-echo](01-tcp-echo/) | `tcp-echo` | TCP echo round trip in one process: `TcpServer` on an ephemeral loopback port; a client sends 1 MiB and verifies the echo |
| [02-udp-echo](02-udp-echo/) | `udp-echo` | UDP echo round trip in one process: `SocketServer` on a datagram socket with an echoing `onRequest`; a client verifies each reply |
| [03-http-server](03-http-server/) | `http-server` | HTTP server: HTML, text and JSON routes, query parameters, a JSON POST route, prefix routing with 400/404/405 answers |
| [04-http-sse](04-http-sse/) | `http-sse` | Server-Sent Events with `send_chunk_stream()`, custom event types, a browser client, the worker pool |
| [05-mcp-server](05-mcp-server/) | `mcp-server` | `MCPServer` over Streamable HTTP with an `echo` and a cancellable `wait` tool; curl walkthrough of initialize, tools/list, tools/call, cancellation and DELETE |
| [06-proxy-aware](06-proxy-aware/) | `proxy-aware-server` | Real client IP/scheme/host behind a reverse proxy with `TrustProxyConfig` and `ProxyAwareHelpers`; HTML-escaped page and a JSON route |
| [07-authentication](07-authentication/) | `authenticated-api` | `AuthenticationMiddleware` with Bearer, Basic and API-key strategies, 401 challenges and a 403 role check |
| [08-compression](08-compression/) | `compression-server` | `CompressionMiddleware` compressing responses with the toy `rle` codec; `Content-Encoding` and `Vary` |
| [09-full-featured](09-full-featured/) | `full-featured-server` | Notes API combining the thread pool, CORS, auth middleware, proxy awareness, compression and JSON GET/POST routes |
| [10-typescript-interop](10-typescript-interop/) | `cpp_server`, `cpp_client` | `MCPServer`/`MCPClient` interop in both directions with the official MCP TypeScript SDK (run in CI) |
| [11-vcpkg-consumption](11-vcpkg-consumption/) | `vcpkg-consumer` | A separate project that gets SocketsHpp through the vcpkg port (overlay or git registry) and uses `find_package(SocketsHpp)` |

Examples 03, 04 and 06-09 use `HttpServer(name, port)`, which listens on **all IPv4
interfaces** (the name only appears in the `Server` response header, so
`HttpServer("localhost", 8080)` is not loopback-only); 05, 10 and 11 bind 127.0.0.1.
Use `addListeningPort("127.0.0.1", port)` in your own code to bind one address. The
HTTP servers take an optional port argument (default 8080) and stop cleanly on Ctrl+C.

For the MCP API in depth, see [docs/MCP_IMPLEMENTATION.md](../docs/MCP_IMPLEMENTATION.md).

## Requirements

- C++17 compiler and CMake 3.14+ (example 11: 3.15)
- The dependencies listed in the [main README](../README.md#dependencies): the bundled
  `external/BS_thread_pool.hpp`, and nlohmann/json (every example includes
  `sockets.hpp`)
- `curl` or a browser to try them out; Node.js 18+ for example 10

## Adding an example

1. Create a numbered directory with `main.cpp`, `CMakeLists.txt` and `README.md`.
2. Add it to the `BUILD_EXAMPLES` block of the top-level `CMakeLists.txt`.
3. Add a row to the table above.

## License

Apache-2.0, like the library.
