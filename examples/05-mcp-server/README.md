# MCP-Style SSE Transport Example

A hand-written, **simplified** imitation of an MCP HTTP+SSE transport, built directly
on `HttpServer` routes. It does not use the library's MCP implementation and does not
process JSON-RPC requests: the SSE stream plays back canned messages.

For a working MCP server (JSON-RPC dispatch, sessions, Streamable HTTP, auth, ...) use
`SocketsHpp::mcp::server::MCPServer`; see
[docs/MCP_IMPLEMENTATION.md](../../docs/MCP_IMPLEMENTATION.md) and the MCP example in
the [main README](../../README.md#mcp-server).

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target mcp-server
```

## Running

```bash
./build/examples/05-mcp-server/mcp-server
```

The server listens on port 8080 on all IPv4 interfaces.

## Routes

| Route | Behaviour |
|-------|-----------|
| `GET /sse` | SSE stream: an `initialized`-style message (id 1), a `tools/list`-style message (id 2), then five `ping` events two seconds apart, then the stream ends. Each connection gets a session id `session-<unix time>` in a global map. |
| `OPTIONS /sse` | CORS preflight answered by the handler itself (204 with `Access-Control-Allow-*` headers) |
| `DELETE /session` | Removes the fixed id `demo-session` from the map and returns 204 (the session id is not read from the request); `OPTIONS` also gets 204, other methods 405 |
| `GET /info` | Static JSON metadata |
| `POST /base64` | Returns the request body and its Base64 encoding as JSON |
| `/` (any other path) | 404 |

```bash
curl -N http://localhost:8080/sse
curl -X DELETE http://localhost:8080/session
curl http://localhost:8080/info
curl -X POST http://localhost:8080/base64 -d "Hello, MCP!"
# {"original":"Hello, MCP!","encoded":"SGVsbG8sIE1DUCE="}
```

## What it demonstrates

- SSE streaming with `send_chunk_stream()` and `SSEEvent` (multi-line `data` is split
  into several `data:` lines)
- `enableThreadPool(4)`, so the pauses between pings do not block other requests
- Setting CORS headers by hand in handlers (the server also has built-in CORS support:
  `enableCors()`, `setCorsOrigin()`, `setCorsHeaders()`)
- Branching on `req.method` inside one route
- `SocketsHpp::utils::base64::encode()`

## Expected output (curl -N /sse)

```
id: 1
event: message
data: {
data:                 "jsonrpc": "2.0",
data:                 "method": "initialized",
...

id: 2
event: message
data: {
data:                 "jsonrpc": "2.0",
data:                 "method": "tools/list",
...

event: ping
data: 1790323647
...
```
