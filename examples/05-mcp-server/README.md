# Model Context Protocol (MCP) Server Example

A simplified MCP server implementation using HTTP+SSE transport.

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Running

```bash
./mcp-server
```

## Testing

### SSE Stream
```bash
curl -N http://localhost:8080/sse
```

Output:
```
id: 1
event: message
data: {"jsonrpc": "2.0", "method": "initialized", ...}

id: 2
event: message
data: {"jsonrpc": "2.0", "method": "tools/list", ...}

event: ping
data: 1704110400
...
```

### Session Management
```bash
# Delete session
curl -X DELETE http://localhost:8080/session

# Server metadata
curl http://localhost:8080/info
```

### Base64 Encoding
```bash
curl -X POST http://localhost:8080/base64 -d "Hello, MCP!"
```

## What it demonstrates

### MCP Features
- ✅ HTTP+SSE transport layer
- ✅ CORS configuration for web clients
- ✅ Session management
- ✅ DELETE method for cleanup
- ✅ OPTIONS for CORS preflight
- ✅ Base64 encoding utility
- ✅ SSE event formatting
- ⚠️ Simplified JSON-RPC (demo only)

### SocketsHpp Features
- `HttpServer::CorsConfig` for CORS setup
- `SSEEvent` for Server-Sent Events
- Method-based routing (GET, POST, DELETE, OPTIONS)
- `base64::encode()` from utils
- Session state management
- Real-time message streaming

## Protocol Notes

This example shows the **transport layer** of MCP. A production server would add:

1. **JSON-RPC 2.0 parsing** - Full request/response handling
2. **Method dispatch** - tools/list, tools/call, prompts/list, etc.
3. **Error handling** - JSON-RPC error codes
4. **Authentication** - Bearer tokens or API keys
5. **Session persistence** - Database or Redis
6. **Connection management** - Reconnection, event replay
7. **Rate limiting** - Request throttling

## MCP Specification

See https://spec.modelcontextprotocol.io/ for:
- Full JSON-RPC message format
- Tool/prompt/resource schemas
- Error codes and handling
- Security recommendations

## Implementation Status

See `docs/FEATURES.md` for detailed MCP feature coverage.
