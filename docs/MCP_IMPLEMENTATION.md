# MCP (Model Context Protocol) Implementation Guide

This document describes the MCP client/server implementation in SocketsHpp following VS Code configuration patterns and MCP HTTP Stream Transport specification.

## Architecture Overview

```
SocketsHpp/
├── http/                     [Existing HTTP/1.1 layer]
│   ├── client/
│   │   └── http_client.h     ✅ HTTP client with streaming
│   ├── server/
│   │   └── http_server.h     ✅ HTTP server with SSE, sessions, CORS
│   └── common/
│       ├── http_constants.h  ✅ HTTP headers, status codes
│       ├── json_rpc.h        ✅ JSON-RPC 2.0 protocol layer
│       └── url_parser.h      ✅ URL/query parsing
└── mcp/                      [NEW - MCP protocol layer]
    ├── common/
    │   └── mcp_config.h      ✅ Configuration structures
    ├── server/
    │   └── mcp_server.h      ⏳ MCP server implementation
    └── client/
        └── mcp_client.h      ⏳ MCP client implementation
```

## Dependencies

### Required
- **nlohmann/json** - JSON parsing/serialization (header-only, MIT)
  ```cmake
  find_package(nlohmann_json CONFIG REQUIRED)
  ```

### Optional
- **jwt-cpp** - JWT Bearer token validation (header-only, MIT)
  ```cmake
  find_package(jwt-cpp CONFIG REQUIRED)
  ```

## Installation

### 1. Install vcpkg dependencies

```bash
# Install vcpkg manifest dependencies
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
```

The `vcpkg.json` manifest automatically installs:
- `nlohmann-json` (required)
- `jwt-cpp` (optional - only if MCP feature enabled)

### 2. Build with MCP support

```bash
# Build with MCP support
cmake --build build --config Release
```

## Configuration Patterns

SocketsHpp MCP follows the **VS Code MCP configuration** standard defined in `mcp.json`.

### Stdio Transport (Default)

**VS Code mcp.json**:
```json
{
  "servers": {
    "my-server": {
      "type": "stdio",
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-example"],
      "env": {
        "API_KEY": "${input:api_key}"
      },
      "envFile": "${workspaceFolder}/.env"
    }
  }
}
```

**C++ equivalent**:
```cpp
#include <SocketsHpp/mcp/common/mcp_config.h>

mcp::ClientConfig config;
config.transport = mcp::TransportType::STDIO;
config.stdio.command = "npx";
config.stdio.args = {"-y", "@modelcontextprotocol/server-example"};
config.stdio.env["API_KEY"] = "your-api-key";
config.stdio.envFile = ".env";
```

### HTTP Transport

**VS Code mcp.json**:
```json
{
  "servers": {
    "remote-server": {
      "type": "http",
      "url": "https://api.example.com/mcp",
      "headers": {
        "Authorization": "Bearer ${input:api_token}"
      }
    }
  }
}
```

**C++ equivalent**:
```cpp
mcp::ClientConfig config;
config.transport = mcp::TransportType::HTTP;
config.http.url = "https://api.example.com/mcp";
config.http.headers["Authorization"] = "Bearer your-token";
config.http.timeoutSeconds = 30;
config.http.enableResumability = true;
```

### Load from JSON

```cpp
#include <nlohmann/json.hpp>
#include <fstream>

// Load VS Code mcp.json
std::ifstream file("mcp.json");
json mcpConfig = json::parse(file);

// Extract server config
json serverConfig = mcpConfig["servers"]["my-server"];
mcp::ClientConfig config = mcp::ClientConfig::fromJson(serverConfig);
```

## Server Configuration

### Environment Variables (Recommended for Docker/Production)

```bash
export MCP_TRANSPORT=http
export MCP_PORT=8080
export MCP_ENDPOINT=/mcp
export MCP_HOST=0.0.0.0
export MCP_RESPONSE_MODE=stream
export MCP_ENABLE_RESUMABILITY=true
export MCP_CORS_ORIGIN=*
export MCP_AUTH_TYPE=bearer
export MCP_AUTH_SECRET=your-secret-key

./my-mcp-server
```

**C++ code**:
```cpp
mcp::ServerConfig config;
config.parseEnv();  // Loads from environment variables
```

### Command-Line Arguments

```bash
./my-mcp-server \
  --transport http \
  --port 8080 \
  --endpoint /mcp \
  --host 0.0.0.0 \
  --response-mode stream \
  --enable-resumability \
  --cors-origin "*"
```

**C++ code**:
```cpp
int main(int argc, char** argv) {
    mcp::ServerConfig config;
    config.parseArgs(argc, argv);  // Parses command-line args
    
    // ... create server with config
}
```

### Programmatic Configuration

