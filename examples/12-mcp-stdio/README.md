# MCP over stdio

A [Model Context Protocol](https://modelcontextprotocol.io) server and client on the
**stdio transport**: the client launches the server as a child process and the two
exchange newline-delimited JSON-RPC messages over the server's stdin and stdout.

| Program | What it does |
|---------|--------------|
| `mcp-stdio-server` | `MCPServer` driven by `StdioServerTransport::runStdio()`. Tools: `add` (`{"a", "b"}` numbers) and `count` (`{"to": 1-100}`, 100 ms per step, sends `notifications/progress` and log messages, cancellable). Logs to stderr only. |
| `mcp-stdio-client` | `MCPClient` with `TransportType::STDIO`: starts the server, initializes, sets the log level, lists and calls tools, prints progress / log notifications and the server's stderr, cancels a long call, then disconnects (closes stdin; the server exits). |

For the API in depth see [docs/MCP_IMPLEMENTATION.md](../../docs/MCP_IMPLEMENTATION.md#stdio-transport).

## Building

From the repository root:

```bash
cmake -S . -B build -DSOCKETSHPP_BUILD_EXAMPLES=ON
cmake --build build --target mcp-stdio-client   # also builds mcp-stdio-server
```

## Running

```bash
./build/examples/12-mcp-stdio/mcp-stdio-client
```

The client looks for `mcp-stdio-server` next to itself; pass another server command
(and its arguments) to talk to any stdio MCP server, e.g.
`mcp-stdio-client npx -y @modelcontextprotocol/server-everything` (the tool calls of
this demo are specific to its own server, so they fail there).

Expected output (abridged):

```
Connected to {"name":"example-stdio-server","version":"1.0.0"}, protocol 2025-03-26
Tool: add - Add two numbers
Tool: count - Count to N (1-100), reporting progress; cancellable
add(2, 3) = 5.0
  progress: 1/3
  log: "reached 1"
...
Counted to 3
count(100): Request cancelled by the client: tools/call
Disconnected
```

The server can also be used by any MCP host that launches stdio servers, e.g. a
VS Code `mcp.json` entry:

```json
{
  "servers": {
    "sockets-hpp-demo": { "type": "stdio", "command": "/path/to/mcp-stdio-server" }
  }
}
```

To try the server by hand, run it and type one message per line:

```
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"me","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add","arguments":{"a":1,"b":2}}}
```

End the session with Ctrl+D (Ctrl+Z, Enter on Windows).
