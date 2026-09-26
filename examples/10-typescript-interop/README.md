# Example 10: MCP interop with the official TypeScript SDK

This example checks SocketsHpp's MCP implementation against the official
[`@modelcontextprotocol/sdk`](https://www.npmjs.com/package/@modelcontextprotocol/sdk)
over the Streamable HTTP transport, in both directions:

| Direction | Client | Server |
|-----------|--------|--------|
| 1 | `ts/client.ts` (TypeScript SDK `Client`) | `cpp_server` (SocketsHpp `MCPServer`) |
| 2 | `cpp_client` (SocketsHpp `MCPClient`) | `ts/server.ts` (TypeScript SDK `McpServer`) |

Both servers expose the same two tools, `get_weather` and `greet` (`{ "name": string }`).
Each client initializes a session, lists the tools, calls both and checks the results,
then terminates the session (HTTP `DELETE`). A client exits non-zero if any step fails.
CI runs both directions on every push (`typescript-interop` job).

## Layout

```
10-typescript-interop/
├── cpp_server.cpp     SocketsHpp MCP server   (default http://127.0.0.1:3000/mcp)
├── cpp_client.cpp     SocketsHpp MCP client   (default http://127.0.0.1:3001/mcp)
├── run-interop.sh     builds, installs and runs both directions
└── ts/
    ├── server.ts      TypeScript SDK server   (default http://127.0.0.1:3001/mcp)
    ├── client.ts      TypeScript SDK client   (default http://127.0.0.1:3000/mcp)
    ├── package.json   pinned via package-lock.json; kept current by Dependabot
    └── tsconfig.json
```

## Requirements

- CMake 3.14+ and a C++17 compiler
- Node.js 18 or newer

## Run everything

```bash
./run-interop.sh                    # builds cpp_server/cpp_client into build-interop/
./run-interop.sh build/examples/10-typescript-interop   # or reuse an existing build
```

## Run each side by hand

```bash
# Build the C++ programs (from the repository root)
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target cpp_server cpp_client

# Install the TypeScript dependencies
cd examples/10-typescript-interop/ts
npm ci

# Direction 1: TypeScript client -> C++ server
../../../build/examples/10-typescript-interop/cpp_server 3000 &
npm run client -- http://127.0.0.1:3000/mcp

# Direction 2: C++ client -> TypeScript server
npm run server -- 3001 &
../../../build/examples/10-typescript-interop/cpp_client http://127.0.0.1:3001/mcp
```

Stop the background servers with `kill %1 %2` (or Ctrl+C in their terminals).

## Protocol version

The TypeScript SDK offers its latest protocol version; SocketsHpp answers with
`2025-03-26` (Streamable HTTP), which the SDK accepts. Both clients print the
negotiated version.
