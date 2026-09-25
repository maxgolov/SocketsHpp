# Example 10: TypeScript Interop (JSON-RPC / MCP-style)

C++ and TypeScript programs talking MCP-style JSON-RPC 2.0 over plain HTTP POST:

- `cpp_server.cpp`: a JSON-RPC endpoint at `http://127.0.0.1:3000/mcp`, written as an
  `HttpServer` route. It answers `initialize`, `tools/list` and `tools/call` for a
  `get_weather` tool that returns a random condition. (This is not the library's
  `MCPServer`; see [docs/MCP_IMPLEMENTATION.md](../../docs/MCP_IMPLEMENTATION.md) for
  that.)
- `cpp_client.cpp`: uses `HttpClient::post()` to call `initialize`, `tools/list` and
  `tools/call` (`greet` with `{"name": "Alice"}`) on the TypeScript server at
  `http://localhost:3001/mcp`.
- `ts_server/server.ts`: Node.js JSON-RPC server on port 3001 with `get_weather` and
  `greet` tools.
- `ts_client/client.ts`: calls the C++ server (`initialize`, `tools/list`,
  `tools/call get_weather`).
- `ts_client/client-ai.ts`: optional; lets an OpenAI model (Vercel AI SDK) call the
  C++ server's tools. Needs an OpenAI API key.

```
10-typescript-interop/
  cpp_server.cpp   cpp_client.cpp   CMakeLists.txt
  demo.sh          demo.ps1         # build everything and run both directions
  ts_server/       ts_client/       # Node.js projects (package.json, *.ts)
```

## Prerequisites

- A C++17 compiler and CMake (see the [main README](../../README.md))
- Node.js 18+ and npm

## Building the C++ programs

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target cpp_server cpp_client
```

The binaries are `build/examples/10-typescript-interop/cpp_server` and `cpp_client`.
Without CMake, from this directory:

```bash
g++ -std=c++17 -I../../include -I../../external -I../../external/nlohmann-json/single_include \
    cpp_server.cpp -o cpp_server -pthread
g++ -std=c++17 -I../../include -I../../external -I../../external/nlohmann-json/single_include \
    cpp_client.cpp -o cpp_client -pthread
```

`demo.sh` / `demo.ps1` build the C++ programs with this directory's own
`CMakeLists.txt` (into `./build`), install the npm dependencies and run both demos.

## Running

### TypeScript client -> C++ server

```bash
./build/examples/10-typescript-interop/cpp_server      # from the repository root; port 3000
cd examples/10-typescript-interop/ts_client && npm install && npx tsx client.ts
```

### C++ client -> TypeScript server

```bash
cd examples/10-typescript-interop/ts_server && npm install && npx tsx server.ts   # port 3001
./build/examples/10-typescript-interop/cpp_client      # from the repository root
```

### AI client (optional)

```bash
cd examples/10-typescript-interop/ts_client
OPENAI_API_KEY=sk-... npx tsx client-ai.ts
```

`client-ai.ts` also loads a `.env` file three directories above its working directory
(the repository root when run from `ts_client/`); `client.ts` and `server.ts` load
`../.env` but need no variables. Keep any `.env` file out of version control.

## Protocol

Every call is a JSON-RPC 2.0 request in an HTTP POST body, answered with a JSON-RPC
response, for example:

```json
{"jsonrpc": "2.0", "id": 1, "method": "tools/call",
 "params": {"name": "get_weather", "arguments": {}}}
```

```json
{"jsonrpc": "2.0", "id": 1,
 "result": {"content": [{"type": "text", "text": "Weather: rainy"}]}}
```

The C++ server also answers CORS preflight (`OPTIONS`) and returns 405 for other
methods; errors are reported as JSON-RPC error `-32603` with HTTP status 500.

## References

- [Model Context Protocol](https://modelcontextprotocol.io/)
- [JSON-RPC 2.0](https://www.jsonrpc.org/specification)
- [Vercel AI SDK](https://sdk.vercel.ai/)
