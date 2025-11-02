# Sockets.HPP

**Modern cross-platform C++17 header-only networking library** with enterprise-grade HTTP server capabilities, real-time streaming, and Model Context Protocol (MCP) support.

## Features

- **Pure Header-Only**: Zero compilation required, just `#include <sockets.hpp>`
- **Cross-Platform**: Windows, Linux, macOS, iOS, Android, embedded systems
- **C++17 Standard**: Modern C++ with minimal dependencies
- **Enterprise HTTP**: Production-ready server with middleware, authentication, compression, and proxy awareness
- **Real-Time Streaming**: Server-Sent Events (SSE) for live data streaming
- **MCP Support**: Full Model Context Protocol server and client implementation
- **Comprehensive Testing**: 231 unit and integration tests (100% passing)

## Origins

Built upon Apache License 2.0 code from:
- [Microsoft 1DS C/C++ Telemetry](https://github.com/microsoft/cpp_client_telemetry/blob/main/tests/common/HttpServer.hpp)
- [OpenTelemetry C++ SDK](https://github.com/open-telemetry/opentelemetry-cpp/tree/main/ext/include/opentelemetry/ext/http)

Extensively refactored and enhanced for modern application development. See [LICENSE](./LICENSE) for details.

## Quick Start

### Prerequisites

1. **C++17 Compiler**: MSVC 2019+, GCC 8+, or Clang 7+
2. **vcpkg** (for building tests): [Install vcpkg](https://vcpkg.io/en/getting-started.html)

### Building

**Windows:**
```cmd
build.cmd
```

**Linux/macOS:**
```bash
./build.sh
```

**Custom CMake:**
```bash
cmake -B build -S .
cmake --build build --config Release
```

### Integration

Simply include the main header in your project:

```cpp
#include <sockets.hpp>
```

No linking required - it's header-only!

## Core Components

### Network Foundations

| Component | Description |
|-----------|-------------|
| `net/common/socket_tools.h` | Low-level socket abstraction (BSD/WinSock) |
| `net/common/socket_server.h` | TCP, UDP, Unix Domain socket servers |

### HTTP Layer

| Component | Description |
|-----------|-------------|
| `http/common/url_parser.h` | URL parsing (`http://host:port`) |
| `http/common/http_constants.h` | HTTP status codes and headers |
| `http/common/json_rpc.h` | JSON-RPC 2.0 implementation |
| `http/server/http_server.h` | Full-featured HTTP server |
| `http/server/http_file_server.h` | Static file serving with MIME types |
| `http/client/http_client.h` | HTTP client with connection pooling |
| `http/client/sse_client.h` | Server-Sent Events client |

### Enterprise Features

| Component | Description |
|-----------|-------------|
| `http/server/proxy_aware.h` | Reverse proxy support (X-Forwarded-*, RFC 7239) |
| `http/server/authentication.h` | Multi-strategy auth (Bearer, API Key, Basic) |
| `http/server/compression.h` | Content negotiation and compression |
| `http/server/compression_windows.h` | Windows Compression API (MSZIP, XPRESS, LZMS) |

### Model Context Protocol (MCP)

| Component | Description |
|-----------|-------------|
| `mcp/server/mcp_server.h` | MCP server with tool/resource/prompt support |
| `mcp/client/mcp_client.h` | MCP client for AI integrations |
| `mcp/common/mcp_config.h` | Configuration and discovery |

### Utilities

| Component | Description |
|-----------|-------------|
| `utils/base64.h` | Base64 encoding/decoding |
| `config.h` | Namespace configuration |
| `macros.h` | Debug and logging macros |

## Usage Examples

### Basic TCP Client

```cpp
#include <sockets.hpp>

SocketParams params{ AF_INET, SOCK_STREAM, 0 };
SocketAddr destination("127.0.0.1:8080");
Socket client(params);
client.connect(destination);
client.send("GET / HTTP/1.1\r\n\r\n", 18);
client.close();
```

### Basic UDP Client

```cpp
#include <sockets.hpp>

SocketParams params{ AF_INET, SOCK_DGRAM, 0 };
SocketAddr destination("127.0.0.1:8080");
Socket client(params);
client.connect(destination);
client.send("Hello, UDP!", 11);
client.close();
```

### HTTP Server with Routing

```cpp
#include <sockets.hpp>

HttpServer server("0.0.0.0", 8080);

server.addHandler("/api/hello", [](const auto& req, auto& res) {
    res.headers["Content-Type"] = "application/json";
    res.body = R"({"message": "Hello, World!"})";
});

server.addHandler("/api/echo", [](const auto& req, auto& res) {
    res.body = req.body;
});

server.start();
server.waitExit();
```

### HTTP Server with Authentication

```cpp
#include <sockets.hpp>
#include <SocketsHpp/http/server/authentication.h>

using namespace SocketsHpp;

HttpServer server("0.0.0.0", 8080);

// Create authentication middleware with Bearer token
auto bearerAuth = std::make_shared<BearerTokenAuth<HttpRequest>>(
    [](const std::string& token) {
        if (token == "secret-token-123") {
            return AuthResult::success("user-id-42");
        }
        return AuthResult::failure();
    }
);

AuthenticationMiddleware<HttpRequest, HttpResponse> authMiddleware;
authMiddleware.addStrategy(bearerAuth);

server.addHandler("/api/protected", [&authMiddleware](const auto& req, auto& res) {
    auto authResult = authMiddleware.authenticate(req, res);
    if (!authResult.success) {
        res.code = 401;
        return;
    }
    res.body = "Protected data for user: " + authResult.userId;
});

server.start();
```

### HTTP Server with Compression

```cpp
#include <sockets.hpp>
#include <SocketsHpp/http/server/compression.h>
#include <SocketsHpp/http/server/compression_windows.h>

using namespace SocketsHpp;

HttpServer server("0.0.0.0", 8080);

// Register Windows compression algorithms
registerWindowsCompression();

// Create compression middleware
CompressionMiddleware<HttpRequest, HttpResponse> compressor;

server.addHandler("/api/data", [&compressor](const auto& req, auto& res) {
    res.headers["Content-Type"] = "application/json";
    res.body = generateLargeJsonData(); // Your data function
    
    // Compress response based on Accept-Encoding
    compressor.compressResponse(req, res);
});

server.start();
```

### Server-Sent Events (SSE) Streaming

```cpp
#include <sockets.hpp>

HttpServer server("0.0.0.0", 8080);

server.addHandler("/events", [](const auto& req, auto& res) {
    res.headers["Content-Type"] = "text/event-stream";
    res.headers["Cache-Control"] = "no-cache";
    res.headers["Connection"] = "keep-alive";
    
    // Stream events to client
    for (int i = 0; i < 10; i++) {
        std::string event = "data: Event " + std::to_string(i) + "\n\n";
        res.streamCallback(event);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
});

server.start();
```

### Proxy-Aware Server (Behind Load Balancer)

```cpp
#include <sockets.hpp>
#include <SocketsHpp/http/server/proxy_aware.h>

using namespace SocketsHpp;

HttpServer server("0.0.0.0", 8080);

// Trust specific proxy IPs
TrustProxyConfig proxyConfig;
proxyConfig.mode = TrustMode::TrustSpecific;
proxyConfig.trustedProxies = {"10.0.0.1", "172.16.0.1"};

server.addHandler("/api/info", [&proxyConfig](const auto& req, auto& res) {
    // Get real client IP and protocol from X-Forwarded-* headers
    std::string clientIP = ProxyAwareHelpers::getClientIP(req, proxyConfig);
    std::string protocol = ProxyAwareHelpers::getProtocol(req, proxyConfig);
    bool isSecure = ProxyAwareHelpers::isSecure(req, proxyConfig);
    
    res.body = "Client IP: " + clientIP + 
               ", Protocol: " + protocol +
               ", Secure: " + (isSecure ? "yes" : "no");
});

server.start();
```

### Model Context Protocol (MCP) Server

```cpp
#include <sockets.hpp>
#include <SocketsHpp/mcp/server/mcp_server.h>

using namespace SocketsHpp;

MCPServer server("127.0.0.1", 3000);

// Register a tool
server.registerTool({
    .name = "calculate",
    .description = "Perform mathematical calculations",
    .inputSchema = R"({
        "type": "object",
        "properties": {
            "expression": {"type": "string"}
        }
    })"
}, [](const nlohmann::json& args) -> nlohmann::json {
    std::string expr = args["expression"];
    // Evaluate expression...
    return {{"result", 42}};
});

// Register a resource
server.registerResource({
    .uri = "file:///data/users.json",
    .name = "User Database",
    .description = "List of all users"
}, []() -> std::string {
    return R"([{"id": 1, "name": "Alice"}])";
});

server.start();
```

## Examples

Comprehensive examples with full documentation:

- **[01-tcp-echo](examples/01-tcp-echo/)** - TCP echo server
- **[02-udp-echo](examples/02-udp-echo/)** - UDP echo server  
- **[03-http-server](examples/03-http-server/)** - HTTP server with routing
- **[04-http-sse](examples/04-http-sse/)** - Server-Sent Events streaming
- **[05-mcp-server](examples/05-mcp-server/)** - Model Context Protocol server
- **[06-proxy-aware](examples/06-proxy-aware/)** - Reverse proxy awareness
- **[07-authentication](examples/07-authentication/)** - Multi-strategy authentication
- **[08-compression](examples/08-compression/)** - Content compression
- **[09-full-featured](examples/09-full-featured/)** - Combined enterprise features

Each example includes a README.md and CMakeLists.txt for standalone building.

## Testing

The project includes 231 comprehensive tests:

```bash
# Build and run all tests
cmake -B build -S .
cmake --build build --config Debug
cd build
ctest -C Debug
```

Test coverage:
- **71 unit tests** for new features (proxy, auth, compression, MCP, SSE, JSON-RPC)
- **160 functional tests** for core networking and HTTP
- **100% pass rate** on Windows x64, Linux, and ARM64

## Documentation

- **[FEATURES.md](docs/FEATURES.md)** - Complete feature overview
- **[MCP_IMPLEMENTATION.md](docs/MCP_IMPLEMENTATION.md)** - MCP architecture details
- **[BUILD_SUMMARY.md](docs/BUILD_SUMMARY.md)** - Build system documentation
- **[ARM64.md](docs/ARM64.md)** - ARM64 platform support

## Dependencies

### Runtime (Header-Only Mode)
- C++17 standard library
- System socket library (POSIX sockets or WinSock)

### Build & Testing
- **[vcpkg](https://vcpkg.io/)** - Package manager
- **[nlohmann/json](https://github.com/nlohmann/json)** - JSON parsing (MCP/JSON-RPC)
- **[cpp-jwt](https://github.com/arun11299/cpp-jwt)** - JWT authentication
- **[Google Test](https://github.com/google/googletest)** - Testing framework

All test dependencies are automatically managed by vcpkg.

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| Windows x64 | ✅ Fully Supported | MSVC 2019+, includes Windows Compression API |
| Windows ARM64 | ✅ Fully Supported | Native ARM64 builds |
| Linux x64 | ✅ Fully Supported | GCC 8+, Clang 7+ |
| Linux ARM64 | ✅ Fully Supported | Raspberry Pi, embedded systems |
| macOS x64/ARM64 | ✅ Fully Supported | Apple Silicon and Intel |
| iOS/Android | ✅ Supported | Embedded use cases |

## Design Philosophy

- **Header-Only**: Zero compilation, instant integration
- **Minimal Dependencies**: Only C++17 stdlib and system sockets for core functionality
- **Platform Agnostic**: Abstracts BSD sockets and WinSock differences
- **Modern C++**: Uses C++17 features, no legacy baggage
- **Production-Ready**: Enterprise features with comprehensive testing
- **Developer-Friendly**: Clear APIs, extensive examples, detailed documentation

## Contributing

Contributions welcome! Please ensure:
- All tests pass (`ctest -C Debug`)
- Code follows existing style
- New features include tests and examples
- Documentation is updated

## License

Apache License 2.0 - See [LICENSE](./LICENSE) for details.

Based on code from Microsoft 1DS C/C++ Telemetry and OpenTelemetry C++ SDK projects.

## Acknowledgments

- Microsoft 1DS Team - Original HTTP server implementation
- OpenTelemetry Contributors - Socket abstractions
- vcpkg Team - Excellent package management
