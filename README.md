# Sockets.HPP

**Cross-platform, header-only C++17 networking library**: sockets, a reactor-based
HTTP/1.1 server and client, Server-Sent Events (SSE), and a Model Context Protocol
(MCP) server and client.

## Features

- **Header-only**: nothing to compile or link beyond the system socket library;
  `#include <sockets.hpp>` (or individual headers under `SocketsHpp/`).
- **Sockets**: thin RAII-friendly wrappers over BSD sockets / WinSock (TCP, UDP,
  IPv4/IPv6, Unix domain sockets), a generic `SocketServer` and a small
  `net::tcp::TcpServer`.
- **Event reactor**: epoll on Linux, kqueue on macOS, `WSAEventSelect` /
  `WSAWaitForMultipleEvents` on Windows.
- **HTTP/1.1 server** (`HttpServer`): prefix routing, keep-alive, pipelining,
  chunked request bodies, `Expect: 100-continue`, HEAD, CORS, request size limits,
  strict request parsing (ambiguous `Content-Length` / `Transfer-Encoding` framing is
  rejected), optional worker thread pool, chunked streaming responses and SSE.
- **Static files** (`HttpFileServer`): serves a document root with path-traversal
  protection and MIME types.
- **HTTP client** (`HttpClient`): plain `http://` only (no TLS), redirects,
  timeouts, chunked/streamed responses, cancellation. **SSE client** (`SSEClient`)
  with WHATWG parsing, `Last-Event-ID` and auto-reconnect.
- **Server helpers**: authentication strategies (Bearer, API key, Basic),
  a pluggable compression registry (you bring the codec), and reverse-proxy
  helpers (`X-Forwarded-*`, RFC 7239 `Forwarded`) with trusted-proxy configuration.
- **MCP**: JSON-RPC 2.0 layer, `MCPServer` (HTTP+SSE 2024-11-05, Streamable HTTP
  2025-03-26, or JSON-RPC over your own STDIO loop) and `MCPClient` (HTTP transports).
- **Tested**: 500+ GoogleTest cases in CI on Ubuntu (GCC, Clang, ASan/UBSan),
  macOS (Clang), Windows (MSVC) and MinGW-w64 (under Wine).

What it is not: there is no TLS (put a reverse proxy such as nginx in front for
HTTPS, or use it for plain-HTTP/loopback services), no HTTP/2, no WebSocket, and no
client connection pooling (every request opens a new connection).

## Origins

