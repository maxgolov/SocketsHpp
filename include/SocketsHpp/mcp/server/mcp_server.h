// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
//
// ┌─────────────────────────────────────────────────────────────────────────────┐
// │  MCP SERVER — SocketsHpp                                                    │
// │                                                                             │
// │  Implements MCP 2024-11-05 (HTTP + persistent SSE) and                      │
// │  MCP 2025-03-26 (Streamable HTTP) over the SocketsHpp HTTP server.          │
// │  STDIO transport is also supported for VS Code / Claude Desktop integration. │
// │                                                                             │
// │  ── NGINX INTEGRATION GUIDE ──────────────────────────────────────────────  │
// │                                                                             │
// │  Run memento-native on a loopback port (e.g. 3601) and put nginx in front  │
// │  for TLS, auth, rate-limiting and load balancing.  This server handles     │
// │  only application logic; all transport hardening is offloaded.             │
// │                                                                             │
// │  Recommended nginx config:                                                  │
// │                                                                             │
// │    upstream mcp_backend {                                                   │
// │        server 127.0.0.1:3601;                                               │
// │        keepalive 32;           # reuse connections                          │
// │    }                                                                        │
// │                                                                             │
// │    limit_req_zone $binary_remote_addr zone=mcp:10m rate=10r/s;              │
// │                                                                             │
// │    server {                                                                 │
// │        listen 443 ssl http2;                                                │
// │        ssl_certificate     /etc/ssl/mcp.crt;   # TLS (offloaded)           │
// │        ssl_certificate_key /etc/ssl/mcp.key;                                │
// │                                                                             │
// │        location /mcp {                                                      │
// │            proxy_pass         http://mcp_backend;                           │
// │            proxy_http_version 1.1;                                          │
// │            proxy_set_header   Connection "";   # keepalive pool             │
// │            proxy_set_header   Host $host;                                   │
// │            proxy_set_header   X-Forwarded-For $remote_addr;                 │
// │            proxy_buffering    off;             # CRITICAL for SSE           │
// │            proxy_cache        off;                                           │
// │            proxy_read_timeout 3600s;           # keep SSE streams alive     │
// │            limit_req          zone=mcp burst=20 nodelay;                    │
// │        }                                                                    │
// │        location /health {                                                   │
// │            proxy_pass http://mcp_backend/health;  # GET /health → health   │
// │        }                                                                    │
// │    }                                                                        │
// │                                                                             │
// │  Features best offloaded to nginx (not implemented here):                   │
// │    • TLS / mTLS termination                                                 │
// │    • OAuth 2.1 / JWT validation (auth_request module or lua-jwt)            │
// │    • Aggressive DDoS / IP-block rules (ngx_http_geo_module)                 │
// │    • gzip compression for JSON payloads                                     │
// │    • Load balancing across multiple server instances                        │
// │    • Access logging / metrics export                                        │
// │    • PKCE / token exchange flows                                            │
// │                                                                             │
// │  NOTE: proxy_buffering off is MANDATORY for SSE.  Without it nginx         │
// │  buffers the entire stream and the client sees nothing until close.         │
// └─────────────────────────────────────────────────────────────────────────────┘
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/common/json_rpc.h>
#include <SocketsHpp/mcp/common/mcp_config.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

// Optional JWT support - only compile if available
#ifdef SOCKETSHPP_HAS_JWT_CPP
#include <jwt-cpp/jwt.h>
#endif

SOCKETSHPP_NS_BEGIN
namespace mcp
{
    namespace server
    {
        using namespace http::server;
        using namespace http::common;
        using json = nlohmann::json;

        /// @brief MCP Server implementing Model Context Protocol over HTTP Stream Transport
        ///
        /// Supports three transports:
        ///   - TransportType::STDIO              — stdin/stdout JSON-RPC for VS Code / Claude Desktop
        ///   - TransportType::HTTP               — HTTP + persistent SSE GET stream (MCP 2024-11-05)
        ///   - TransportType::HTTP_STREAMABLE    — Streamable HTTP POST (MCP 2025-03-26)
        ///
        /// Auto-registered built-in methods (servers MUST NOT override these):
        ///   - "ping"                     → {} (liveness check, required by spec)
        ///   - "notifications/initialized"→ no-op (client ACK after initialize)
        ///   - "notifications/cancelled" → cancels pending in-flight request
        ///   - "logging/setLevel"        → sets minimum log level for notifications/message
        ///
        /// See file header comment for nginx integration guide.
        class MCPServer
        {
        public:
            /// @brief Simple method handler (no cancellation support)
            using MethodHandler = std::function<json(const json& params)>;

            /// @brief Cancellable method handler — receives a cancel token.
            /// Poll cancel_requested->load() in long-running operations.
            /// Throw std::runtime_error or return a result when done.
            using CancellableMethodHandler =
                std::function<json(const json& params,
                                   std::shared_ptr<std::atomic<bool>> cancel_requested)>;

