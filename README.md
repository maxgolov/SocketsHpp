# Sockets.HPP

**Modern cross-platform C++17 header-only networking library** with enterprise-grade HTTP server capabilities, real-time streaming, and Model Context Protocol (MCP) support.

## Features

- **Pure Header-Only**: Zero compilation required, just `#include <sockets.hpp>`
- **Cross-Platform**: Windows, Linux, macOS, iOS, Android, embedded systems
- **C++17 Standard**: Modern C++ with minimal dependencies
- **Enterprise HTTP**: Production-ready server with middleware, authentication, compression, and proxy awareness
- **Optional Multi-Threading**: BS::thread_pool integration for concurrent request processing
- **Real-Time Streaming**: Server-Sent Events (SSE) for live data streaming
- **MCP Support**: Full Model Context Protocol server and client implementation
- **Comprehensive Testing**: 231 unit and integration tests on Windows, 183 on Linux/ARM64 (100% passing)

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

**All Platforms (Windows):**
```cmd
build-all.cmd
```
Builds and tests for Windows x64, Linux x64 (WSL), and Linux ARM64 (QEMU).

**Windows x64 Only:**
```cmd
build.cmd
```
or
```powershell
powershell -ExecutionPolicy Bypass -File scripts\Build-Windows.ps1
```

**Linux x64 (Native or WSL):**
```bash
./scripts/build-linux.sh
```

**Linux ARM64 (Cross-Compilation):**
```bash
./scripts/build-arm64.sh
```

**Legacy Build (Deprecated):**
```bash
./build.sh  # Creates out/ directory with Ninja build
```

**Custom CMake:**
```bash
# Windows
cmake -B build/windows-x64 -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows-x64 --config Release

# Linux
cmake -B build/linux-x64 -S . -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build/linux-x64
```

### Integration into Existing Projects

SocketsHpp is a header-only library that can be integrated into your C++ project using several methods:

#### Option 1: vcpkg Package Manager (Recommended for vcpkg Users)

Best for projects already using vcpkg. Provides full dependency management and binary caching.

SocketsHpp includes a ready-to-use vcpkg port in `./ports/socketshpp/`. See [Example 11: vcpkg Consumption](./examples/11-vcpkg-consumption/) for a complete working example.

**Quick Install:**
```bash
# From the SocketsHpp repository root
vcpkg install socketshpp --overlay-ports=./ports
```

**Use in your project's `vcpkg.json`:**
```json
{
  "name": "my-project",
  "version": "1.0.0",
  "dependencies": ["socketshpp"],
  "vcpkg-configuration": {
    "overlay-ports": ["path/to/SocketsHpp/ports"]
  }
}
```

**CMakeLists.txt integration:**
```cmake
# Find the package
find_package(SocketsHpp CONFIG REQUIRED)

# Link to your target
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE SocketsHpp::SocketsHpp)

# Platform-specific libraries (automatic on most systems)
if(WIN32)
    target_link_libraries(myapp PRIVATE ws2_32)
else()
    target_link_libraries(myapp PRIVATE pthread)
endif()
```

#### Option 2: Git Submodules (Recommended for Version Control)

Best for reproducible builds with tight version control integration.

```bash
# Add SocketsHpp as submodule
git submodule add https://github.com/maxgolov/SocketsHpp.git external/SocketsHpp

# Add dependencies
git submodule add https://github.com/nlohmann/json.git external/nlohmann-json
git submodule add https://github.com/bshoshany/thread-pool.git external/thread-pool

# Initialize submodules when cloning
git clone --recursive https://github.com/youruser/yourproject.git
```

**CMakeLists.txt integration:**
```cmake
# Add dependencies
add_subdirectory(external/nlohmann-json)
add_subdirectory(external/thread-pool)

# Create interface library for SocketsHpp
add_library(socketshpp INTERFACE)
target_include_directories(socketshpp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/SocketsHpp/include)
target_compile_features(socketshpp INTERFACE cxx_std_17)
target_link_libraries(socketshpp INTERFACE 
    nlohmann_json::nlohmann_json
    bshoshany-thread-pool::bshoshany-thread-pool)

# Use in your project
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE socketshpp)
```

#### Option 3: CMake FetchContent (Recommended for CMake-Native Projects)

Best for projects wanting zero external dependencies beyond CMake.

