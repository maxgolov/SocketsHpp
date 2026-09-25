# Feature Overview

What SocketsHpp implements today, what it deliberately leaves out, and the limits to
keep in mind. For usage see the [README](../README.md); for MCP details see
[MCP_IMPLEMENTATION.md](MCP_IMPLEMENTATION.md).

## Sockets and event loop

- `net::utils::Socket`, `SocketAddr`, `SocketParams`, `ScopedSocket`: BSD sockets and
  WinSock behind one API; TCP and UDP over IPv4/IPv6; Unix domain sockets on POSIX and
  on Windows SDKs that provide `<afunix.h>`.
- `net::utils::Reactor`: one event thread per server using epoll (Linux), kqueue
  (macOS) or `WSAEventSelect` + `WSAWaitForMultipleEvents` (Windows, at most 64 sockets
  per reactor).
- `net::common::SocketServer` (TCP, UDP and Unix domain, callback based) and
  `net::tcp::TcpServer` (message handler returning the reply).
- `net::server::ThreadPoolServer`: a small wrapper around `BS::thread_pool`.

## HTTP server (`http::server::HttpServer`)

Protocol handling:

- HTTP/1.1 and HTTP/1.0 requests; keep-alive (HTTP/1.1 default, `Connection` honoured)
  and pipelined requests on one connection, answered in order.
- Any method token is passed to handlers; `HEAD` is served as `GET` without a body.
- Request bodies with `Content-Length` or `Transfer-Encoding: chunked` (chunk
  extensions and trailer fields are accepted and discarded); `Expect: 100-continue`.
- Responses with `Content-Length`, or chunked streaming via `send_chunk_stream()`;
  1xx/204/304 responses never carry a body.
- Strict parsing to avoid request smuggling: `Transfer-Encoding` together with
  `Content-Length`, repeated `Content-Length` or `Host`, invalid lengths, unknown
  transfer codings (501), `Transfer-Encoding` on HTTP/1.0, control characters in the
  request line or header values, and malformed chunk framing are all rejected.
- Limits: request line URI 8 KB, header section 8 KB and body 2 MB by default
  (`setRequestLimits()`; 431 / 413), header names 256 bytes, header values 8 KB, at
  most 100 query parameters.
- Error responses for malformed requests (400, 413, 417, 431, 501, 505) are generated
  by the server before any handler runs.

Application features:

- Prefix routing with longest-match precedence and fall-through (see the README's
  routing rules), owned (`route()`) or referenced (`addHandler()`) callbacks.
- `HttpRequest` helpers: case-insensitive `get_header_value()` / `has_header()`,
  URL-decoding `parse_query()`, `Accept` parsing with q-values (`accepts()`,
  `get_accepted_types()`).
- CORS headers and preflight (`enableCors()`, `setCorsOrigin()`, `setCorsHeaders()`).
- Session manager with timeouts and optional event history (used by the MCP server).
- Server-Sent Events: `SSEEvent` formatting (CR/LF-safe fields, multi-line data),
  automatic `Cache-Control: no-cache` and `X-Accel-Buffering: no`.
- Optional worker pool (`enableThreadPool()`) so handlers and stream callbacks do not
  block the reactor.
- `HttpFileServer`: static files from a document root with percent-decoding,
  path-traversal and symlink containment checks, `index.html` for extension-less paths
  and MIME types by extension. Files are read fully into memory.

Server helpers (opt-in headers, applied from your handlers):

- `authentication.h`: `BearerTokenAuth`, `ApiKeyAuth`, `BasicAuth`, and
  `AuthenticationMiddleware` (tries strategies in order, sets 401 body and
  `WWW-Authenticate` challenges).
- `compression.h`: `CompressionRegistry`, `CompressionStrategy` and
  `CompressionMiddleware` (`Accept-Encoding` q-value negotiation, content-type and
  minimum-size filters, bounded request decompression). **No codec is bundled**:
  register your own (for example zlib for `gzip`):

  ```cpp
  #include <SocketsHpp/http/server/compression.h>
  #include <memory>

  using namespace SocketsHpp::http::server;

  std::vector<uint8_t> myGzip(const std::vector<uint8_t>& in, int level);
  std::vector<uint8_t> myGunzip(const std::vector<uint8_t>& in);

  void registerGzip()
  {
      CompressionRegistry::instance().registerStrategy(
          std::make_shared<CompressionStrategy>("gzip", myGzip, myGunzip));
  }
  ```

  Test codecs (`rle`, `identity`) are in `compression_simple.h`; Windows-only
  `mszip`/`xpress`/`lzms` via the Windows Compression API are in
  `compression_windows.h` (not standard HTTP content-codings).
