# MCP (Model Context Protocol) Guide

SocketsHpp ships a JSON-RPC 2.0 layer, an MCP server and an MCP client, all
header-only and built on the library's own HTTP server and client.

| Header | Namespace | Contents |
|--------|-----------|----------|
| `SocketsHpp/http/common/json_rpc.h` | `SocketsHpp::http::common` | `JsonRpcRequest`, `JsonRpcNotification`, `JsonRpcResponse`, `JsonRpcError`, `JsonRpcId` |
| `SocketsHpp/mcp/common/mcp_config.h` | `SocketsHpp::mcp` | `TransportType`, `ServerConfig`, `ClientConfig`, `HttpConfig`, `StdioConfig` |
| `SocketsHpp/mcp/server/mcp_server.h` | `SocketsHpp::mcp::server` | `MCPServer` |
| `SocketsHpp/mcp/client/mcp_client.h` | `SocketsHpp::mcp::client` | `MCPClient` |

`SocketsHpp::mcp` also defines `using json = nlohmann::json;`.

## Dependencies

- **nlohmann/json** (required). Available as the `external/nlohmann-json` submodule or
  any installed copy; see the [README](../README.md#dependencies) for CMake wiring.
- **jwt-cpp** (optional). When CMake finds `jwt-cpp` (`find_package(jwt-cpp CONFIG)`),
  `SocketsHpp::SocketsHpp` defines `SOCKETSHPP_HAS_JWT_CPP` and links it, which enables
  built-in JWT validation in the server (see [Authentication](#authentication)). Without
  CMake, define `SOCKETSHPP_HAS_JWT_CPP` and put jwt-cpp (and OpenSSL) on your include
  and link paths yourself. The vcpkg port exposes this as the `jwt` feature.

## Transports

| `TransportType` | Protocol revision | Server | Client |
|-----------------|-------------------|--------|--------|
| `HTTP` | 2024-11-05 (HTTP + SSE) | POST for requests, GET for the notification stream | yes |
| `HTTP_STREAMABLE` | 2025-03-26 (Streamable HTTP) | POST returns JSON or SSE; optional GET stream | yes |
| `STDIO` | any | no listener: feed messages to `processMessage()` from your own stdin/stdout loop | **not supported** (`connect()` returns false) |

Both HTTP transports are plain HTTP. There is no TLS: the client rejects `https://`
URLs, and the server should sit behind a TLS-terminating reverse proxy when it is
reachable from other machines.

## Server

### Minimal server

```cpp
#include <SocketsHpp/mcp/server/mcp_server.h>
#include <chrono>
#include <thread>

using namespace SocketsHpp::mcp;
using SocketsHpp::http::common::JsonRpcError;
using json = nlohmann::json;

int main(int argc, char** argv)
{
    ServerConfig config;
    config.transport = TransportType::HTTP_STREAMABLE;
    config.host = "127.0.0.1";
    config.port = 8080;
    config.parseEnv();             // optional: MCP_* environment variables
    config.parseArgs(argc, argv);  // optional: --transport, --port, ...

    server::MCPServer server(config);

    server.registerMethod("initialize", [](const json&) -> json {
        return {{"capabilities", {{"tools", json::object()}}},
                {"serverInfo", {{"name", "weather"}, {"version", "1.0.0"}}}};
    });

    server.registerMethod("tools/list", [](const json&) -> json {
        json tool = {{"name", "get_weather"},
                     {"description", "Current weather for a city"},
                     {"inputSchema", {{"type", "object"},
                                      {"properties", {{"city", {{"type", "string"}}}}},
                                      {"required", json::array({"city"})}}}};
        return {{"tools", json::array({tool})}};
    });

    server.registerMethod("tools/call", [](const json& params) -> json {
        if (params.value("name", "") != "get_weather")
            throw JsonRpcError::invalidParams("unknown tool");
        std::string city = params.value("arguments", json::object()).value("city", "?");
        return {{"content", json::array({{{"type", "text"}, {"text", "Sunny in " + city}}})}};
    });

    server.listen();  // non-blocking; throws std::runtime_error if it cannot bind
    std::this_thread::sleep_for(std::chrono::hours(1));
    server.stop();    // also done by the destructor
}
```

Key points:

- `MCPServer` does not implement any MCP feature methods (`tools/*`, `resources/*`,
  `prompts/*`, ...) itself: you register every method your server offers with
  `registerMethod(name, handler)`. Handlers return the JSON-RPC `result`; throwing
  `JsonRpcError` (for example `JsonRpcError::invalidParams(...)`) produces that error,
  and any other `std::exception` becomes `-32603 Internal error`. Unknown methods get
  `-32601 Method not found`.
- Methods may be registered at any time, including after `listen()`.
- `listen()` binds `config.host:config.port`, starts a 4-thread worker pool and the
  reactor, and returns immediately. `port()` returns the bound port (useful with
  `config.port = 0`), or -1 before `listen()`. `stop()` closes all notification streams
  and stops the HTTP server. `listen()` throws for `TransportType::STDIO`.
- **Loopback guard:** `listen()` refuses to bind anything other than `127.0.0.1`,
  `localhost`, `::1` or `[::1]` unless `config.allowNonLoopback = true`. `0.0.0.0`
  therefore throws by default.

### Built-in methods

| Method | Behaviour |
|--------|-----------|
| `initialize` | Handled by the server; see below. |
| `ping` | Returns `{}`. |
| `notifications/initialized` | Accepted, no-op. |
| `notifications/cancelled` | Sets the cancel token of the in-flight request whose id is `params.requestId`, in the same session. |
| `logging/setLevel` | Sets the minimum level for `push_log()` (default `warning`); invalid levels get `-32602`. |

Registering one of these names replaces the built-in (for `initialize`, see below).

### Initialize and version negotiation

The server supports protocol versions `2025-03-26` and `2024-11-05`. For an
`initialize` request it answers with the client's `protocolVersion` if supported,
otherwise with `2025-03-26` (the client then decides whether to continue).

If you registered an `initialize` handler, its result object is returned with
`protocolVersion` overwritten by the negotiated version. Otherwise the server returns
`{"protocolVersion": ..., "capabilities": {}, "serverInfo": server_info()}` (name,
version and latest protocol version).
Register a handler to advertise capabilities such as `tools`.

The client's `capabilities` are stored per session and can be read with
`get_client_capabilities(sessionId)` (pass `""` for the STDIO transport).
`server_info()` returns the name, version and latest protocol version.

`initialize` must be a single message: inside a JSON-RPC batch it is rejected with
`-32600`.

### JSON-RPC details

- Batches (JSON arrays) are supported on every transport; notifications inside a batch
  produce no entry, and a batch of only notifications gets `202 Accepted` (HTTP) or an
  empty string (`processMessage()`).
- Request ids may be strings, integers (the full `int64` range, kept without narrowing)
  or `null`. Other ids, and unsigned values above `INT64_MAX`, are rejected with
  `-32600 Invalid Request`, as are messages whose `jsonrpc` is not `"2.0"`, whose
  `method` is missing, or whose `params` is not an object or array.
- Notifications (no `id`) never get a response; errors in notification handlers are
  logged and dropped.

### HTTP endpoints

| Request | `HTTP` (2024-11-05) | `HTTP_STREAMABLE` (2025-03-26) |
|---------|---------------------|--------------------------------|
| `POST <endpoint>` | `Content-Type` must be `application/json` (else 400). Response is `application/json` (array for batches). | Response is `text/event-stream` when `Accept` contains `text/event-stream` (one `data:` event per response, then the stream ends), otherwise `application/json`. |
| `GET <endpoint>` | Opens the session's notification stream (SSE). | Same. |
| `DELETE <endpoint>` | Terminates the session: 204, 404 if unknown, 400 without a session id, 405 if `session.allowClientTermination` is false. | Same. |
| `OPTIONS <endpoint>` | CORS preflight, 204. | Same. |
| `GET /health` | `server_info()` as JSON. | Same. |
| Notification-only POST | `202 Accepted`, empty body. | Same. |

The default endpoint is `/mcp` (`config.endpoint`). Request bodies above
`config.maxMessageSize` (4 MB) are rejected with 413.

With `TransportType::HTTP` and `responseMode = ResponseMode::STREAM`, an `initialize`
POST that accepts `text/event-stream` is answered as a single SSE event (`id: init-1`);
`responseMode` has no other effect.

### Sessions

With `session.enabled` (the default), a successful `initialize` creates a session and
returns its id in the `Mcp-Session-Id` response header (`session.headerName`).

- **Streamable HTTP:** every request except `initialize` must carry the header:
  missing gives 400, an unknown or expired id gives 404 with JSON-RPC error `-32001`.
- **Legacy HTTP:** the header is optional on POST, but validated when present.
- The notification stream (GET) needs the session id, from the header or, for
  compatibility, a `?session=<id>` query parameter.
- Sessions expire after `session.sessionTimeoutSeconds` (1 hour) without activity.
  At most 10,000 sessions exist at once; beyond that `initialize` fails with an
  internal error.

### Server-initiated messages

Messages from server to client travel on the session's notification stream (the GET
endpoint):

```cpp
// From any thread, e.g. inside a tools/call handler:
server.push_progress(sessionId, /*progressToken*/ "job-1", 0.5, 1.0, "halfway");
server.push_log(sessionId, "warning", "my-server", "disk almost full");
server.push_event(sessionId, "event: message\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/tools/list_changed\"}\n\n");
```

- `push_event()` queues a raw, fully formatted SSE event and returns false if the session
  has no stream queue. `push_progress()` and `push_log()` wrap
  `notifications/progress` and `notifications/message`; `push_log()` drops messages below
  the level set by `logging/setLevel`.
- With **Streamable HTTP** the queue is created at `initialize`, so events pushed before
  the client opens GET are kept and delivered when it does. With **legacy HTTP** the
  queue is created by the GET request, so `push_event()` returns false until then.
- One stream per session: a new GET replaces the previous stream (which is closed; its
  undelivered events move to the new stream).
- While idle the stream sends empty SSE comments a few times per second and a
  `: keepalive` comment every `sseWriteDeadlineSeconds` (30 s). The stream ends on `stop()`, on `DELETE`, when the
  session expires, or when `cleanupStaleSessions(idleSeconds)` removes it; call that
  periodically if clients may disappear without `DELETE`.
- To find the session inside a handler, have the client send it in the parameters
  (for example a progress token) or keep your own mapping; handlers receive only
  `params`.

### Resumability

With `resumability.enabled = true`:

- every event queued by `push_event()` (and the `push_*` helpers) gets an `id:` line
  (a server-wide sequence number) unless it already has one, and is recorded in the
  session's history (the newest `resumability.maxHistorySize` events, default 1000);
- a GET with `Last-Event-ID: <id>` replays the recorded events that follow `<id>` and
  then continues live. If `<id>` is not in the history, nothing is replayed.

History lives as long as the session. `resumability.historyDurationMs` is stored but
not currently enforced; the size limit is what bounds the history.

### Cancellation

```cpp
server.registerCancellable("tools/call",
    [](const json& params, std::shared_ptr<std::atomic<bool>> cancelled) -> json {
        for (int step = 0; step < 100; ++step)
        {
            if (cancelled->load())
                throw std::runtime_error("cancelled");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return {{"content", json::array()}};
    });
```

A `notifications/cancelled` with `params.requestId` equal to the in-flight request's
JSON-RPC id (and sent in the same session) sets the token. Handlers run on the HTTP
server's worker pool, so the notification is processed while the request runs.

### Authentication

```cpp
config.auth.enabled = true;
config.auth.type = ServerConfig::AuthConfig::Type::BEARER;   // "Authorization: Bearer <token>"
config.auth.validator = [](const std::string& token) { return token == "expected-token"; };
```

| `auth.type` | Header (`auth.headerName`) | Validation |
|-------------|----------------------------|------------|
| `BEARER` | `Authorization`; the `Bearer ` prefix is stripped | `validator`, else JWT (HS256, key `secretOrPublicKey`) when built with jwt-cpp |
| `API_KEY` | set `headerName = "x-api-key"` (or any header) | `validator`, else constant-time comparison with `secretOrPublicKey` |
| `CAPABILITY_TOKEN` | always `X-MCP-Capability-Token` | `validator`, else constant-time comparison with `secretOrPublicKey` |

- Header names match case-insensitively. Missing or invalid credentials get 401.
- If auth is enabled but nothing can validate (no `validator`, no usable secret, or
  `BEARER` with a secret but no jwt-cpp), requests get **500**: the server fails closed.
- JWT validation checks the HS256 signature; add your own `validator` (it takes
  precedence) for issuer/audience/expiry rules or other algorithms.
- Authentication applies to POST, GET and DELETE on the endpoint, not to `OPTIONS` or
  `/health`.

### Rate limiting

`config.maxRequestsPerMinute = N` (0 = off) allows N POST/GET requests per client per
fixed one-minute window; excess requests get `429 Too Many Requests` with
`Retry-After: 60`. Clients are identified by the TCP peer address. Set
`trustProxyHeaders = true` only when the server is reachable exclusively through a
reverse proxy: the last `X-Forwarded-For` hop is then used instead, which any direct
client could otherwise spoof.

### CORS

Every endpoint response (and `/health`) carries the headers from `config.cors`:
`allowOrigin` (default `*`), `allowMethods`, `allowHeaders`, `exposeHeaders` (includes
`Mcp-Session-Id`) and `maxAge`. `OPTIONS` answers 204. Narrow `allowOrigin` for
browser-facing deployments.

### STDIO transport

`MCPServer` does not read stdin itself. Drive it with `processMessage()`, which takes
one JSON-RPC message or batch and returns the response text (empty for notifications):

```cpp
#include <SocketsHpp/mcp/server/mcp_server.h>
#include <iostream>
#include <string>

int main()
{
    SocketsHpp::mcp::ServerConfig config;  // transport defaults to STDIO
    SocketsHpp::mcp::server::MCPServer server(config);
    // ... registerMethod(...) ...

    std::string line;
    while (std::getline(std::cin, line))  // newline-delimited JSON-RPC
    {
        std::string reply = server.processMessage(line);
        if (!reply.empty())
            std::cout << reply << '\n' << std::flush;
    }
}
```

Sessions, authentication, rate limiting and the notification stream do not apply to
STDIO.

### Configuration reference (`ServerConfig`)

| Field | Default | Notes |
|-------|---------|-------|
| `transport` | `STDIO` | `HTTP`, `HTTP_STREAMABLE` or `STDIO` |
| `serverName`, `serverVersion` | `"mcp-server"`, `"1.0.0"` | Default `serverInfo` and `/health` |
| `host`, `port`, `endpoint` | `"127.0.0.1"`, `8080`, `"/mcp"` | |
| `allowNonLoopback` | `false` | Loopback guard for `listen()` |
| `responseMode` | `BATCH` | `STREAM` only affects legacy `initialize` (see above) |
| `maxMessageSize` | 4 MB | HTTP request body limit |
| `batchTimeoutMs` | 30000 | Not used by the current implementation |
| `cors.*` | see [CORS](#cors) | |
| `session.enabled`, `headerName`, `allowClientTermination`, `sessionTimeoutSeconds` | `true`, `"Mcp-Session-Id"`, `true`, 3600 | |
| `resumability.enabled`, `historyDurationMs`, `maxHistorySize` | `false`, 300000, 1000 | See [Resumability](#resumability) |
| `auth.enabled`, `type`, `headerName`, `secretOrPublicKey`, `validator` | off | See [Authentication](#authentication) |
| `maxRequestsPerMinute`, `trustProxyHeaders` | 0, `false` | See [Rate limiting](#rate-limiting) |
| `sseWriteDeadlineSeconds` | 30 | Keepalive interval on idle streams |

`parseArgs(argc, argv)` understands `--transport http|streamable|http-streamable|stdio`,
`--port`, `--endpoint`, `--host`, `--response-mode stream|batch`, `--max-message-size`,
`--enable-resumability` and `--cors-origin`. `parseEnv()` reads `MCP_TRANSPORT`,
`MCP_PORT`, `MCP_ENDPOINT`, `MCP_HOST`, `MCP_RESPONSE_MODE`, `MCP_MAX_MESSAGE_SIZE`,
`MCP_ENABLE_RESUMABILITY` (`true`/`1`), `MCP_CORS_ORIGIN`, `MCP_AUTH_TYPE`
(`bearer` or `api-key`; enables auth) and `MCP_AUTH_SECRET`. Neither sets
`allowNonLoopback`, so a non-loopback `--host` still needs that flag in code.

## Client

```cpp
#include <SocketsHpp/mcp/client/mcp_client.h>
#include <iostream>

using namespace SocketsHpp::mcp;
using json = nlohmann::json;

int main()
{
    ClientConfig config;
    config.transport = TransportType::HTTP_STREAMABLE;
    config.http.url = "http://127.0.0.1:8080/mcp";
    config.http.headers["Authorization"] = "Bearer expected-token";
    config.http.timeoutSeconds = 30;   // per-request read timeout
    config.connectTimeoutSeconds = 10;

    client::MCPClient client;
    client.onStatus([](bool connected, const std::string& message) {
        std::cout << (connected ? "up: " : "down: ") << message << '\n';
    });
    client.onNotification("notifications/progress", [](const json& params) {
        std::cout << "progress " << params.value("progress", 0.0) << '\n';
    });

    if (!client.connect(config))  // false for STDIO; HTTP connects lazily
        return 1;

    try
    {
        json init = client.initialize({{"name", "weather-client"}, {"version", "1.0.0"}});
        std::cout << "server: " << init["serverInfo"].dump() << '\n';
        for (const auto& tool : client.listTools())
            std::cout << "tool: " << tool.value("name", "") << '\n';
        json result = client.callTool("get_weather", {{"city", "Oslo"}});
        std::cout << result.dump(2) << '\n';
    }
    catch (const SocketsHpp::http::common::JsonRpcError& e)
    {
        std::cerr << "JSON-RPC error " << e.code << ": " << e.message << '\n';
    }
    catch (const std::exception& e)
    {
        std::cerr << "transport error: " << e.what() << '\n';
    }

    client.disconnect();
}
```

- `connect(config)` accepts `HTTP` and `HTTP_STREAMABLE`. Nothing is sent until
  `initialize(clientInfo)`, which sends `initialize` (protocol version `2025-03-26` for
  `HTTP_STREAMABLE`, `2024-11-05` for `HTTP`), stores the `Mcp-Session-Id`, sends
  `notifications/initialized` and returns the server's result.
- Request helpers: `ping()`, `listTools()`, `callTool(name, args)`, `listPrompts()`,
  `getPrompt(name, args)`, `listResources()`, `readResource(uri)`,
  `subscribeResource(uri)`, `unsubscribeResource(uri)`, `listResourceTemplates()`.
  The `list*` helpers return the array (`tools`, `prompts`, ...); the others return the
  `result` object. Request ids are sequential integers.
- Responses may be `application/json` or `text/event-stream`; notifications that
  arrive in an SSE response before the result are dispatched to the handlers.
- `onNotification(method, handler)` registers a handler and opens the server's
  notification stream (GET with `Mcp-Session-Id` and the configured headers, using
  `SSEClient` with auto-reconnect and `Last-Event-ID`). The stream is also opened after
  `initialize()` when `config.http.enableResumability` is set.
- Errors: a JSON-RPC error response throws `SocketsHpp::http::common::JsonRpcError`
  (a plain struct with `code`, `message`, `data`, not derived from `std::exception`);
  HTTP failures or non-200 status codes throw `std::runtime_error`.
- `disconnect()` (also run by the destructor) stops the notification stream and sends
  `DELETE` to end the session.
- `sessionId()`, `isConnected()` and `getServerCapabilities()` expose state.
- `ClientConfig::maxRetries`, `retryBackoffMs` and `readTimeoutSeconds` are not used
  by the current client; use `http.timeoutSeconds` for the read timeout.

### Loading VS Code `mcp.json` entries

```cpp
#include <SocketsHpp/mcp/common/mcp_config.h>
#include <fstream>

SocketsHpp::mcp::ClientConfig loadServer(const std::string& path, const std::string& name)
{
    std::ifstream file(path);
    nlohmann::json mcp = nlohmann::json::parse(file);
    return SocketsHpp::mcp::ClientConfig::fromJson(mcp["servers"][name]);
}
```

`fromJson()` maps `"type"`: `stdio` to `STDIO` (fills `stdio.command`, `args`, `env`,
`envFile`, `cwd`), `http` and `sse` to `HTTP`, `streamable` and `http-streamable` to
`HTTP_STREAMABLE` (fills `http.url`, `headers`, `timeout`), and throws
`std::invalid_argument` for anything else. VS Code `${input:...}` variables are not
expanded, and STDIO configurations can be parsed but not connected.

## JSON-RPC layer

`json_rpc.h` can be used on its own:

```cpp
#include <SocketsHpp/http/common/json_rpc.h>

using namespace SocketsHpp::http::common;

void jsonRpcDemo()
{
    JsonRpcRequest req;
    req.id = std::int64_t{42};  // or std::string("abc"), or nullptr
    req.method = "tools/list";
    req.params = json::object();
    std::string wire = req.serialize();  // {"id":42,"jsonrpc":"2.0","method":"tools/list","params":{}}

    JsonRpcRequest parsed = JsonRpcRequest::parse(wire);  // throws on invalid JSON or id
    bool isNotification = !parsed.hasId();

    JsonRpcResponse ok = JsonRpcResponse::success(parsed.id, {{"tools", json::array()}});
    JsonRpcResponse err = JsonRpcResponse::failure(parsed.id, JsonRpcError::methodNotFound("x"));
    JsonRpcError custom = JsonRpcError::serverError(-32000, "backend unavailable");
    custom.data = json{{"retryAfter", 5}};
    (void)isNotification; (void)ok; (void)err;
}
```

`JsonRpcId` is `std::variant<std::monostate, std::string, std::int64_t, std::nullptr_t>`,
where `monostate` means "no id" (a notification). `jsonRpcIdFromJson()` /
`jsonRpcIdToJson()` convert to and from JSON.

## Deployment behind nginx

TLS, OAuth flows, compression and load balancing are best handled by a reverse proxy.
Keep the server on loopback and disable proxy buffering so SSE is delivered as it is
produced:

```nginx
location /mcp {
    proxy_pass         http://127.0.0.1:8080;
    proxy_http_version 1.1;
    proxy_set_header   Connection "";
    proxy_set_header   Host $host;
    proxy_set_header   X-Forwarded-For $remote_addr;
    proxy_buffering    off;       # required for SSE
    proxy_read_timeout 3600s;     # long-lived notification streams
}
```

## Tests

With `-DSOCKETSHPP_BUILD_TESTS=ON` (and nlohmann/json available):

| Binary | Covers |
|--------|--------|
| `build/test/json_rpc_test`, `json_rpc_minimal_test` | JSON-RPC types and ids |
| `build/test/mcp_config_test` | `ServerConfig` / `ClientConfig` parsing |
| `build/test/mcp_streamable_test` (POSIX only) | Server over both HTTP transports, sessions, batching, auth, rate limiting, cancellation, resumability, client |

```bash
ctest --test-dir build -R "JsonRpc|MCPConfig|Mcp|StreamableHttp" --output-on-failure
```

## References

- [MCP specification](https://modelcontextprotocol.io/specification)
- [JSON-RPC 2.0](https://www.jsonrpc.org/specification)
- [VS Code MCP servers](https://code.visualstudio.com/docs/copilot/customization/mcp-servers)
- [jwt-cpp](https://github.com/Thalhammer/jwt-cpp), [nlohmann/json](https://github.com/nlohmann/json)
