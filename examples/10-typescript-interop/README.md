# Example 10: TypeScript Interop (ModelContext Protocol)

This example demonstrates interoperability between:
- **SocketsHpp C++ MCP server/client**
- **TypeScript MCP server/client** (using Vercel AI SDK ModelContext Protocol)

All tools implement a `get_weather` handler that returns a random weather kind.

## Directory Structure
```
10-typescript-interop/
  .env.example           # Copy to .env for API keys
  cpp_server.cpp         # Minimal MCP server using SocketsHpp
  cpp_client.cpp         # Minimal MCP client using SocketsHpp
  ts_server/             # TypeScript MCP server implementation
  ts_client/             # TypeScript MCP client implementation
```

## Running the Interop Example

### Prerequisites
- C++17 compiler (for SocketsHpp)
- Node.js v18+
- NPM

### .env File
Copy `.env.example` to `.env` and put your API keys there (not checked in).

### 1. Launch C++ MCP server (port 3000)
```
g++ -std=c++17 cpp_server.cpp -o cpp_server -I../../include/ && ./cpp_server
```

### 2. Run TypeScript client against C++ server
```
cd ts_client && npm install && npx tsx client.ts
```

### 3. Launch TypeScript server (port 3001)
```
cd ts_server && npm install && npx tsx server.ts
```

### 4. Run C++ client against TypeScript server
```
g++ -std=c++17 cpp_client.cpp -o cpp_client -I../../include/ && ./cpp_client
```


## Security
- Ensure your `.env` is **never** committed to version control (use `.gitignore`).

---

## Protocol
- Both client and server use ModelContext Protocol (JSON-RPC over HTTP).
- API: `{ "tool": "get_weather", ... } => { "kind": "rainy"|...}`

---

## References
- [ModelContext Protocol Spec](https://github.com/vercel/ai/blob/main/packages/core/src/protocol/schema.ts)
- [SocketsHpp Project](https://github.com/maxgolov/SocketsHpp)
- [Vercel AI SDK](https://sdk.vercel.ai/)