```cpp
mcp::ServerConfig config;

// Transport
config.transport = mcp::TransportType::HTTP;
config.port = 8080;
config.endpoint = "/mcp";
config.host = "0.0.0.0";

// Response mode
config.responseMode = mcp::ServerConfig::ResponseMode::STREAM;

// Message limits
config.maxMessageSize = 4 * 1024 * 1024;  // 4MB
config.batchTimeoutMs = 30000;

// CORS
config.cors.allowOrigin = "*";
config.cors.allowMethods = "GET, POST, DELETE, OPTIONS";
config.cors.allowHeaders = "Content-Type, Accept, Authorization, Mcp-Session-Id, Last-Event-ID";

// Session management
config.session.enabled = true;
config.session.headerName = "Mcp-Session-Id";
config.session.allowClientTermination = true;
config.session.sessionTimeoutSeconds = 3600;

// Resumability (Last-Event-ID support)
config.resumability.enabled = true;
config.resumability.historyDurationMs = 300000;  // 5 minutes
config.resumability.maxHistorySize = 1000;

// Authentication (optional)
config.auth.enabled = true;
config.auth.type = mcp::ServerConfig::AuthConfig::Type::BEARER;
config.auth.headerName = "Authorization";
config.auth.secretOrPublicKey = "your-jwt-secret";

// Custom validator
config.auth.validator = [](const std::string& token) -> bool {
    // Custom token validation logic
    return token == "valid-token";
};
```

## Authentication Options

### 1. Bearer Token (JWT or Opaque)

**Configuration**:
```cpp
config.auth.enabled = true;
config.auth.type = mcp::ServerConfig::AuthConfig::Type::BEARER;
config.auth.headerName = "Authorization";
```

**Client request**:
```http
POST /mcp HTTP/1.1
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
Content-Type: application/json
```

**JWT validation with jwt-cpp**:
```cpp
#include <jwt-cpp/jwt.h>

config.auth.validator = [secret = "your-secret"](const std::string& token) -> bool {
    try {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret})
            .with_issuer("your-issuer");
        
        auto decoded = jwt::decode(token);
        verifier.verify(decoded);
        return true;
    } catch (...) {
        return false;
    }
};
```

### 2. API Key

**Configuration**:
```cpp
config.auth.enabled = true;
config.auth.type = mcp::ServerConfig::AuthConfig::Type::API_KEY;
config.auth.headerName = "x-api-key";
config.auth.validator = [](const std::string& key) {
    return key == "your-api-key";
};
```

**Client request**:
```http
POST /mcp HTTP/1.1
x-api-key: your-api-key
Content-Type: application/json
```

### 3. Custom Validation

```cpp
config.auth.validator = [](const std::string& token) -> bool {
    // Database lookup
    return database.isValidToken(token);
};
```

## JSON-RPC 2.0 Layer

The `json_rpc.h` header provides full JSON-RPC 2.0 support:

### Request/Response

```cpp
#include <SocketsHpp/http/common/json_rpc.h>

using namespace http::common;

// Create request
JsonRpcRequest req;
req.id = "request-123";
req.method = "tools/list";
req.params = json::object();

std::string jsonStr = req.serialize();
// {"jsonrpc":"2.0","id":"request-123","method":"tools/list","params":{}}

// Parse request
auto parsed = JsonRpcRequest::parse(jsonStr);

// Create response
auto resp = JsonRpcResponse::success("request-123", json{{"tools", json::array()}});
std::string respStr = resp.serialize();
```

### Notifications

```cpp
// Create notification (no ID)
JsonRpcNotification notif;
notif.method = "notifications/initialized";
notif.params = json{{"version", "1.0"}};

std::string jsonStr = notif.serialize();
// {"jsonrpc":"2.0","method":"notifications/initialized","params":{"version":"1.0"}}
```

### Error Handling

```cpp
// Standard errors
auto error = JsonRpcError::methodNotFound("unknown_method");
auto resp = JsonRpcResponse::failure("request-123", error);

// Custom errors
auto customError = JsonRpcError::serverError(-32000, "Database connection failed");

// With data
JsonRpcError error;
error.code = -32001;
error.message = "Validation failed";
error.data = json{{"field", "email"}, {"reason", "invalid format"}};
```

## Example: Complete MCP Server

```cpp
#include <SocketsHpp/mcp/server/mcp_server.h>

int main(int argc, char** argv) {
    // Configuration
    mcp::ServerConfig config;
    config.parseEnv();           // Load from environment
    config.parseArgs(argc, argv); // Override with command-line args
    
    // Create server
    mcp::MCPServer server(config);
    
    // Register MCP methods
    server.registerMethod("initialize", [](const json& params) -> json {
        return {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", {
                {"tools", json::object()}
            }},
            {"serverInfo", {
                {"name", "my-mcp-server"},
                {"version", "1.0.0"}
            }}
        };
    });
    
    server.registerMethod("tools/list", [](const json& params) -> json {
        return {
            {"tools", json::array({
                {
                    {"name", "get_weather"},
                    {"description", "Get current weather"},
                    {"inputSchema", {
                        {"type", "object"},
                        {"properties", {
                            {"location", {{"type", "string"}}}
                        }},
                        {"required", json::array({"location"})}
                    }}
                }
            })}
        };
    });
    
    server.registerMethod("tools/call", [](const json& params) -> json {
        std::string tool = params["name"];
        if (tool == "get_weather") {
            std::string location = params["arguments"]["location"];
            // Fetch weather...
            return {
                {"content", json::array({
                    {{"type", "text"}, {"text", "Weather: Sunny, 72°F"}}
                })}
            };
        }
        throw JsonRpcError::methodNotFound(tool);
    });
    
    // Start server
    server.listen();
    
    return 0;
}
```

