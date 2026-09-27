# MCP Server Example

A small but complete [Model Context Protocol](https://modelcontextprotocol.io) server
built with `SocketsHpp::mcp::server::MCPServer`, using the Streamable HTTP transport
(MCP 2025-03-26) at `http://127.0.0.1:8080/mcp`. It offers two tools:

| Tool | Arguments | Result |
|------|-----------|--------|
| `echo` | `{"text": string}` | The text |
| `wait` | `{"seconds": 1-60}` | `Waited N s` after N seconds; can be cancelled with `notifications/cancelled` |

For the full API (sessions, notification streams, progress, auth, rate limiting, the
client) see [docs/MCP_IMPLEMENTATION.md](../../docs/MCP_IMPLEMENTATION.md); example 10
talks to the official MCP TypeScript SDK.

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target mcp-server
```

## Running

```bash
./build/examples/05-mcp-server/mcp-server          # port 8080
./build/examples/05-mcp-server/mcp-server 9000     # another port
```

`MCPServer` binds only the configured host (`127.0.0.1` here) and refuses a
non-loopback host unless `ServerConfig::allowNonLoopback` is set. Press Ctrl+C to stop.

## Talking to it with curl

Every POST needs `Content-Type: application/json` and an `Accept` header listing both
`application/json` and `text/event-stream`. Because `Accept` includes
`text/event-stream`, each response arrives as one SSE event (`data: {...}`); with
`Accept: application/json` alone the server answers with plain JSON instead.

**1. Initialize** and keep the session id from the `Mcp-Session-Id` response header:

```bash
curl -s -D headers.txt http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"curl","version":"1.0"}}}'
# data: {"id":1,"jsonrpc":"2.0","result":{"capabilities":{"tools":{}},"protocolVersion":"2025-03-26","serverInfo":{"name":"example-mcp-server","version":"1.0.0"}}}

SESSION=$(grep -i '^mcp-session-id:' headers.txt | cut -d' ' -f2 | tr -d '\r')
echo "$SESSION"
# session-1648acffc8e3571c8c54f4056a849e57
```

Every later request must carry `Mcp-Session-Id: $SESSION` (without it: 400; with an
unknown or deleted id: 404).

**2. Confirm initialization** (a notification, so the answer is `202 Accepted` with no body):

```bash
curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SESSION" \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'
# 202
```

**3. List the tools:**

```bash
curl -s http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SESSION" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
# data: {"id":2,"jsonrpc":"2.0","result":{"tools":[{"description":"Return the given text",...,"name":"echo"},{...,"name":"wait"}]}}
```

**4. Call a tool:**

```bash
curl -s http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SESSION" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"echo","arguments":{"text":"Hello, MCP!"}}}'
# data: {"id":3,"jsonrpc":"2.0","result":{"content":[{"text":"Hello, MCP!","type":"text"}]}}
```

An unknown tool or bad arguments give a JSON-RPC error, for example
`{"error":{"code":-32602,"message":"Unknown tool: nope"},...}`.

**5. Cancel a running call.** Start a 30-second `wait` in the background, then send
`notifications/cancelled` with its request id; the call ends at once with an error:

```bash
curl -s http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SESSION" \
  -d '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"wait","arguments":{"seconds":30}}}' &
sleep 1
curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SESSION" \
  -d '{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":4}}'
wait
# 202
# data: {"error":{"code":-32603,"message":"cancelled"},"id":4,"jsonrpc":"2.0"}
```

**6. End the session:**

```bash
curl -s -o /dev/null -w "%{http_code}\n" -X DELETE http://127.0.0.1:8080/mcp -H "Mcp-Session-Id: $SESSION"
# 204
```

`GET /health` returns the server name and protocol version as JSON.

## What it demonstrates

- `ServerConfig` with `TransportType::HTTP_STREAMABLE`, host, port and endpoint;
  `MCPServer::listen()` (non-blocking), `port()` and `stop()`
- `registerMethod("initialize", ...)` to advertise the `tools` capability (the
  protocol version and the session are handled by `MCPServer`)
- `registerMethod("tools/list", ...)` returning the tool descriptions with JSON
  schemas
- `registerCancellable("tools/call", ...)`: the handler polls its `cancelled` token,
  which a `notifications/cancelled` from the same session sets. Handlers run on
  `MCPServer`'s worker threads, so the notification is processed while the call runs.
- `JsonRpcError::invalidParams()` for bad input; any other exception becomes a
  `-32603` internal error

## Expected output

```
MCP server listening at http://127.0.0.1:8080/mcp
^CShutting down...
```