- `proxy_aware.h`: `TrustProxyConfig` (none / all / specific proxies) and
  `ProxyAwareHelpers` for the real client IP, scheme and host from `X-Forwarded-*`,
  `X-Real-IP` and RFC 7239 `Forwarded`, honoured only from trusted peers.

## HTTP clients (`http::client`)

- `HttpClient`: `get()`, `post()` and `send()` for any method; connect and read
  timeouts; redirect following (301/302/303/307/308, method rewriting, credentials
  stripped across origins); chunked, `Content-Length` and read-until-close bodies;
  streaming via `chunkCallback`; response size cap; `cancel()` from another thread;
  bracketed IPv6 hosts.
- `SSEClient`: WHATWG-conformant SSE parsing (`SSEParser`), extra request headers,
  `Last-Event-ID` tracking, auto-reconnect with server `retry:` and exponential
  backoff, thread-safe `close()`.
- `http::common::UrlParser` for `scheme://host:port/path?query` (IPv6 aware).

## MCP and JSON-RPC

- JSON-RPC 2.0 requests, notifications, responses, batches and standard error codes;
  ids as string, 64-bit integer or null.
- `mcp::server::MCPServer`: MCP 2024-11-05 (HTTP + SSE) and 2025-03-26 (Streamable
  HTTP) over HTTP, plus `processMessage()` for STDIO; version negotiation, sessions,
  server push (`push_event`, `push_log`, `push_progress`), resumability with
  `Last-Event-ID`, cancellation, `logging/setLevel`, Bearer / API key / capability-token
  auth (JWT with jwt-cpp), per-client rate limiting, CORS, `/health`, and a
  loopback-only bind guard.
- `mcp::client::MCPClient`: HTTP and Streamable HTTP transports, tools / prompts /
  resources helpers, notification handlers over SSE. No STDIO client.

## Utilities

- `utils::Base64` (RFC 4648, strict decoding) - see
  [include/SocketsHpp/utils/README.md](../include/SocketsHpp/utils/README.md).
- Logging hooks: `LOG_*` macros compiled out by default, `HAVE_CONSOLE_LOG` for stdout.

## Not implemented

| Feature | Notes |
|---------|-------|
| TLS / HTTPS | Neither server nor client. Terminate TLS in a reverse proxy; the client refuses `https://` URLs. |
| HTTP/2, HTTP/3, WebSocket | HTTP/1.x only. SSE covers server push. |
| Client connection reuse | Every `HttpClient` request opens and closes its own connection. |
| Built-in compression codecs | Framework only; bring zlib/brotli/zstd or compress in the proxy. |
| Caching / conditional requests | No `ETag`, `If-None-Match`, `If-Modified-Since` handling. |
| Range requests | No `206 Partial Content`. |
| Multipart / form parsing | Bodies are delivered raw in `HttpRequest::content`. |
| Global middleware chain | Auth, compression and proxy helpers are called from handlers. |
| Connection timeouts on the server | Idle or slow connections are not timed out by the server; use a reverse proxy for slowloris protection. |
| Host header validation | Duplicate `Host` is rejected, but the value is not checked. |

## Limits and deployment advice

- One reactor thread per server. Without `enableThreadPool()`, a slow handler or a
  blocking stream callback stalls all connections of that server.
- On Windows a reactor watches at most 64 sockets.
- Request bodies are buffered in memory (bounded by `setRequestLimits()`), as are
  static files served by `HttpFileServer`.
- For internet-facing services, run behind nginx, Caddy or HAProxy for TLS, timeouts,
  compression and load balancing, and use `TrustProxyConfig` so the application still
  sees the real client:

  ```nginx
  location / {
      proxy_pass http://127.0.0.1:8080;
      proxy_http_version 1.1;
      proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
      proxy_set_header X-Forwarded-Proto $scheme;
      proxy_buffering off;   # needed for SSE
  }
  ```

## Tests

The suite has 500+ GoogleTest cases (unit and functional) plus header
self-containment and header-only link checks; see [test/README.md](../test/README.md).
CI runs it on Ubuntu (GCC, Clang, GCC with ASan/UBSan, Clang with jwt-cpp), macOS,
Windows (MSVC) and MinGW-w64 under Wine.

## References

- [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110) / [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112) - HTTP semantics and HTTP/1.1
- [RFC 7239](https://www.rfc-editor.org/rfc/rfc7239) - `Forwarded` header
- [RFC 4648](https://www.rfc-editor.org/rfc/rfc4648) - Base64
- [WHATWG Server-Sent Events](https://html.spec.whatwg.org/multipage/server-sent-events.html)
- [JSON-RPC 2.0](https://www.jsonrpc.org/specification)
- [Model Context Protocol](https://modelcontextprotocol.io/)