## Example: MCP Client

```cpp
#include <SocketsHpp/mcp/client/mcp_client.h>

int main() {
    // Load config from mcp.json
    std::ifstream file("mcp.json");
    json mcpConfig = json::parse(file);
    auto config = mcp::ClientConfig::fromJson(mcpConfig["servers"]["my-server"]);
    
    // Create client
    mcp::MCPClient client(config);
    
    // Initialize connection
    json initResult = client.initialize({
        {"protocolVersion", "2024-11-05"},
        {"capabilities", json::object()},
        {"clientInfo", {
            {"name", "my-client"},
            {"version", "1.0.0"}
        }}
    });
    
    // Call method
    json tools = client.request("tools/list", json::object());
    std::cout << "Available tools: " << tools.dump(2) << std::endl;
    
    // Call tool
    json result = client.request("tools/call", {
        {"name", "get_weather"},
        {"arguments", {{"location", "Tokyo"}}}
    });
    
    // Handle server notifications
    client.onNotification("notifications/message", [](const json& params) {
        std::cout << "Server message: " << params["message"] << std::endl;
    });
    
    // Close connection
    client.close();
    
    return 0;
}
```

## Testing

Comprehensive tests are provided in `test/functional/`:

```bash
# Build and run MCP tests
cmake --build build --target mcp_server_test mcp_client_test json_rpc_test
ctest -R mcp
```

## HTTP Stream Transport Protocol

### Endpoints

| Method | Endpoint | Purpose |
|--------|----------|---------|
| POST | `/mcp` | Send JSON-RPC requests |
| GET | `/mcp?session={id}` | Establish SSE stream for server messages |
| DELETE | `/mcp` | Terminate session |
| OPTIONS | `/mcp` | CORS preflight |

### Headers

| Header | Direction | Purpose |
|--------|-----------|---------|
| `Mcp-Session-Id` | Both | Session identification |
| `Last-Event-ID` | Client → Server | Resume from event |
| `Authorization` | Client → Server | Bearer token auth |
| `x-api-key` | Client → Server | API key auth |
| `Content-Type` | Both | `application/json` or `text/event-stream` |
| `Accept` | Client → Server | `application/json, text/event-stream` |

### Session Flow

1. **Initialize**: Client sends `initialize` request via POST
2. **Session Created**: Server responds with `Mcp-Session-Id` header
3. **SSE Stream**: Client opens GET connection with session ID
4. **Messages**: Bidirectional JSON-RPC 2.0 messages
5. **Terminate**: Client sends DELETE request (optional)

### Resumability (Last-Event-ID)

When enabled, server maintains event history:

```http
GET /mcp?session=abc123 HTTP/1.1
Last-Event-ID: event-456
```

Server replays missed events since `event-456`.

## Best Practices

### Server
1. **Always enable CORS** for web clients
2. **Use environment variables** for secrets (never hardcode)
3. **Enable resumability** for long-running connections
4. **Set message size limits** to prevent DoS
5. **Implement proper authentication** for production

### Client
1. **Use mcp.json** for configuration
2. **Handle reconnections** automatically
3. **Validate server capabilities** after initialize
4. **Set appropriate timeouts**
5. **Handle errors gracefully**

### Security
1. **Use HTTPS** in production
2. **Validate JWT signatures** properly
3. **Rotate API keys** regularly
4. **Implement rate limiting**
5. **Log authentication failures**

## Troubleshooting

### "Failed to parse JSON-RPC request"
- Ensure Content-Type is `application/json`
- Validate JSON syntax
- Check request has required fields (`jsonrpc`, `method`)

### "Session not found"
- Check `Mcp-Session-Id` header is being sent
- Verify session hasn't expired
- Enable session logging for debugging

### "Authorization failed"
- Verify Bearer token format: `Authorization: Bearer {token}`
- Check JWT signature and claims
- Ensure token hasn't expired

### "Connection timeout"
- Increase `readTimeoutSeconds` in client config
- Check network connectivity
- Verify server is listening on correct port

## References

- [MCP Specification](https://spec.modelcontextprotocol.io/)
- [MCP HTTP Stream Transport](https://spec.modelcontextprotocol.io/specification/basic/transports/#http-with-sse)
- [JSON-RPC 2.0 Specification](https://www.jsonrpc.org/specification)
- [VS Code MCP Documentation](https://code.visualstudio.com/docs/copilot/customization/mcp-servers)
- [jwt-cpp Documentation](https://thalhammer.github.io/jwt-cpp/)
- [nlohmann/json Documentation](https://json.nlohmann.me/)

## License

Apache-2.0 (same as SocketsHpp)