```cmake
include(FetchContent)

# Fetch SocketsHpp
FetchContent_Declare(socketshpp
    GIT_REPOSITORY https://github.com/maxgolov/SocketsHpp.git
    GIT_TAG main
)

# Fetch dependencies
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_Declare(thread_pool
    GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
    GIT_TAG v4.1.0
)

FetchContent_MakeAvailable(nlohmann_json thread_pool)

# Manually handle SocketsHpp (header-only)
FetchContent_GetProperties(socketshpp)
if(NOT socketshpp_POPULATED)
    FetchContent_Populate(socketshpp)
    
    add_library(socketshpp INTERFACE)
    target_include_directories(socketshpp INTERFACE
        ${socketshpp_SOURCE_DIR}/include)
    target_compile_features(socketshpp INTERFACE cxx_std_17)
    target_link_libraries(socketshpp INTERFACE 
        nlohmann_json::nlohmann_json)
endif()

# Use in your project
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE socketshpp)
```

#### Option 4: Manual Installation (Simple Projects)

Best for minimal projects or when modifying library source.

```bash
# Clone repository
git clone https://github.com/maxgolov/SocketsHpp.git

# Copy headers to your project
cp -r SocketsHpp/include/SocketsHpp /path/to/your/project/external/include/
cp -r SocketsHpp/include/sockets.hpp /path/to/your/project/external/include/
```

**CMakeLists.txt:**
```cmake
add_executable(myapp main.cpp)
target_include_directories(myapp PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/external/include)
target_compile_features(myapp PRIVATE cxx_std_17)
```

**Note:** You'll need to manually install dependencies (nlohmann-json, thread-pool) as well.

#### Basic Usage

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

### HTTP Server with Multi-Threading (Optional)

```cpp
#include <sockets.hpp>

HttpServer server("0.0.0.0", 8080);

// Enable thread pool for concurrent request processing (optional)
server.enableThreadPool(4);  // 4 worker threads (0 = hardware_concurrency)

server.addHandler("/api/compute", [](const auto& req, auto& res) {
    // CPU-intensive work offloaded to worker threads
    // Reactor thread remains responsive for I/O
    auto result = performExpensiveComputation();
    res.body = result;
});

server.start();
server.waitExit();

// Thread pool is automatically stopped on server shutdown
// Can also disable manually: server.disableThreadPool();
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

The project includes 231 comprehensive tests on Windows, 183 on Linux/ARM64:

```bash
# Build and run all platforms (Windows)
build-all.cmd

# Windows x64 only
powershell -ExecutionPolicy Bypass -File scripts\Build-Windows.ps1

# Linux x64 (WSL or native)
./scripts/build-linux.sh

# Linux ARM64 (cross-compilation with QEMU)
./scripts/build-arm64.sh

# Run tests manually after build
cd build/windows-x64  # or build/linux-x64, build/linux-arm64
ctest --output-on-failure
```

Test coverage:
- **71 unit tests** for new features (proxy, auth, compression, MCP, SSE, JSON-RPC)
- **160 functional tests** for core networking and HTTP
- **100% pass rate** on Windows x64, Linux x64, and Linux ARM64
- **231 tests on Windows** (48 Windows-specific tests)
- **183 tests on Linux/ARM64** (cross-platform core)

## Documentation

- **[FEATURES.md](docs/FEATURES.md)** - Complete feature overview
- **[MCP_IMPLEMENTATION.md](docs/MCP_IMPLEMENTATION.md)** - MCP architecture details
- **[BUILD_SUMMARY.md](docs/BUILD_SUMMARY.md)** - Build system documentation
- **[ARM64.md](docs/ARM64.md)** - ARM64 platform support

## Dependencies

### Runtime (Header-Only Mode)
- C++17 standard library
- System socket library (POSIX sockets or WinSock)
- **[BS::thread_pool](https://github.com/bshoshany/thread-pool)** (optional) - Multi-threading support

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
- **Optional Multi-Threading**: BS::thread_pool integration available but not required
- **Platform Agnostic**: Abstracts BSD sockets and WinSock differences
- **Modern C++**: Uses C++17 features, no legacy baggage
- **Minimalist & Practical**: ~75% HTTP/1.1 compliance, optimized for <10K concurrent connections
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