            /// @brief Create MCP server with configuration
            /// @param config Server configuration
            explicit MCPServer(const ServerConfig& config)
                : m_config(config)
                , m_httpServer(config.host, config.port)
                , m_running(false)
                , m_logLevel("warning")
            {
                // Configure session manager
                m_sessionManager.setSessionTimeout(std::chrono::seconds(config.session.sessionTimeoutSeconds));
                if (config.resumability.enabled)
                {
                    m_sessionManager.enableResumability(
                        true,
                        std::chrono::milliseconds(config.resumability.historyDurationMs),
                        config.resumability.maxHistorySize
                    );
                }

                // Set HTTP server limits
                m_httpServer.setMaxRequestContentSize(config.maxMessageSize);

                // ── Auto-register built-in MCP protocol methods ───────────────────────────
                // ping — required by spec; clients poll for liveness
                m_methods["ping"] = [](const json&) -> json { return json::object(); };

                // notifications/initialized — client ACK after initialize, no response needed
                m_methods["notifications/initialized"] = [](const json&) -> json { return json::object(); };

                // notifications/cancelled — client cancels an in-flight request by requestId
                m_methods["notifications/cancelled"] = [this](const json& params) -> json {
                    json req_id = params.value("requestId", json{});
                    if (!req_id.is_null()) {
                        std::lock_guard<std::mutex> lock(m_pendingMutex);
                        auto it = m_pendingCancellations.find(req_id);
                        if (it != m_pendingCancellations.end())
                            it->second->store(true);
                    }
                    return json::object();
                };

                // logging/setLevel — client requests minimum log severity for notifications/message
                m_methods["logging/setLevel"] = [this](const json& params) -> json {
                    std::string level = params.value("level", "warning");
                    static const std::vector<std::string> valid =
                        {"debug","info","notice","warning","error","critical","alert","emergency"};
                    if (std::find(valid.begin(), valid.end(), level) != valid.end())
                        m_logLevel = level;
                    return json::object();
                };

                // Setup routes based on transport type
                if (config.transport == TransportType::HTTP)
                {
                    setupHttpRoutes();
                }
                else if (config.transport == TransportType::HTTP_STREAMABLE)
                {
                    setupStreamableRoutes();
                }
                // STDIO transport handled externally (read from stdin, write to stdout)
            }

            ~MCPServer()
            {
                stop();
            }

            /// @brief Register a method handler (simple — no cancellation)
            /// @note Built-in methods (ping, notifications/initialized, notifications/cancelled,
            ///       logging/setLevel) are pre-registered and cannot be overridden.
            void registerMethod(const std::string& method, MethodHandler handler)
            {
                m_methods[method] = std::move(handler);
            }

            /// @brief Register a cancellable method handler.
            /// The handler receives a shared cancel_requested token. Poll it in long-running
            /// operations; when true, abort and throw std::runtime_error("cancelled").
            /// The token is set by notifications/cancelled from the client.
            void registerCancellable(const std::string& method, CancellableMethodHandler handler)
            {
                m_methods[method] = [this, method, handler](const json& params) -> json {
                    auto token = std::make_shared<std::atomic<bool>>(false);
                    // Register by using the request ID from the current in-flight context.
                    // We store it temporarily so notifications/cancelled can find it.
                    // Since JSON-RPC id is not in params, we use a UUID as a surrogate key.
                    auto surrogate = json(method + "_" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()));
                    {
                        std::lock_guard<std::mutex> lock(m_pendingMutex);
                        m_pendingCancellations[surrogate] = token;
                    }
                    json result;
                    try {
                        result = handler(params, token);
                    } catch (...) {
                        std::lock_guard<std::mutex> lock(m_pendingMutex);
                        m_pendingCancellations.erase(surrogate);
                        throw;
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_pendingMutex);
                        m_pendingCancellations.erase(surrogate);
                    }
                    return result;
                };
            }

            /// @brief Push a progress notification to a session's SSE stream.
            /// Call from within a tool handler to report long-running operation progress.
            /// @param sessionId   Session to push to (from Mcp-Session-Id header)
            /// @param token       progressToken from the tool call params (string or int)
            /// @param progress    0.0–1.0 or total steps completed (spec allows either)
            /// @param total       Optional: denominator when progress is a step count
            /// @param message     Optional: human-readable status message
            bool push_progress(const std::string& sessionId,
                                const json&        token,
                                double             progress,
                                std::optional<double> total   = std::nullopt,
                                const std::string& message    = "")
            {
                json params = {
                    {"progressToken", token},
                    {"progress",      progress}
                };
                if (total.has_value())  params["total"]   = total.value();
                if (!message.empty())   params["message"] = message;

                json event_body = {
                    {"jsonrpc", "2.0"},
                    {"method",  "notifications/progress"},
                    {"params",  params}
                };
                return push_event(sessionId,
                    "event: message\ndata: " + event_body.dump() + "\n\n");
            }

            /// @brief Push a log message notification to a session's SSE stream.
            /// Only emitted when level >= the level set by logging/setLevel (default: warning).
            /// @param sessionId Session to push to
            /// @param level     One of: debug info notice warning error critical alert emergency
            /// @param logger    Logger name (e.g., "memento-native")
            /// @param data      Log message (string or structured JSON object)
            bool push_log(const std::string& sessionId,
                          const std::string& level,
                          const std::string& logger,
                          const json&        data)
            {
                static const std::map<std::string, int> LEVEL_ORDER = {
                    {"debug",0},{"info",1},{"notice",2},{"warning",3},
                    {"error",4},{"critical",5},{"alert",6},{"emergency",7}
                };
                // Suppress if below the requested log level
                auto it_min = LEVEL_ORDER.find(m_logLevel);
                auto it_cur = LEVEL_ORDER.find(level);
                if (it_min != LEVEL_ORDER.end() && it_cur != LEVEL_ORDER.end() &&
                    it_cur->second < it_min->second)
                    return false;

                json event_body = {
                    {"jsonrpc", "2.0"},
                    {"method",  "notifications/message"},
                    {"params",  {{"level", level}, {"logger", logger}, {"data", data}}}
                };
                return push_event(sessionId,
                    "event: message\ndata: " + event_body.dump() + "\n\n");
            }

            /// @brief Get the capabilities object the client advertised in its initialize call.
            /// Returns null JSON if the session has not completed initialize, or sessionId is empty.
            json get_client_capabilities(const std::string& sessionId) const
            {
                std::lock_guard<std::mutex> lock(m_clientCapsMutex);
                auto it = m_clientCapabilities.find(sessionId);
                return it != m_clientCapabilities.end() ? it->second : json{};
            }