Built upon Apache License 2.0 code from:
- [Microsoft 1DS C/C++ Telemetry](https://github.com/microsoft/cpp_client_telemetry/blob/main/tests/common/HttpServer.hpp)
- [OpenTelemetry C++ SDK](https://github.com/open-telemetry/opentelemetry-cpp/tree/main/ext/include/opentelemetry/ext/http)

Extensively refactored and extended since. See [LICENSE](./LICENSE).

## Dependencies

| Dependency | Needed by | Notes |
|------------|-----------|-------|
| C++17 compiler | everything | MSVC 2019+, GCC 8+, Clang 7+ |
| System sockets + threads | everything | `ws2_32` on Windows, pthreads elsewhere (the CMake target adds both) |
| [BS::thread_pool](https://github.com/bshoshany/thread-pool) 5.x | `http_server.h` (and everything that includes it) | Bundled as `external/BS_thread_pool.hpp`; always required, even if you never call `enableThreadPool()` |
| [nlohmann/json](https://github.com/nlohmann/json) | MCP headers, `json_rpc.h`, and the umbrella `sockets.hpp` | Git submodule `external/nlohmann-json`, or any installed copy |
| [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) | optional | Enables JWT (HS256) validation in `MCPServer` (`SOCKETSHPP_HAS_JWT_CPP`) |
| [GoogleTest](https://github.com/google/googletest) | tests only | Only when `SOCKETSHPP_BUILD_TESTS=ON` |

`sockets.hpp` includes the MCP headers, so it needs nlohmann/json. Code that only
needs sockets or HTTP can include the specific headers (for example
`SocketsHpp/http/server/http_server.h`) and skip nlohmann/json.

## Quick start (CMake)

All integration paths end with the same interface target, `SocketsHpp::SocketsHpp`,
which carries the include directories, C++17, threads and (on Windows) `ws2_32`.

### add_subdirectory

```cmake
find_package(nlohmann_json CONFIG REQUIRED)   # or provide it with FetchContent
add_subdirectory(external/SocketsHpp)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE SocketsHpp::SocketsHpp nlohmann_json::nlohmann_json)
```

In a subproject build the target does not carry nlohmann/json by itself, so link
`nlohmann_json::nlohmann_json` yourself when you use `sockets.hpp` or the MCP headers.

### FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_Declare(SocketsHpp
    GIT_REPOSITORY https://github.com/maxgolov/SocketsHpp.git
    GIT_TAG main)   # pin a commit or tag in real projects
FetchContent_MakeAvailable(json SocketsHpp)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE SocketsHpp::SocketsHpp nlohmann_json::nlohmann_json)
```

### Install + find_package

```bash
cmake -S . -B build
cmake --install build --prefix /opt/socketshpp
```

```cmake
find_package(SocketsHpp 1.0 CONFIG REQUIRED)   # CMAKE_PREFIX_PATH=/opt/socketshpp
target_link_libraries(myapp PRIVATE SocketsHpp::SocketsHpp)
```

The install contains the headers, `BS_thread_pool.hpp` (unless
`SOCKETSHPP_INSTALL_BUNDLED_THREAD_POOL=OFF`) and the CMake package. The package
config attaches `nlohmann_json::nlohmann_json` automatically when
`find_package(nlohmann_json)` succeeds, and `jwt-cpp` when the library was installed
with it.

### vcpkg (overlay port)

A port lives in [`ports/socketshpp`](ports/socketshpp/README.md). It is not in the
vcpkg registry, so use it as an overlay port in manifest mode:

```json
{
  "name": "myapp",
  "version": "1.0.0",
  "dependencies": [ "socketshpp" ]
}
```

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_OVERLAY_PORTS=/path/to/SocketsHpp/ports
```

Then `find_package(SocketsHpp CONFIG REQUIRED)` as above. The port depends on
`nlohmann-json` and `bshoshany-thread-pool` (>= 5.0.0); the optional `jwt` feature
adds `jwt-cpp`. See [example 11](examples/11-vcpkg-consumption/).

### Without CMake

Add `include/` and `external/` (for `BS_thread_pool.hpp`) plus the nlohmann/json
include directory to the include path, compile as C++17 and link threads
(and `ws2_32` on Windows):

```bash
g++ -std=c++17 -Iinclude -Iexternal -Iexternal/nlohmann-json/single_include main.cpp -pthread
```

## Building this repository

```bash
git clone --recursive https://github.com/maxgolov/SocketsHpp.git
cd SocketsHpp
cmake -S . -B build -DSOCKETSHPP_BUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tests need GoogleTest (`find_package(GTest CONFIG)`): for example
`apt install libgtest-dev`, `brew install googletest`, or the vcpkg toolchain
(`-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`, which also
provides nlohmann-json and the thread pool from `vcpkg.json`). On multi-config
generators (Visual Studio) add `--config Debug` to the build and `-C Debug` to ctest.

### CMake options

| Option | Default | Meaning |
|--------|---------|---------|
| `SOCKETSHPP_BUILD_TESTS` | `OFF` | Build the unit/functional tests and header checks (needs GoogleTest) |
| `BUILD_EXAMPLES` | `OFF` | Build `examples/01`-`10` |
| `SOCKETSHPP_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX` |
| `SOCKETSHPP_INSTALL` | `ON` for top-level builds | Generate install rules and the CMake package |
| `SOCKETSHPP_INSTALL_BUNDLED_THREAD_POOL` | `ON` | Install `external/BS_thread_pool.hpp` with the headers |
| `ENABLE_ASAN` | `OFF` | AddressSanitizer (GCC/Clang) |
| `ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer (GCC/Clang) |

### Cross-compiling

- **MinGW-w64** (Windows binaries from Linux):
  `cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake`.
  Tests can run under Wine with `-DCMAKE_CROSSCOMPILING_EMULATOR=$(command -v wine64)`
  (GoogleTest must be built with the same toolchain).
- **Linux ARM64** with QEMU: see [docs/ARM64.md](docs/ARM64.md).

The helper scripts (`build.cmd`, `build-all.cmd`, `scripts/*.sh`, `scripts/*.ps1`,
see [scripts/README.md](scripts/README.md)) are convenience wrappers; the plain CMake
commands above are what CI runs.

## Usage

The examples below use these namespaces (all under `SocketsHpp`, also available as
the `SOCKETSHPP_NS` macro): `net::utils` (sockets), `http::server`, `http::client`,
`mcp`, `mcp::server`, `mcp::client`.

### TCP and UDP clients

```cpp
#include <SocketsHpp/net/common/socket_tools.h>
#include <string>

using namespace SocketsHpp::net::utils;

int main()
{
    // TCP: connect and send
    Socket tcp(SocketParams{AF_INET, SOCK_STREAM, 0});
    if (!tcp.connect(SocketAddr("127.0.0.1:8080")))
        return 1;
    const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    tcp.send(request.data(), request.size());
    char buffer[4096];
    int received = tcp.recv(buffer, sizeof(buffer));
    (void)received;
    tcp.close();

    // UDP: connect() just sets the default destination for send()
    Socket udp(SocketParams{AF_INET, SOCK_DGRAM, 0});
    udp.connect(SocketAddr("127.0.0.1:9000"));
    udp.send("hello", 5);
    udp.close();
}
```

`SocketAddr` accepts `"host:port"`, `"[v6]:port"` or `(address, port)`.
For a simple TCP server, `net::tcp::TcpServer` (in `SocketsHpp/net/tcp/tcp.h`)
binds to `127.0.0.1` by default and replies with whatever the `onMessage` handler returns:

```cpp
#include <SocketsHpp/net/tcp/tcp.h>

int main()
{
    SocketsHpp::net::tcp::TcpServer server(0);  // port 0 = ephemeral, on 127.0.0.1
    server.onMessage([](const std::string& msg, auto&) { return "echo: " + msg; });
    server.Start();
    int port = server.port();
    (void)port;
    server.Stop();
}
```

### HTTP server

```cpp
#include <SocketsHpp/http/server/http_server.h>
#include <chrono>
#include <thread>

using namespace SocketsHpp::http::server;

int main()
{
    // "my-service" is only used for the Server response header. This constructor
    // listens on port 8080 on all IPv4 interfaces.
    HttpServer server("my-service", 8080);

    server.route("/hello", [](const HttpRequest& req, HttpResponse& res) -> int {
        std::string name = "world";
        try
        {
            auto query = req.parse_query();  // /hello?name=... (URL-decoded)
            if (query.count("name"))
                name = query["name"];
        }
        catch (const std::invalid_argument&)  // malformed or oversized query string
        {
            return 400;
        }
        res.set_content("Hello, " + name + "!\n");
        return 200;
    });

    server.route("/api/echo", [](const HttpRequest& req, HttpResponse& res) -> int {
        if (req.method != "POST")
            return 405;
        res.set_content(req.content, req.get_header_value("Content-Type"));
        return 200;
    });

    server.start();  // non-blocking: the reactor runs on its own thread
    std::this_thread::sleep_for(std::chrono::minutes(5));
    server.stop();   // also done by the destructor
}
```

Handlers are `int(const HttpRequest&, HttpResponse&)`. `HttpRequest` has `method`,
`uri` (including the query string), `protocol`, `headers`, `content` (the body) and
`client` (`"ip:port"` of the peer), plus `get_header_value()`, `has_header()`,
`parse_query()` and `accepts()`. `HttpResponse` has `set_status()`, `set_header()`,
`set_content(body, contentType)` and `send_chunk_stream()`.

Handlers must not let exceptions escape: the server does not catch them, and an
exception thrown on the reactor or a pool thread terminates the process. Note that
`parse_query()` throws `std::invalid_argument` for malformed or oversized query strings.

**Listening address.** To bind a specific address (for example loopback only), or to
use an ephemeral port, start from the default constructor:

```cpp
HttpServer server;
server.setServerName("my-service");
int port = server.addListeningPort("127.0.0.1", 0);  // returns the bound port
// server.getListeningPort() returns the first bound port as well
```

`addListeningPort(port)` listens on all IPv4 interfaces; `addListeningPort(host, port)`
accepts `"127.0.0.1"`, `"::1"`, `"[::1]"` or `"localhost"`. A server can listen on
several ports (`getListeningPorts()`).

**Routing rules.**

- Routes are URI **prefixes**: `/api` also matches `/api/users` and `/apix`. Candidates
  are tried longest prefix first, then in registration order, so a `/` catch-all never
  shadows `/events`.
- A handler takes the request when it returns a non-zero status, calls
  `set_status()`, or produces a body or a stream (status then defaults to 200).
  Returning `0` without touching the response declines, and the next candidate runs.
  Returning `-1` closes the connection without a response.
- If no handler takes the request: `OPTIONS` gets 204 when CORS is enabled
  (`enableCors()`, `setCorsOrigin()`, `setCorsHeaders()`) and 405 otherwise; `DELETE`
  with an `Mcp-Session-Id` header terminates that session of the server's built-in
  session manager; everything else gets 404.
- `HEAD` is dispatched as `GET` and the body is dropped.
- `route()` owns the callback. `addHandler(path, HttpRequestCallback&)` and
  `server[path] = callback` store a reference, so that object must outlive the server.

Other knobs: `setRequestLimits(maxHeaderBytes, maxBodyBytes)` (defaults 8 KB / 2 MB;
oversized requests get 431 / 413), `setKeepalive(false)`, and
`createSession()` / `validateSession()` / `terminateSession()` / `setSessionTimeout()`.

### Streaming and Server-Sent Events

`send_chunk_stream(callback, onEnd)` sends a chunked response: the server calls
`callback` repeatedly, sends each returned string immediately, and ends the stream when
it returns an empty string. (`send_chunk()` only appends to a buffered body.)

```cpp
server.route("/events", [](const HttpRequest&, HttpResponse& res) -> int {
    res.set_header("Content-Type", "text/event-stream");
    int n = 0;
    res.send_chunk_stream(
        [n]() mutable -> std::string {
            if (n == 5)
                return "";  // end of stream
            if (n > 0)
                std::this_thread::sleep_for(std::chrono::seconds(1));
            ++n;
            return SSEEvent::message("tick " + std::to_string(n), std::to_string(n)).format();
        },
        [] { /* stream finished */ });
    return 200;
});
```

For `text/event-stream` responses the server adds `Cache-Control: no-cache` and
`X-Accel-Buffering: no`. `SSEEvent` (`message()`, `custom(event, data, id)`, or set
`event`/`data`/`id`/`retry`) formats one event and splits multi-line data.

### Thread pool

By default all handlers and stream callbacks run on the single reactor thread, so a
slow handler (or a stream callback that sleeps, as above) stalls every connection.

```cpp
server.enableThreadPool(8);  // before start(); 0 = std::thread::hardware_concurrency()
```

With the pool enabled, request handlers and every stream-callback invocation run on
pool workers while the reactor keeps serving I/O. Handlers for different connections
then run concurrently and must be thread-safe; requests on one connection are still
processed in order. `disableThreadPool()` / `isThreadPoolEnabled()` are available. The
pool is `BS::thread_pool` from the bundled header, which `http_server.h` always includes.

### HTTP client

```cpp
#include <SocketsHpp/http/client/http_client.h>
#include <iostream>

using namespace SocketsHpp::http::client;

int main()
{
    HttpClient client;
    client.setConnectTimeout(5000);  // ms
    client.setReadTimeout(10000);    // ms

    HttpClientResponse resp;
    if (client.get("http://127.0.0.1:8080/hello?name=you", resp))
        std::cout << resp.code << ' ' << resp.getHeader("content-type") << '\n' << resp.body;

    HttpClientRequest req;
    req.method = "PUT";
    req.uri = "http://127.0.0.1:8080/api/items/1";
    req.setContentType("application/json");
    req.body = R"({"name":"widget"})";
    HttpClientResponse putResp;
    bool ok = client.send(req, putResp);  // false on connection/protocol errors
    return ok ? 0 : 1;
}
```

- Only `http://` URLs work: `https://` is rejected (the call returns false) instead of
  being sent in cleartext.
- Redirects (301/302/303/307/308) are followed up to 10 hops (`setMaxRedirects()`,
  `setFollowRedirects(false)`). 303, and 301/302 for methods other than GET/HEAD, switch
  to GET without a body; `Authorization`, `Cookie` and `Proxy-Authorization` are dropped
  when a redirect leaves the original host and port.
- `getHeader()` / `hasHeader()` match names case-insensitively; repeated response
  headers are joined with `", "`.
- Set `resp.chunkCallback` / `resp.onComplete` to receive the body incrementally;
  `setMaxResponseBodySize()` caps buffered bodies (1 GiB default); `cancel()` aborts an
  in-flight request from another thread.

### SSE client

```cpp
#include <SocketsHpp/http/client/sse_client.h>
#include <iostream>
#include <thread>

using namespace SocketsHpp::http::client;

int main()
{
    SSEClient sse;
    sse.setRequestHeader("Authorization", "Bearer my-token");  // sent on every (re)connect
    sse.setAutoReconnect(true, 3000);                          // ms; server "retry:" overrides

    std::thread reader([&sse] {
        // Blocks until the stream ends (or close() is called when auto-reconnect is on)
        sse.connect("http://127.0.0.1:8080/events",
            [](const SSEEvent& e) { std::cout << "id " << e.id << ": " << e.data << '\n'; },
            [](const std::string& error) { std::cerr << error << '\n'; });
    });

    std::this_thread::sleep_for(std::chrono::seconds(10));
    sse.close();  // thread-safe; unblocks connect()
    reader.join();
}
```

The client tracks the last event id and sends it as `Last-Event-ID` when it reconnects.
Unlike `HttpClient`, it has no read timeout by default.

### Authentication

`SocketsHpp/http/server/authentication.h` provides `BearerTokenAuth`, `ApiKeyAuth` and
`BasicAuth` strategies and an `AuthenticationMiddleware` that tries them in order.
There is no global middleware hook: call it from the routes you want to protect.

```cpp
#include <SocketsHpp/http/server/authentication.h>
#include <SocketsHpp/http/server/http_server.h>
#include <memory>

using namespace SocketsHpp::http::server;

void addProtectedRoute(HttpServer& server)
{
    auto bearer = std::make_shared<BearerTokenAuth<HttpRequest>>([](const std::string& token) {
        return token == "secret-token" ? AuthResult::success("alice")
                                       : AuthResult::failure("invalid token");
    });
    auto apiKey = std::make_shared<ApiKeyAuth<HttpRequest>>("X-API-Key", [](const std::string& key) {
        return key == "key-123" ? AuthResult::success("service") : AuthResult::failure("bad key");
    });

    auto auth = std::make_shared<AuthenticationMiddleware<HttpRequest, HttpResponse>>();
    auth->addStrategy(bearer);
    auth->addStrategy(apiKey);

    server.route("/api/private", [auth, bearer](const HttpRequest& req, HttpResponse& res) -> int {
        HttpRequest request = req;  // authenticate() takes a mutable request
        if (!auth->authenticate(request, res))
            return 401;  // body and WWW-Authenticate challenges are already set

        // A strategy can also be called directly to get the AuthResult (user id, claims)
        AuthResult who = bearer->authenticate(req);
        res.set_content(who ? "hello " + who.userId + "\n" : "hello API client\n");
        return 200;
    });
}
```

### Compression

`SocketsHpp/http/server/compression.h` has a codec registry and an
`Accept-Encoding`-aware `CompressionMiddleware`, but **no codec of its own**: register
one (for example gzip via zlib) at startup, then compress per route:

```cpp
#include <SocketsHpp/http/server/compression.h>
#include <SocketsHpp/http/server/http_server.h>
#include <memory>

using namespace SocketsHpp::http::server;

std::vector<uint8_t> gzipCompress(const std::vector<uint8_t>& in, int level);  // your codec
std::vector<uint8_t> gzipDecompress(const std::vector<uint8_t>& in);

void addCompressedRoute(HttpServer& server)
{
    CompressionRegistry::instance().registerStrategy(
        std::make_shared<CompressionStrategy>("gzip", gzipCompress, gzipDecompress));

    auto compression = std::make_shared<CompressionMiddleware>();  // level 6, >= 1 KB, text/JSON types
    server.route("/data", [compression](const HttpRequest& req, HttpResponse& res) -> int {
        std::string body(4096, 'x');
        std::string encoding;
        if (compression->compressResponse(req.get_header_value("Accept-Encoding"),
                                          "text/plain", body, encoding))
            res.set_header("Content-Encoding", encoding);
        res.set_header("Vary", "Accept-Encoding");
        res.set_content(body, "text/plain");
        return 200;
    });
}
```

Register codecs before `start()` (the registry is not synchronized). For tests,
`compression::registerSimpleCompression()` (`compression_simple.h`) adds `rle` and
`identity`; on Windows, `compression::registerWindowsCompression()`
(`compression_windows.h`, link `cabinet`) adds `mszip`, `xpress` and `lzms`, which
are not standard HTTP content-codings and are only useful between your own peers.

### Behind a reverse proxy

```cpp
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/server/proxy_aware.h>

using namespace SocketsHpp::http::server;

void addWhoAmI(HttpServer& server)
{
    TrustProxyConfig trust;
    trust.addTrustedProxy("127.0.0.1");  // switches the mode to TrustSpecific
    // or: trust.setMode(TrustProxyConfig::TrustMode::TrustAll);  (never on a public port)

    server.route("/whoami", [trust](const HttpRequest& req, HttpResponse& res) -> int {
        std::string ip = ProxyAwareHelpers::getClientIP(req.headers, req.client, trust);
        std::string proto = ProxyAwareHelpers::getProtocol(req.headers, req.client, trust);
        std::string host = ProxyAwareHelpers::getHost(req.headers, req.client, trust);
        res.set_content(proto + "://" + host + " from " + ip + "\n");
        return 200;
    });
}
```

Forwarded headers (`X-Forwarded-For/Proto/Host`, `X-Real-IP`, RFC 7239 `Forwarded`)
are honoured only when the direct peer (`req.client`) is trusted; otherwise the
helpers report the direct connection.

### MCP server

```cpp
#include <SocketsHpp/mcp/server/mcp_server.h>
#include <chrono>
#include <thread>

using namespace SocketsHpp::mcp;
using SocketsHpp::http::common::JsonRpcError;
using json = nlohmann::json;

int main()
{
    ServerConfig config;
    config.transport = TransportType::HTTP_STREAMABLE;  // MCP 2025-03-26
    config.host = "127.0.0.1";                          // loopback unless allowNonLoopback
    config.port = 8080;                                 // 0 = ephemeral, see server.port()
    config.endpoint = "/mcp";
    config.serverName = "demo-server";

    server::MCPServer server(config);

    server.registerMethod("initialize", [](const json&) -> json {
        // protocolVersion is filled in by the server's version negotiation
        return {{"capabilities", {{"tools", json::object()}}},
                {"serverInfo", {{"name", "demo-server"}, {"version", "1.0.0"}}}};
    });

    server.registerMethod("tools/list", [](const json&) -> json {
        json echo = {{"name", "echo"},
                     {"description", "Echo the text back"},
                     {"inputSchema", {{"type", "object"},
                                      {"properties", {{"text", {{"type", "string"}}}}}}}};
        return {{"tools", json::array({echo})}};
    });

    server.registerMethod("tools/call", [](const json& params) -> json {
        if (params.value("name", "") != "echo")
            throw JsonRpcError::invalidParams("unknown tool");
        std::string text = params.value("arguments", json::object()).value("text", "");
        return {{"content", json::array({{{"type", "text"}, {"text", text}}})}};
    });

    server.listen();  // non-blocking
    std::this_thread::sleep_for(std::chrono::minutes(5));
    server.stop();
}
```

`ping`, `notifications/initialized`, `notifications/cancelled` and `logging/setLevel`
are built in. `registerCancellable()` gives a handler a cancel token,
`push_event()` / `push_log()` / `push_progress()` send server-initiated messages on a
session's SSE stream, `processMessage()` handles one JSON-RPC message (or batch) for
a STDIO transport you drive yourself, and `GET /health` returns the server info. See
[docs/MCP_IMPLEMENTATION.md](docs/MCP_IMPLEMENTATION.md) for transports, sessions,
resumability, authentication (including JWT via jwt-cpp), rate limiting and CORS.

### MCP client

```cpp
#include <SocketsHpp/mcp/client/mcp_client.h>
#include <iostream>

using namespace SocketsHpp::mcp;
using json = nlohmann::json;

int main()
{
    ClientConfig config;
    config.transport = TransportType::HTTP_STREAMABLE;  // or HTTP (2024-11-05); STDIO is not supported
    config.http.url = "http://127.0.0.1:8080/mcp";      // http:// only
    config.http.headers["Authorization"] = "Bearer my-token";

    client::MCPClient client;
    client.onNotification("notifications/message", [](const json& params) {
        std::cout << "log: " << params.dump() << '\n';
    });
    if (!client.connect(config))
        return 1;

    try
    {
        json info = client.initialize({{"name", "demo-client"}, {"version", "1.0.0"}});
        json tools = client.listTools();
        json result = client.callTool("echo", {{"text", "hi"}});
        std::cout << result.dump(2) << '\n';
    }
    catch (const SocketsHpp::http::common::JsonRpcError& e)  // JSON-RPC error response
    {
        std::cerr << e.code << ' ' << e.message << '\n';
    }
    catch (const std::exception& e)  // HTTP/transport failure
    {
        std::cerr << e.what() << '\n';
    }
    client.disconnect();  // sends DELETE to end the session
}
```

## Platform notes

| Platform | Reactor | Notes |
|----------|---------|-------|
| Linux (x64, ARM64) | epoll | GCC and Clang in CI; ARM64 via cross-compilation + QEMU ([docs/ARM64.md](docs/ARM64.md)) |
| macOS | kqueue | AppleClang in CI |
| Windows (x64; ARM64 not in CI) | `WSAEventSelect` + `WSAWaitForMultipleEvents` | MSVC x64 in CI; MinGW-w64 via `cmake/toolchains/mingw-w64-x86_64.cmake` (tests under Wine) |

- On Windows a reactor waits on at most 64 event handles (`WSA_MAXIMUM_WAIT_EVENTS`),
  so one server handles roughly 64 sockets at a time (listening sockets included).
  WinSock is initialized automatically.
- Unix domain sockets are available on POSIX systems and on Windows when the SDK
  provides `<afunix.h>` (Windows 10 SDK 17063 or later).
- Logging is compiled out by default. Define `HAVE_CONSOLE_LOG` to print to stdout,
  or define `LOG_DEBUG` / `LOG_TRACE` / `LOG_INFO` / `LOG_WARN` / `LOG_ERROR` yourself
  before including the headers.

## Header-only guarantee

Every header is self-contained and every function is `inline`, so the headers can be
included from any number of translation units. The test build enforces this with two
targets (built when `SOCKETSHPP_BUILD_TESTS=ON`):

- `header_self_contained_check` compiles every public header on its own, twice
  (include guards);
- `header_only_link_check` links two translation units that each include every public
  header (catches non-inline definitions) and runs as a ctest.

## Examples

See [examples/README.md](examples/README.md). Build them with
`-DBUILD_EXAMPLES=ON`; binaries land in `build/examples/<name>/`.

| Example | Shows |
|---------|-------|
| [01-tcp-echo](examples/01-tcp-echo/) | TCP client sending 1 MB |
| [02-udp-echo](examples/02-udp-echo/) | UDP client sending a datagram |
| [03-http-server](examples/03-http-server/) | Routes, query parameters, JSON/HTML responses |
| [04-http-sse](examples/04-http-sse/) | SSE streaming with `send_chunk_stream` and the thread pool |
| [05-mcp-server](examples/05-mcp-server/) | Hand-rolled MCP-style SSE transport on `HttpServer` (not `MCPServer`) |
| [06-proxy-aware](examples/06-proxy-aware/) | `TrustProxyConfig` and `ProxyAwareHelpers` |
| [07-authentication](examples/07-authentication/) | Bearer token / API key checks in handlers |
| [08-compression](examples/08-compression/) | Large HTML response (compression is not wired in) |
| [09-full-featured](examples/09-full-featured/) | Proxy awareness + authentication |
| [10-typescript-interop](examples/10-typescript-interop/) | JSON-RPC interop with TypeScript client/server |
| [11-vcpkg-consumption](examples/11-vcpkg-consumption/) | Consuming the vcpkg overlay port with `find_package` |

## Documentation

- [docs/FEATURES.md](docs/FEATURES.md) - what is and is not implemented
- [docs/INTEGRATION.md](docs/INTEGRATION.md) - adding SocketsHpp to a project
- [docs/MCP_IMPLEMENTATION.md](docs/MCP_IMPLEMENTATION.md) - MCP server/client guide
- [docs/ARM64.md](docs/ARM64.md) - ARM64 cross-compilation and QEMU testing
- [test/README.md](test/README.md) - test suites and how to run them
- [include/SocketsHpp/utils/README.md](include/SocketsHpp/utils/README.md) - Base64 utility
- [ports/socketshpp/README.md](ports/socketshpp/README.md) - vcpkg port

## Contributing

Contributions are welcome. Please make sure the tests pass
(`-DSOCKETSHPP_BUILD_TESTS=ON`, then `ctest`), follow the existing style, and add
tests and documentation for new features.

## License

Apache License 2.0 - see [LICENSE](./LICENSE).

Based on code from the Microsoft 1DS C/C++ Telemetry and OpenTelemetry C++ SDK projects.
