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
| [01-tcp-echo](01-tcp-echo/) | `tcp-echo` | TCP **client**: connects to `127.0.0.1:40000` and sends 1 MB |
| [02-udp-echo](02-udp-echo/) | `udp-echo` | UDP **client**: sends one datagram to `127.0.0.1:40000` |
| [03-http-server](03-http-server/) | `http-server` | HTTP server on port 8080: HTML, text and JSON routes, query parameters, a POST route |
| [04-http-sse](04-http-sse/) | `http-sse` | Server-Sent Events with `send_chunk_stream()`, custom event types, a browser client, the worker pool |
| [05-mcp-server](05-mcp-server/) | `mcp-server` | MCP-*style* HTTP+SSE transport written by hand on `HttpServer` (CORS headers, a demo SSE stream, Base64); it does **not** use `MCPServer` |
| [06-proxy-aware](06-proxy-aware/) | `proxy-aware-server` | Real client IP/scheme/host behind a reverse proxy with `TrustProxyConfig` and `ProxyAwareHelpers` |
| [07-authentication](07-authentication/) | `authenticated-api` | Bearer token and API key checks done inside route handlers |
| [08-compression](08-compression/) | `compression-server` | Serves a large HTML page; compression is **not** applied (the README shows how to add it) |
| [09-full-featured](09-full-featured/) | `full-featured-server` | Proxy awareness plus Bearer/API-key authentication |
| [10-typescript-interop](10-typescript-interop/) | `cpp_server`, `cpp_client` | JSON-RPC (MCP-style) interop between C++ and TypeScript clients/servers |
| [11-vcpkg-consumption](11-vcpkg-consumption/) | `vcpkg-consumer` | A separate project using the vcpkg overlay port and `find_package(SocketsHpp)` |

The HTTP servers use `HttpServer(name, port)`, which listens on **all IPv4
interfaces** (the name only appears in the `Server` response header, so
`HttpServer("localhost", 8080)` is not loopback-only). They run until you press Ctrl+C.
Use `addListeningPort("127.0.0.1", port)` in your own code to bind one address.

For a real MCP server and client, see `SocketsHpp::mcp::server::MCPServer` and
`SocketsHpp::mcp::client::MCPClient` in [docs/MCP_IMPLEMENTATION.md](../docs/MCP_IMPLEMENTATION.md).

## Requirements

- C++17 compiler and CMake 3.14+ (examples 06-09 declare 3.20, example 10 3.21)
- The dependencies listed in the [main README](../README.md#dependencies): the bundled
  `external/BS_thread_pool.hpp`, and nlohmann/json (every example includes
  `sockets.hpp`)
- `curl`, `nc` (netcat) or a browser to try them out; Node.js 18+ for example 10

## Adding an example

1. Create a numbered directory with `main.cpp`, `CMakeLists.txt` and `README.md`.
2. Add it to the `BUILD_EXAMPLES` block of the top-level `CMakeLists.txt`.
3. Add a row to the table above.

## License

Apache-2.0, like the library.