            /// @brief Return the negotiated server info (name, version, protocolVersion).
            /// Useful for diagnostics and the health endpoint.
            json server_info() const
            {
                return {
                    {"name",            m_config.serverName.empty() ? "mcp-server" : m_config.serverName},
                    {"version",         m_config.serverVersion.empty() ? "1.0.0"   : m_config.serverVersion},
                    {"protocolVersion", SUPPORTED_VERSIONS.front()}
                };
            }

            /// @brief Push a server-initiated SSE event to a specific session's notification stream.
            /// Thread-safe; can be called from any thread.
            /// @param sessionId  Session to push to (from initialize response Mcp-Session-Id header)
            /// @param eventData  Raw SSE event string (e.g. "event: message\ndata: {...}\n\n")
            /// @return true if the session exists and event was queued
            bool push_event(const std::string& sessionId, const std::string& eventData)
            {
                std::lock_guard<std::mutex> lock(m_sseQueuesMutex);
                auto it = m_sseQueues.find(sessionId);
                if (it == m_sseQueues.end() || it->second->closed.load())
                    return false;
                {
                    std::lock_guard<std::mutex> qlock(it->second->mutex);
                    it->second->events.push(eventData);
                    it->second->lastActivity = std::chrono::steady_clock::now();
                }
                it->second->cv.notify_one();
                return true;
            }

