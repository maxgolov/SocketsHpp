# Changelog

## 1.1.0

### Fixes
- The reactor no longer busy-loops at 100% CPU while a stream callback (SSE) waits on
  the thread pool with writable interest still armed (epoll/kqueue).
- Windows: a reactor stalled for every client once a 65th socket was accepted
  (`WSAWaitForMultipleEvents` limit); extra connections are now refused.
- `HttpResponse::set_content(body)` keeps a `Content-Type` set earlier with
  `set_header()` instead of resetting it to `text/plain`.
- Unconsumed client input is bounded while a response is in progress.
- `getClientIP()` only returns literal IP addresses from forwarded headers.
- An unmatched `DELETE` without `Mcp-Session-Id` gets 404 instead of 400.
- `ClientConfig::fromJson()` maps VS Code's `"type": "http"` to Streamable HTTP.

### HTTP server
- Idle and request timeouts (`setIdleTimeout()`, `setRequestTimeout()`; 408 for slow
  requests), `setMaxConnections()`.
- Listening on Unix domain sockets (`addListeningUnixSocket()`), `HttpClient` over a
  Unix socket, systemd socket activation (`addInheritedListeningSockets()`,
  `adoptListeningSocket()`, `sdNotify()`).
- Graceful drain with `shutdown(drainTimeout)`.
- Metrics (`metrics()`, Prometheus `enableMetricsEndpoint()`), runtime log hook
  (`SocketsHpp::setLogHandler()`), `setCorsMethods()`, correct `WWW-Authenticate` /
  `ETag` casing, SSE headers also for `text/event-stream; charset=...`.
- `multipart/form-data` parser (`http/server/multipart.h`).

### MCP
- Protocol versions 2025-06-18 and 2025-11-25 (`MCP-Protocol-Version` header, no
  batching for new sessions) alongside 2025-03-26 and 2024-11-05.
- `Origin` validation (`allowedOrigins`), no response to cancelled requests, client
  JSON-RPC responses accepted with 202.
- Typed helpers: `registerTool()`, `registerCancellableTool()`, `registerPrompt()`,
  `registerResource()`.
- STDIO: `StdioServerTransport` for servers and a STDIO `MCPClient` that launches the
  server process (`utils/process.h`); client protocol check, capabilities, retries,
  cancellation tokens.
- `ServerConfig::unixSocketPath`, idle/request timeouts.

### Build and packaging
- CMake options are prefixed (`SOCKETSHPP_BUILD_EXAMPLES`, `SOCKETSHPP_ENABLE_ASAN`,
  `SOCKETSHPP_ENABLE_UBSAN`); the old names still work in top-level builds.
- The vcpkg port builds a pinned release from GitHub and the repository is a vcpkg
  git registry (`versions/`).
- Examples reworked (01-09), MCP STDIO example (12); docs audited against the code;
  `docs/DESIGN.md`.

## 1.0.0

Initial versioned release.