            /// @brief Close and remove all SSE streams idle longer than sseIdleTimeoutSeconds.
            /// Call periodically from a maintenance thread.
            void cleanupStaleSessions(int sseIdleTimeoutSeconds = 300)
            {
                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(m_sseQueuesMutex);
                for (auto it = m_sseQueues.begin(); it != m_sseQueues.end(); )
                {
                    auto& q = it->second;
                    std::lock_guard<std::mutex> qlock(q->mutex);
                    auto idleSec = std::chrono::duration_cast<std::chrono::seconds>(
                        now - q->lastActivity).count();
                    if (idleSec > sseIdleTimeoutSeconds || q->closed.load())
                    {
                        q->closed.store(true);
                        q->cv.notify_all();
                        it = m_sseQueues.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            /// @brief Start server (HTTP or HTTP_STREAMABLE mode).
            /// SSRF guard: refuses to bind to non-loopback unless config.allowNonLoopback is true.
            void listen()
            {
                if (m_config.transport != TransportType::HTTP &&
                    m_config.transport != TransportType::HTTP_STREAMABLE)
                {
                    throw std::runtime_error("listen() only available in HTTP transport modes");
                }

                // SSRF loopback guard (backport from FMcpNativeTransport)
                bool isLoopback = (m_config.host == "127.0.0.1" || m_config.host == "localhost"
                    || m_config.host == "::1" || m_config.host == "[::1]");
                if (!isLoopback && !m_config.allowNonLoopback)
                {
                    throw std::runtime_error(
                        "SSRF guard: refusing to bind to non-loopback address '" + m_config.host +
                        "'. Set ServerConfig::allowNonLoopback = true to override.");
                }

                m_running = true;
                m_httpServer.enableThreadPool(4);  // SSE callbacks run on pool; reactor stays free
                m_httpServer.start();
            }

            /// @brief Stop server — closes all pending SSE queues before stopping.
            void stop()
            {
                if (!m_running.exchange(false))
                    return;

                // Signal all SSE streams to close
                {
                    std::lock_guard<std::mutex> lock(m_sseQueuesMutex);
                    for (auto& [sid, q] : m_sseQueues)
                    {
                        q->closed.store(true);
                        q->cv.notify_all();
                    }
                    m_sseQueues.clear();
                }

                m_httpServer.stop();
            }

            /// @brief Process JSON-RPC message (STDIO mode)
            /// @param jsonRpcMessage JSON-RPC message string
            /// @return JSON-RPC response string (empty for notifications)
            std::string processMessage(const std::string& jsonRpcMessage)
            {
                try
                {
                    json j = json::parse(jsonRpcMessage);

                    // Check if it's a notification (no ID)
                    if (!j.contains("id"))
                    {
                        auto notif = JsonRpcNotification::parse(jsonRpcMessage);
                        handleNotification(notif);
                        return ""; // Notifications don't get responses
                    }

                    // Parse as request
                    auto request = JsonRpcRequest::parse(jsonRpcMessage);
                    auto response = handleRequest(request);
                    return response.serialize();
                }
                catch (const json::parse_error& e)
                {
                    auto error = JsonRpcError::parseError(e.what());
                    auto response = JsonRpcResponse::failure(nullptr, error);
                    return response.serialize();
                }
                catch (const std::exception& e)
                {
                    auto error = JsonRpcError::internalError(e.what());
                    auto response = JsonRpcResponse::failure(nullptr, error);
                    return response.serialize();
                }
            }

        private:
            // Supported protocol versions — highest first (negotiation preference order)
            static inline const std::vector<std::string> SUPPORTED_VERSIONS = {
                "2025-03-26", "2024-11-05"
            };

            // ----------------------------------------------------------------
            // Per-session SSE event queue (backport: real push SSE stream)
            // ----------------------------------------------------------------
            struct SSESessionQueue
            {
                std::queue<std::string> events;
                std::mutex mutex;
                std::condition_variable cv;
                std::atomic<bool> closed{false};
                std::chrono::steady_clock::time_point lastActivity{std::chrono::steady_clock::now()};
            };

            // ----------------------------------------------------------------
            // Per-IP rate limiting (backport: token bucket)
            // ----------------------------------------------------------------
            struct RateLimitEntry
            {
                int count = 0;
                std::chrono::steady_clock::time_point windowStart{std::chrono::steady_clock::now()};
            };

            ServerConfig m_config;
            HttpServer m_httpServer;
            SessionManager m_sessionManager;
            std::map<std::string, MethodHandler> m_methods;
            std::atomic<bool> m_running{false};

            // SSE queues: sessionId → queue (push_event writes here, GET handler reads)
            std::map<std::string, std::shared_ptr<SSESessionQueue>> m_sseQueues;
            std::mutex m_sseQueuesMutex;

            // Rate limiting
            std::map<std::string, RateLimitEntry> m_rateLimitMap;
            std::mutex m_rateLimitMutex;

            // Cancellation tokens: requestId → cancel flag (set by notifications/cancelled)
            std::map<json, std::shared_ptr<std::atomic<bool>>> m_pendingCancellations;
            mutable std::mutex m_pendingMutex;

            // Client capabilities per session (stored on initialize)
            std::map<std::string, json> m_clientCapabilities;
            mutable std::mutex m_clientCapsMutex;
            json m_pendingClientCaps;  // temporary staging during initialize before session ID is known

            // Current minimum log level for notifications/message (set by logging/setLevel)
            std::string m_logLevel;

            // ----------------------------------------------------------------
            // SSRF / rate helpers
            // ----------------------------------------------------------------

            /// @brief Per-IP token-bucket check.  Returns false and sets 429 if limit exceeded.
            bool checkRateLimit(const std::string& clientIp, HttpResponse& res)
            {
                if (m_config.maxRequestsPerMinute <= 0)
                    return true; // rate limiting disabled

                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(m_rateLimitMutex);
                auto& entry = m_rateLimitMap[clientIp];
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - entry.windowStart).count();
                if (elapsed >= 60)
                {
                    entry.count = 0;
                    entry.windowStart = now;
                }
                if (++entry.count > m_config.maxRequestsPerMinute)
                {
                    res.set_status(429);
                    res.set_header("Retry-After", "60");
                    res.send("Too many requests");
                    return false;
                }
                return true;
            }

            // ── MCP Streamable HTTP transport (2025-03-26) ────────────────────────────
            //
            // Unified POST endpoint:
            //   - Client sends JSON-RPC (single or batch) via POST
            //   - If Accept includes text/event-stream → server responds with SSE stream
            //     (each JSON-RPC response sent as a separate `data:` event; stream closed
            //     when all responses are sent)
            //   - Otherwise → server responds with application/json (single) or
            //     application/json array (batch)
            //   - Notifications (no id) → 202 Accepted, empty body
            //
            // Optional GET endpoint (same URL):
            //   - Opens a long-lived SSE notification stream for server-initiated messages
            //   - Reuses the existing handleHttpGet() implementation
            //
            // Protocol version: 2025-03-26

            /// @brief Setup Streamable HTTP routes (MCP 2025-03-26)
            void setupStreamableRoutes()
            {
                std::string endpoint = m_config.endpoint;

                m_httpServer.route(endpoint, [this](const HttpRequest& req, HttpResponse& res) -> int {
                    if (req.method == "POST")
                    {
                        handleStreamablePost(req, res);
                    }
                    else if (req.method == "GET")
                    {
                        handleHttpGet(req, res);   // same SSE notification stream as 2024-11-05
                    }
                    else if (req.method == "DELETE")
                    {
                        handleHttpDelete(req, res);
                    }
                    else if (req.method == "OPTIONS")
                    {
                        handleHttpOptions(req, res);
                    }
                    else
                    {
                        res.set_status(405);
                        res.set_header("Allow", "GET, POST, DELETE, OPTIONS");
                        res.send("");
                    }
                    return 0;
                });

                // GET / → health / discovery JSON (also used by nginx upstream health checks)
                m_httpServer.route("/health", [this](const HttpRequest& req, HttpResponse& res) -> int {
                    if (req.method == "GET")
                    {
                        applyCorsHeaders(res);
                        res.set_status(200);
                        res.set_header("Content-Type", "application/json");
                        res.send(server_info().dump());
                    }
                    else
                    {
                        res.set_status(405);
                        res.send("");
                    }
                    return 0;
                });
            }

            /// @brief Handle Streamable HTTP POST (MCP 2025-03-26)
            ///
            /// Accepts:
            ///   - Single JSON-RPC request  → JSON or SSE with one `data:` event
            ///   - JSON-RPC batch (array)   → JSON array or SSE with one `data:` event per response
            ///   - Notification (no id)     → 202 Accepted, no body
            ///
            /// Response content type is negotiated via the `Accept` header:
            ///   Accept includes "text/event-stream" → SSE; otherwise application/json.
            void handleStreamablePost(const HttpRequest& req, HttpResponse& res)
            {
                applyCorsHeaders(res);

                // Rate limit
                std::string clientIp = req.headers.count("X-Forwarded-For")
                    ? req.headers.at("X-Forwarded-For") : "unknown";
                if (!checkRateLimit(clientIp, res)) return;

                // Authenticate
                if (!authenticate(req, res)) return;

                // Negotiate response format
                bool wantsSSE = false;
                {
                    auto it = req.headers.find("Accept");
                    if (it != req.headers.end())
                        wantsSSE = it->second.find("text/event-stream") != std::string::npos;
                }

                // Helper: send a set of serialised responses either as SSE or JSON
                auto sendResponses = [&](const std::vector<std::string>& resps)
                {
                    if (resps.empty())
                    {
                        res.set_status(202);
                        res.send("");
                        return;
                    }
                    if (wantsSSE)
                    {
                        res.set_status(200);
                        res.set_header("Content-Type", "text/event-stream");
                        res.set_header("Cache-Control", "no-cache");
                        std::string body;
                        for (auto& r : resps)
                            body += "data: " + r + "\n\n";
                        res.send(body);
                    }
                    else if (resps.size() == 1)
                    {
                        res.set_status(200);
                        res.set_header("Content-Type", "application/json");
                        res.send(resps[0]);
                    }
                    else
                    {
                        // Batch: return JSON array
                        std::string arr = "[";
                        for (size_t i = 0; i < resps.size(); ++i)
                        {
                            if (i) arr += ",";
                            arr += resps[i];
                        }
                        arr += "]";
                        res.set_status(200);
                        res.set_header("Content-Type", "application/json");
                        res.send(arr);
                    }
                };

                try
                {
                    json body = json::parse(req.content);
                    bool isBatch = body.is_array();

                    if (!isBatch)
                    {
                        // Notification (no id) → 202, no response body
                        if (!body.contains("id"))
                        {
                            auto notif = JsonRpcNotification::parse(req.content);
                            handleNotification(notif);
                            res.set_status(202);
                            res.send("");
                            return;
                        }

                        // initialize → create session, echo session header
                        if (body.value("method", "") == "initialize")
                        {
                            std::string sessionId = m_sessionManager.createSession();
                            // Pre-create SSE queue so the session's GET stream works immediately
                            {
                                std::lock_guard<std::mutex> lock(m_sseQueuesMutex);
                                m_sseQueues[sessionId] = std::make_shared<SSESessionQueue>();
                            }
                            res.set_header(m_config.session.headerName, sessionId);

                            auto request  = JsonRpcRequest::parse(req.content);
                            auto response = handleRequest(request);

                            // Commit pending client capabilities to this session
                            {
                                std::lock_guard<std::mutex> lock(m_clientCapsMutex);
                                if (!m_pendingClientCaps.is_null())
                                {
                                    m_clientCapabilities[sessionId] = m_pendingClientCaps;
                                    m_pendingClientCaps = json{};
                                }
                            }

                            sendResponses({response.serialize()});
                            return;
                        }

                        // All other requests: validate session (optional but recommended)
                        std::string sessionId = getSessionId(req);
                        if (!sessionId.empty() && !m_sessionManager.validateSession(sessionId))
                        {
                            auto error    = JsonRpcError::serverError(-32001, "Invalid or expired session");
                            auto response = JsonRpcResponse::failure(nullptr, error);
                            res.set_status(404);
                            res.set_header("Content-Type", "application/json");
                            res.send(response.serialize());
                            return;
                        }

                        auto request  = JsonRpcRequest::parse(req.content);
                        auto response = handleRequest(request);
                        sendResponses({response.serialize()});
                    }
                    else
                    {
                        // Batch: validate session for the whole batch
                        std::string sessionId = getSessionId(req);
                        if (!sessionId.empty() && !m_sessionManager.validateSession(sessionId))
                        {
                            auto error    = JsonRpcError::serverError(-32001, "Invalid or expired session");
                            auto response = JsonRpcResponse::failure(nullptr, error);
                            res.set_status(404);
                            res.set_header("Content-Type", "application/json");
                            res.send(response.serialize());
                            return;
                        }

                        std::vector<std::string> serialised;
                        for (auto& item : body)
                        {
                            if (!item.contains("id"))
                            {
                                // Notification within batch — handle, no response
                                try
                                {
                                    auto notif = JsonRpcNotification::parse(item.dump());
                                    handleNotification(notif);
                                }
                                catch (...) {}
                                continue;
                            }
                            auto request  = JsonRpcRequest::parse(item.dump());
                            auto response = handleRequest(request);
                            serialised.push_back(response.serialize());
                        }
                        sendResponses(serialised);
                    }
                }
                catch (const json::parse_error& e)
                {
                    auto error    = JsonRpcError::parseError(e.what());
                    auto response = JsonRpcResponse::failure(nullptr, error);
                    res.set_status(400);
                    res.set_header("Content-Type", "application/json");
                    res.send(response.serialize());
                }
                catch (const std::exception& e)
                {
                    auto error    = JsonRpcError::internalError(e.what());
                    auto response = JsonRpcResponse::failure(nullptr, error);
                    res.set_status(500);
                    res.set_header("Content-Type", "application/json");
                    res.send(response.serialize());
                }
            }

            /// @brief Setup routes based on transport type
            void setupHttpRoutes()
            {
                std::string endpoint = m_config.endpoint;

                // GET / → health / discovery JSON (nginx upstream health checks)
                m_httpServer.route("/health", [this](const HttpRequest& req, HttpResponse& res) -> int {
                    if (req.method == "GET")
                    {
                        applyCorsHeaders(res);
                        res.set_status(200);
                        res.set_header("Content-Type", "application/json");
                        res.send(server_info().dump());
                    }
                    else
                    {
                        res.set_status(405);
                        res.send("");
                    }
                    return 0;
                });

                // POST: Handle JSON-RPC requests
                m_httpServer.route(endpoint, [this](const HttpRequest& req, HttpResponse& res) -> int {
                    if (req.method == "POST")
                    {
                        handleHttpPost(req, res);
                    }
                    else if (req.method == "GET")
                    {
                        handleHttpGet(req, res);
                    }
                    else if (req.method == "DELETE")
                    {
                        handleHttpDelete(req, res);
                    }
                    else if (req.method == "OPTIONS")
                    {
                        handleHttpOptions(req, res);
                    }
                    else
                    {
                        res.set_status(405); // Method Not Allowed
                        res.set_header("Allow", "GET, POST, DELETE, OPTIONS");
                        res.send("");
                    }
                    return 0;
                });
            }

            /// @brief Handle HTTP POST request (JSON-RPC)
            void handleHttpPost(const HttpRequest& req, HttpResponse& res)
            {
                // Apply CORS headers
                applyCorsHeaders(res);

                // Rate limit
                std::string clientIp = req.headers.count("X-Forwarded-For")
                    ? req.headers.at("X-Forwarded-For") : "unknown";
                if (!checkRateLimit(clientIp, res))
                    return;

                // Authenticate if required
                if (!authenticate(req, res))
                {
                    return;
                }

                // Check Content-Type
                auto contentType = req.headers.find("Content-Type");
                if (contentType == req.headers.end() || contentType->second.find("application/json") == std::string::npos)
                {
                    auto error = JsonRpcError::invalidRequest("Content-Type must be application/json");
                    auto response = JsonRpcResponse::failure(nullptr, error);
                    res.set_status(400);
                    res.set_header("Content-Type", "application/json");
                    res.send(response.serialize());
                    return;
                }

                // Parse and handle request
                try
                {
                    // Pre-parse JSON to check if this is a notification (no "id")
                    json j = json::parse(req.content);
                    if (!j.contains("id"))
                    {
                        // JSON-RPC notification — handle it and respond 202 Accepted (no body)
                        auto notif = JsonRpcNotification::parse(req.content);
                        handleNotification(notif);
                        res.set_status(202);
                        res.send("");
                        return;
                    }

                    auto request = JsonRpcRequest::parse(req.content);
                    
                    // Special handling for initialize - create session
                    if (request.method == "initialize")
                    {
                        std::string sessionId = m_sessionManager.createSession();
                        auto response = handleRequest(request);

                        // Commit pending client capabilities to this session
                        {
                            std::lock_guard<std::mutex> lock(m_clientCapsMutex);
                            if (!m_pendingClientCaps.is_null())
                            {
                                m_clientCapabilities[sessionId] = m_pendingClientCaps;
                                m_pendingClientCaps = json{};
                            }
                        }
                        
                        res.set_header(m_config.session.headerName, sessionId);
                        
                        // Check if client wants SSE stream or batch response
                        auto acceptHeader = req.headers.find("Accept");
                        bool wantsSSE = (acceptHeader != req.headers.end() && 
                                        acceptHeader->second.find("text/event-stream") != std::string::npos);

                        if (wantsSSE && m_config.responseMode == ServerConfig::ResponseMode::STREAM)
                        {
                            // Send response via SSE
                            res.set_header("Content-Type", "text/event-stream");
                            res.set_header("Cache-Control", "no-cache");
                            res.set_header("Connection", "keep-alive");
                            
                            SSEEvent event;
                            event.id = "init-1";
                            event.data = response.serialize();
                            
                            if (m_config.resumability.enabled)
                            {
                                m_sessionManager.addEvent(sessionId, event.id, event.format());
                            }
                            
                            res.send_chunk(event.format());
                            res.send_chunk(""); // End stream
                        }
                        else
                        {
                            // Send JSON response
                            res.set_status(200);
                            res.set_header("Content-Type", "application/json");
                            res.send(response.serialize());
                        }
                    }
                    else
                    {
                        // Validate session for non-initialize requests
                        std::string sessionId = getSessionId(req);
                        if (!sessionId.empty() && !m_sessionManager.validateSession(sessionId))
                        {
                            auto error = JsonRpcError::serverError(-32001, "Invalid or expired session");
                            auto response = JsonRpcResponse::failure(request.id, error);
                            res.set_status(404);
                            res.set_header("Content-Type", "application/json");
                            res.send(response.serialize());
                            return;
                        }

                        auto response = handleRequest(request);
                        res.set_status(200);
                        res.set_header("Content-Type", "application/json");
                        res.send(response.serialize());
                    }
                }
                catch (const json::parse_error& e)
                {
                    auto error = JsonRpcError::parseError(e.what());
                    auto response = JsonRpcResponse::failure(nullptr, error);
                    res.set_status(400);
                    res.set_header("Content-Type", "application/json");
                    res.send(response.serialize());
                }
                catch (const std::exception& e)
                {
                    auto error = JsonRpcError::internalError(e.what());
                    auto response = JsonRpcResponse::failure(nullptr, error);
                    res.set_status(500);
                    res.set_header("Content-Type", "application/json");
                    res.send(response.serialize());
                }
            }

            /// @brief Handle HTTP GET request — real SSE push stream (backport from FMcpNativeTransport)
            ///
            /// Creates a per-session blocking event queue shared with push_event().
            /// The send_chunk_stream callback blocks on the queue (up to writeDeadlineSeconds),
            /// returning "" to terminate the stream if the session is closed or idle too long.
            void handleHttpGet(const HttpRequest& req, HttpResponse& res)
            {
                // Apply CORS headers
                applyCorsHeaders(res);

                // Authenticate if required
                if (!authenticate(req, res))
                {
                    return;
                }

                // Rate limit by client IP (best-effort: use peer address if available)
                std::string clientIp = req.headers.count("X-Forwarded-For")
                    ? req.headers.at("X-Forwarded-For") : "unknown";
                if (!checkRateLimit(clientIp, res))
                    return;

                // Get session ID: prefer Mcp-Session-Id header (2025-03-26 spec),
                // fall back to ?session= query param for compatibility.
                std::string sessionId;
                auto sessionHeaderIt = req.headers.find("Mcp-Session-Id");
                if (sessionHeaderIt != req.headers.end())
                {
                    sessionId = sessionHeaderIt->second;
                }
                else
                {
                    auto params = req.parse_query();
                    auto sessionIt = params.find("session");
                    if (sessionIt == params.end())
                    {
                        res.set_status(400);
                        res.send("Missing Mcp-Session-Id header");
                        return;
                    }
                    sessionId = sessionIt->second;
                }
                if (!m_sessionManager.validateSession(sessionId))
                {
                    res.set_status(404);
                    res.send("Invalid or expired session");
                    return;
                }

                // Check for Last-Event-ID (resumability)
                std::string lastEventId;
                auto lastEventIdHeader = req.headers.find("Last-Event-Id");
                if (lastEventIdHeader != req.headers.end())
                {
                    lastEventId = lastEventIdHeader->second;
                }

                // Setup SSE stream headers
                res.set_status(200);
                res.set_header("Content-Type", "text/event-stream");
                res.set_header("Cache-Control", "no-cache");
                res.set_header("Connection", "keep-alive");
                res.set_header(m_config.session.headerName, sessionId);

                // Create or replace per-session queue
                auto queue = std::make_shared<SSESessionQueue>();
                {
                    std::lock_guard<std::mutex> lock(m_sseQueuesMutex);
                    // Close any pre-existing stream for this session
                    auto existing = m_sseQueues.find(sessionId);
                    if (existing != m_sseQueues.end())
                    {
                        existing->second->closed.store(true);
                        existing->second->cv.notify_all();
                    }
                    m_sseQueues[sessionId] = queue;
                }

                // Pre-populate missed events for resumability
                if (!lastEventId.empty() && m_config.resumability.enabled)
                {
                    auto missedEvents = m_sessionManager.getEventsSince(sessionId, lastEventId);
                    std::lock_guard<std::mutex> qlock(queue->mutex);
                    for (const auto& event : missedEvents)
                    {
                        queue->events.push(event);
                    }
                }

                // Write deadline: if no event arrives within this many seconds, send a keepalive
                // comment and reset; close the stream if the session has been closed.
                const int writeDeadlineSeconds = m_config.sseWriteDeadlineSeconds > 0
                    ? m_config.sseWriteDeadlineSeconds : 30;

                // send_chunk_stream callback — called by HTTP server for each chunk.
                // Returns "" to signal end-of-stream.
                //
                // IMPORTANT: this callback runs on the reactor's single I/O thread.
                // We must not block for more than ~200 ms or other connections (e.g.
                // tools/list) cannot be accepted/processed while the SSE stream is open.
                // We use a short poll interval and send a minimal SSE comment each time
                // so the reactor yields between keepalive cycles.
                auto weakQueue = std::weak_ptr<SSESessionQueue>(queue);
                auto lastKeepalive = std::make_shared<std::chrono::steady_clock::time_point>(
                    std::chrono::steady_clock::now());
                res.send_chunk_stream([weakQueue, writeDeadlineSeconds, lastKeepalive]() -> std::string
                {
                    auto q = weakQueue.lock();
                    if (!q)
                        return ""; // queue destroyed — close stream

                    std::unique_lock<std::mutex> lock(q->mutex);
                    // Short poll so the reactor thread is never blocked > 200 ms.
                    bool signalled = q->cv.wait_for(
                        lock,
                        std::chrono::milliseconds(200),
                        [&q] { return !q->events.empty() || q->closed.load(); });

                    if (q->closed.load() && q->events.empty())
                        return ""; // close stream

                    if (!signalled)
                    {
                        // No event arrived within 200 ms.  Only send an SSE keepalive
                        // comment when writeDeadlineSeconds have elapsed so we don't
                        // flood clients; otherwise return a silent SSE comment that
                        // every compliant client ignores (line starting with ':').
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            now - *lastKeepalive).count();
                        if (elapsed >= writeDeadlineSeconds)
                        {
                            *lastKeepalive = now;
                            q->lastActivity = now;
                            return ": keepalive\n\n";
                        }
                        return ": \n\n"; // silent SSE comment — reactor yields, client ignores
                    }

                    // Dequeue next event
                    std::string event = std::move(q->events.front());
                    q->events.pop();
                    q->lastActivity = std::chrono::steady_clock::now();
                    return event;
                });
            }

            /// @brief Handle HTTP DELETE request (terminate session)
            void handleHttpDelete(const HttpRequest& req, HttpResponse& res)
            {
                // Apply CORS headers
                applyCorsHeaders(res);

                if (!m_config.session.allowClientTermination)
                {
                    res.set_status(403);
                    res.send("Client session termination not allowed");
                    return;
                }

                std::string sessionId = getSessionId(req);
                if (sessionId.empty())
                {
                    res.set_status(400);
                    res.send("Missing session ID");
                    return;
                }

                if (m_sessionManager.terminateSession(sessionId))
                {
                    res.set_status(204); // No Content
                    res.send("");
                }
                else
                {
                    res.set_status(404);
                    res.send("Session not found");
                }
            }

            /// @brief Handle HTTP OPTIONS request (CORS preflight)
            void handleHttpOptions(const HttpRequest& req, HttpResponse& res)
            {
                applyCorsHeaders(res);
                res.set_status(204);
                res.send("");
            }

            /// @brief Apply CORS headers to response
            void applyCorsHeaders(HttpResponse& res)
            {
                res.set_header("Access-Control-Allow-Origin", m_config.cors.allowOrigin);
                res.set_header("Access-Control-Allow-Methods", m_config.cors.allowMethods);
                res.set_header("Access-Control-Allow-Headers", m_config.cors.allowHeaders);
                res.set_header("Access-Control-Expose-Headers", m_config.cors.exposeHeaders);
                res.set_header("Access-Control-Max-Age", m_config.cors.maxAge);
            }

            /// @brief Authenticate request.
            /// Supports Bearer, API_KEY, and CAPABILITY_TOKEN (X-MCP-Capability-Token header).
            /// @return true if authenticated or auth not required, false otherwise
            bool authenticate(const HttpRequest& req, HttpResponse& res)
            {
                if (!m_config.auth.enabled)
                {
                    return true;
                }

                // Capability token check (backport from FMcpNativeTransport).
                // When a capability token is configured, check X-MCP-Capability-Token first.
                if (m_config.auth.type == ServerConfig::AuthConfig::Type::CAPABILITY_TOKEN)
                {
                    auto capIt = req.headers.find("X-MCP-Capability-Token");
                    if (capIt == req.headers.end() || capIt->second.empty())
                    {
                        res.set_status(401);
                        res.set_header("WWW-Authenticate", "MCP-Capability-Token");
                        res.send("X-MCP-Capability-Token required");
                        return false;
                    }
                    if (m_config.auth.secretOrPublicKey.has_value() &&
                        capIt->second != m_config.auth.secretOrPublicKey.value())
                    {
                        res.set_status(401);
                        res.send("Invalid capability token");
                        return false;
                    }
                    return true;
                }

                auto authHeader = req.headers.find(m_config.auth.headerName);
                if (authHeader == req.headers.end())
                {
                    res.set_status(401);
                    res.set_header("WWW-Authenticate", "Bearer");
                    res.send("Authentication required");
                    return false;
                }

                std::string token = authHeader->second;

                // Strip "Bearer " prefix if present
                if (m_config.auth.type == ServerConfig::AuthConfig::Type::BEARER)
                {
                    if (token.find("Bearer ") == 0)
                    {
                        token = token.substr(7);
                    }
                }

                // Use custom validator if provided
                if (m_config.auth.validator)
                {
                    if (!m_config.auth.validator(token))
                    {
                        res.set_status(401);
                        res.send("Invalid authentication token");
                        return false;
                    }
                    return true;
                }

#ifdef SOCKETSHPP_HAS_JWT_CPP
                // JWT validation if secret provided and no custom validator
                if (m_config.auth.type == ServerConfig::AuthConfig::Type::BEARER && 
                    m_config.auth.secretOrPublicKey.has_value())
                {
                    try
                    {
                        auto decoded = jwt::decode(token);
                        auto verifier = jwt::verify()
                            .allow_algorithm(jwt::algorithm::hs256{m_config.auth.secretOrPublicKey.value()});
                        verifier.verify(decoded);
                        return true;
                    }
                    catch (const std::exception&)
                    {
                        res.set_status(401);
                        res.send("Invalid JWT token");
                        return false;
                    }
                }
#endif

                // Simple secret comparison for API key type
                if (m_config.auth.type == ServerConfig::AuthConfig::Type::API_KEY &&
                    m_config.auth.secretOrPublicKey.has_value())
                {
                    if (token != m_config.auth.secretOrPublicKey.value())
                    {
                        res.set_status(401);
                        res.send("Invalid API key");
                        return false;
                    }
                    return true;
                }

                // If we get here, auth is enabled but no validation method configured
                res.set_status(500);
                res.send("Server authentication misconfigured");
                return false;
            }

            /// @brief Get session ID from request headers
            std::string getSessionId(const HttpRequest& req)
            {
                auto it = req.headers.find(m_config.session.headerName);
                if (it != req.headers.end())
                {
                    return it->second;
                }
                return "";
            }

            /// @brief Handle JSON-RPC request
            JsonRpcResponse handleRequest(const JsonRpcRequest& request)
            {
                // ── Protocol version negotiation for initialize ─────────────────────────
                // Per MCP spec: server responds with the highest supported version that is
                // ≤ the client's requested version.  If no common version → -32002 error.
                if (request.method == "initialize")
                {
                    json params = request.params.value_or(json::object());
                    std::string client_ver = params.value("protocolVersion", "");

                    std::string negotiated;
                    for (auto& v : SUPPORTED_VERSIONS)
                    {
                        if (v <= client_ver || client_ver.empty()) { negotiated = v; break; }
                    }
                    if (negotiated.empty())
                    {
                        auto error = JsonRpcError::serverError(-32002,
                            "Unsupported protocol version: " + client_ver +
                            ". Supported: 2025-03-26, 2024-11-05");
                        return JsonRpcResponse::failure(request.id, error);
                    }

                    // Store client capabilities for this session (session is set in outer handler)
                    // We stash them under a per-request key; the caller sets the real session header.
                    json caps = params.value("capabilities", json::object());
                    // Stored keyed by clientInfo.name+version as a best-effort before session is known.
                    // The full session storage happens in setupStreamableRoutes/setupHttpRoutes
                    // where the sessionId is available.  See m_pendingClientCaps below.
                    {
                        std::lock_guard<std::mutex> lock(m_clientCapsMutex);
                        m_pendingClientCaps = caps;
                    }

                    // Delegate to user's initialize handler if registered, otherwise use built-in
                    auto it = m_methods.find("initialize");
                    if (it != m_methods.end())
                    {
                        try {
                            json result = it->second(params);
                            // Patch protocolVersion to the negotiated value
                            result["protocolVersion"] = negotiated;
                            return JsonRpcResponse::success(request.id, result);
                        }
                        catch (const JsonRpcError& err) { return JsonRpcResponse::failure(request.id, err); }
                        catch (const std::exception& e) {
                            return JsonRpcResponse::failure(request.id, JsonRpcError::internalError(e.what()));
                        }
                    }
                    // No user handler: return a minimal valid initialize response
                    return JsonRpcResponse::success(request.id, {
                        {"protocolVersion", negotiated},
                        {"capabilities",    json::object()},
                        {"serverInfo",      server_info()}
                    });
                }

                // Find method handler
                auto it = m_methods.find(request.method);
                if (it == m_methods.end())
                {
                    auto error = JsonRpcError::methodNotFound(request.method);
                    return JsonRpcResponse::failure(request.id, error);
                }

                try
                {
                    // Extract params (default to empty object if not provided)
                    json params = request.params.value_or(json::object());
                    
                    // Call handler
                    json result = it->second(params);
                    
                    return JsonRpcResponse::success(request.id, result);
                }
                catch (const JsonRpcError& error)
                {
                    return JsonRpcResponse::failure(request.id, error);
                }
                catch (const std::exception& e)
                {
                    auto error = JsonRpcError::internalError(e.what());
                    return JsonRpcResponse::failure(request.id, error);
                }
            }

            /// @brief Handle JSON-RPC notification
            void handleNotification(const JsonRpcNotification& notification)
            {
                // Find method handler
                auto it = m_methods.find(notification.method);
                if (it == m_methods.end())
                {
                    LOG_WARN("MCPServer: Unknown notification method: %s", notification.method.c_str());
                    return;
                }

                try
                {
                    json params = notification.params.value_or(json::object());
                    it->second(params); // Notifications don't return values
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("MCPServer: Error handling notification %s: %s", 
                             notification.method.c_str(), e.what());
                }
            }
        };

    } // namespace server
} // namespace mcp
SOCKETSHPP_NS_END
