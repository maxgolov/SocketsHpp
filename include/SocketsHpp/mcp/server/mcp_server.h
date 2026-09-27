// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
//
// MCP SERVER — SocketsHpp
//
// Implements MCP protocol versions 2025-11-25, 2025-06-18, 2025-03-26 (Streamable
// HTTP) and a POST-based transport for protocol version 2024-11-05 (not the legacy
// SSE `endpoint`-event handshake) over the SocketsHpp HTTP server. The STDIO transport is
// supported only through MCPServer::processMessage(), driven by the caller: the
// server never reads stdin itself and never binds a port in STDIO mode;
// StdioServerTransport (stdio_server.h) runs that loop over stdin/stdout.
//
// ── NGINX INTEGRATION GUIDE ──────────────────────────────────────────────────
//
// Run the MCP server on a loopback port (e.g. 3601) and put nginx in front for
// TLS, OAuth, advanced rate-limiting and load balancing. The server itself only
// provides basic hardening: a loopback bind guard, optional per-client rate
// limiting, and bearer / API-key / capability-token authentication.
//
// Recommended nginx config:
//
//   upstream mcp_backend {
//       server 127.0.0.1:3601;
//       keepalive 32;           # reuse connections
//   }
//
//   limit_req_zone $binary_remote_addr zone=mcp:10m rate=10r/s;
//
//   server {
//       listen 443 ssl http2;
//       ssl_certificate     /etc/ssl/mcp.crt;   # TLS (offloaded)
//       ssl_certificate_key /etc/ssl/mcp.key;
//
//       location /mcp {
//           proxy_pass         http://mcp_backend;
//           proxy_http_version 1.1;
//           proxy_set_header   Connection "";   # keepalive pool
//           proxy_set_header   Host $host;
//           proxy_set_header   X-Forwarded-For $remote_addr;
//           proxy_buffering    off;             # CRITICAL for SSE
//           proxy_cache        off;
//           proxy_read_timeout 3600s;           # keep SSE streams alive
//           limit_req          zone=mcp burst=20 nodelay;
//       }
//       location /health {
//           proxy_pass http://mcp_backend/health;  # GET /health → health
//       }
//   }
//
// Features best offloaded to nginx (not implemented here):
//   • TLS / mTLS termination
//   • OAuth 2.1 flows and JWT validation beyond HS256 shared secrets
//   • Aggressive DDoS / IP-block rules (ngx_http_geo_module)
//   • gzip compression for JSON payloads
//   • Load balancing across multiple server instances
//   • Access logging / metrics export
//   • PKCE / token exchange flows
//
// NOTE: proxy_buffering off is MANDATORY for SSE. Without it nginx buffers the
// entire stream and the client sees nothing until close.

/// @file mcp_server.h
/// @brief MCP (Model Context Protocol) server: mcp::server::MCPServer.
///
/// - HTTP transports: MCPServer::listen() is non-blocking; it binds only to
///   ServerConfig::host / ServerConfig::port (port 0 = ephemeral, see MCPServer::port())
///   and refuses non-loopback hosts unless ServerConfig::allowNonLoopback is set.
/// - STDIO transport: the caller reads messages and passes them to
///   MCPServer::processMessage(); nothing is bound.
/// - JWT (HS256) validation of bearer tokens requires jwt-cpp and the
///   SOCKETSHPP_HAS_JWT_CPP macro, which the SocketsHpp::SocketsHpp CMake target
///   defines automatically when jwt-cpp is found.
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/common/json_rpc.h>
#include <SocketsHpp/mcp/common/mcp_config.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <stdexcept>
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
        using json = nlohmann::json;  ///< nlohmann::json alias.

        /// @brief MCP server: dispatches JSON-RPC 2.0 messages to registered method handlers.
        ///
        /// Transports (ServerConfig::transport):
        ///   - TransportType::STDIO — caller-driven: pass each message to processMessage().
        ///     The server never reads stdin and never binds a port.
        ///   - TransportType::HTTP — protocol version 2024-11-05: requests and responses on
        ///     POST, notifications on a GET SSE stream opened after initialize. This is not
        ///     the legacy HTTP+SSE `endpoint`-event handshake.
        ///   - TransportType::HTTP_STREAMABLE — Streamable HTTP POST, optional GET stream
        ///     (MCP 2025-03-26).
        /// HTTP transports serve ServerConfig::endpoint (POST, GET, DELETE, OPTIONS) and
        /// GET /health, and bind only in listen().
        ///
        /// Auto-registered built-in methods (registering the same name replaces them):
        ///   - "ping"                      → {} (liveness check, required by spec)
        ///   - "notifications/initialized" → no-op (client ACK after initialize)
        ///   - "notifications/cancelled"   → sets the cancel token of the in-flight request
        ///     whose JSON-RPC id is params.requestId, within the sender's own session
        ///   - "logging/setLevel"          → sets the minimum level for push_log() for
        ///     the session that sent it (default "warning")
        /// "initialize" is handled internally (version negotiation, session creation); a
        /// simple handler registered as "initialize" supplies the result object.
        ///
        /// Protocol versions (supportedProtocolVersions()): "2025-11-25", "2025-06-18",
        /// "2025-03-26", "2024-11-05". initialize answers with the client's version if
        /// supported, otherwise the latest; the result is stored per session
        /// (get_protocol_version()). For sessions on 2025-06-18 or later, JSON-RPC batches
        /// are rejected and the HTTP transports validate the MCP-Protocol-Version header.
        ///
        /// Typed helpers: registerTool() / registerCancellableTool(), registerPrompt() and
        /// registerResource() provide tools/list + tools/call, prompts/list + prompts/get
        /// and resources/list + resources/read. The matching capabilities ("tools",
        /// "prompts", "resources") are advertised by the default initialize result and
        /// merged into the "capabilities" of a registered initialize handler's result
        /// (keys the handler sets itself are left untouched). A handler registered for one
        /// of those method names with registerMethod() / registerCancellable() takes
        /// precedence over the helper.
        ///
        /// @note Thread safety: on the HTTP transports listen() enables 4 worker threads
        ///       and handlers run on them concurrently, so registered handlers (and
        ///       AuthConfig::validator) must be thread-safe. registerMethod() /
        ///       registerCancellable() may be called at any time, including after listen();
        ///       push_* helpers may be called from any thread.
        ///
        /// See the file header comment for the nginx integration guide.
        class MCPServer
        {
        public:
            /// @brief Simple method handler (no cancellation support).
            ///
            /// Receives the request params (an empty object if absent) and returns the
            /// JSON-RPC result. Throwing JsonRpcError sends that error; any other
            /// std::exception becomes an internal error (-32603).
            using MethodHandler = std::function<json(const json& params)>;

            /// @brief Cancellable method handler — also receives a cancel token.
            ///
            /// Poll cancel_requested->load() in long-running operations; it becomes true
            /// when the same session sends notifications/cancelled for this request id.
            /// Return a result or throw (e.g. std::runtime_error("cancelled")) when done.
            using CancellableMethodHandler =
                std::function<json(const json& params,
                                   std::shared_ptr<std::atomic<bool>> cancel_requested)>;

            /// @brief Create an MCP server. Registers built-in methods and, for HTTP
            ///        transports, the HTTP routes. Nothing is bound until listen().
            /// @param config Server configuration (copied).
            explicit MCPServer(const ServerConfig& config)
                : m_config(config)
                // Default-constructed: nothing is bound until listen(), so STDIO
                // servers never open a port and the loopback guard runs first.
                , m_running(false)
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
                m_httpServer.setIdleTimeout(std::chrono::milliseconds(config.idleTimeoutMs));
                m_httpServer.setRequestTimeout(std::chrono::milliseconds(config.requestTimeoutMs));

                // ── Auto-register built-in MCP protocol methods ───────────────────────────
                // ping — required by spec; clients poll for liveness
                registerMethod("ping", [](const json&) -> json { return json::object(); });

                // notifications/initialized — client ACK after initialize, no response needed
                registerMethod("notifications/initialized", [](const json&) -> json { return json::object(); });

                // notifications/cancelled — client cancels an in-flight request by requestId
                registerMethod("notifications/cancelled", [this](const json& params) -> json {
                    if (params.is_object() && params.contains("requestId"))
                    {
                        JsonRpcId id;
                        if (jsonRpcIdFromJson(params["requestId"], id))
                        {
                            std::lock_guard<std::mutex> lock(m_pendingMutex);
                            // Scoped to the sender's session: a client can only
                            // cancel its own requests.
                            auto it = m_pendingCancellations.find(cancellationKey(id));
                            if (it != m_pendingCancellations.end())
                                it->second->store(true);
                        }
                    }
                    return json::object();
                });

                // logging/setLevel — client requests minimum log severity for notifications/message
                registerMethod("logging/setLevel", [this](const json& params) -> json {
                    std::string level = (params.is_object() && params.contains("level") &&
                                         params["level"].is_string())
                        ? params["level"].get<std::string>() : std::string();
                    int idx = logLevelIndex(level);
                    if (idx < 0)
                        throw JsonRpcError::invalidParams("Invalid log level: " + level);
                    // Per client: the level applies to the session that sent the request.
                    std::lock_guard<std::mutex> lock(m_logLevelsMutex);
                    m_logLevels[currentSession()] = idx;
                    return json::object();
                });

                // Setup routes based on transport type
                if (config.transport == TransportType::HTTP)
                {
                    setupHttpRoutes();
                }
                else if (config.transport == TransportType::HTTP_STREAMABLE)
                {
                    setupStreamableRoutes();
                }
                // STDIO: no routes; the caller feeds messages to processMessage()
            }

            /// @brief Calls stop().
            ~MCPServer()
            {
                stop();
            }

            /// @brief Register (or replace) a simple method or notification handler.
            /// @param method JSON-RPC method name.
            /// @param handler Handler; must be thread-safe on the HTTP transports.
            /// @note Thread-safe. Registering a built-in name (ping, notifications/initialized,
            ///       notifications/cancelled, logging/setLevel) replaces the built-in.
            void registerMethod(const std::string& method, MethodHandler handler)
            {
                std::unique_lock<std::shared_mutex> lock(m_methodsMutex);
                m_methods[method] = MethodEntry{std::move(handler), nullptr};
            }

            /// @brief Register (or replace) a cancellable method handler.
            ///
            /// The handler receives a shared cancel_requested token. Poll it in long-running
            /// operations; when true, abort (e.g. throw std::runtime_error("cancelled")).
            /// The token is set by a notifications/cancelled from the same session whose
            /// params.requestId equals the JSON-RPC id of the in-flight request (tokens are
            /// keyed by (session, id)). For STDIO the session is "" and cancellation only
            /// works if the caller runs processMessage() concurrently.
            /// If the token is set when the handler finishes (returns or throws), no
            /// response is sent for the request, as the MCP spec asks (HTTP: 202 with an
            /// empty body, or the entry is left out of a batch; processMessage(): "").
            /// @param method JSON-RPC method name.
            /// @param handler Handler; must be thread-safe on the HTTP transports.
            /// @note Thread-safe. A cancellable "initialize" handler is ignored.
            void registerCancellable(const std::string& method, CancellableMethodHandler handler)
            {
                std::unique_lock<std::shared_mutex> lock(m_methodsMutex);
                m_methods[method] = MethodEntry{nullptr, std::move(handler)};
            }

            // ── Typed helpers: tools, prompts, resources ─────────────────────────────

            /// @brief Tool handler: receives the tools/call `arguments` (an empty object if
            ///        absent) and returns the tool result.
            ///
            /// The return value is turned into a CallToolResult:
            ///   - an object containing "content" is sent as is (it may set "isError",
            ///     "structuredContent", "_meta", ...);
            ///   - a string becomes one text content item;
            ///   - null becomes an empty content list;
            ///   - any other object becomes "structuredContent" plus its serialized JSON
            ///     as a text content item (as the spec recommends for structured output);
            ///   - any other JSON value (array, number, bool) becomes its serialized JSON
            ///     as a text content item.
            /// Throwing a std::exception produces a result with "isError": true and the
            /// exception message as text content (a tool execution error, visible to the
            /// model). Throwing JsonRpcError sends that JSON-RPC error instead.
            using ToolHandler = std::function<json(const json& args)>;

            /// @brief Cancellable tool handler: like ToolHandler, and also receives the
            ///        request's cancel token (see registerCancellable()).
            using CancellableToolHandler =
                std::function<json(const json& args,
                                   std::shared_ptr<std::atomic<bool>> cancel_requested)>;

            /// @brief Prompt handler: receives the prompts/get `arguments` (an object of
            ///        strings, empty if absent) and returns the prompt.
            ///
            /// An object containing "messages" is sent as is (GetPromptResult); an array is
            /// used as the "messages" list; a string becomes a single "user" message with
            /// that text. The registered description is added when the result has none.
            /// Throwing JsonRpcError sends that error; any other std::exception becomes an
            /// internal error (-32603).
            using PromptHandler = std::function<json(const json& args)>;

            /// @brief Resource reader: receives the requested URI and returns its contents.
            ///
            /// An object containing "contents" is sent as is (ReadResourceResult); an
            /// object with "text" or "blob" is one content item; an array is the list of
            /// content items; a string is the text of the resource. "uri" and "mimeType"
            /// are filled in on content items that lack them. Throwing JsonRpcError sends
            /// that error; any other std::exception becomes an internal error (-32603).
            using ResourceReader = std::function<json(const std::string& uri)>;

            /// @brief Register (or replace) a tool served by the built-in tools/list and
            ///        tools/call handlers.
            ///
            /// tools/list returns the tools in registration order (a replaced tool keeps
            /// its position). tools/call with an unknown name, a missing name or
            /// non-object arguments is answered with JSON-RPC error -32602. The input is not
            /// validated against the schema; the handler checks its arguments.
            /// Registering a tool adds "tools" to the capabilities of the initialize result.
            /// @param name        Tool name (non-empty; the spec recommends 1-128 characters
            ///                    from A-Z a-z 0-9 _ - .).
            /// @param description Human-readable description (omitted from tools/list if empty).
            /// @param inputSchema JSON Schema of the arguments; must be an object (null means
            ///                    {"type": "object"}).
            /// @param handler     Tool implementation; must be thread-safe on the HTTP transports.
            /// @throws std::invalid_argument for an empty name, a non-object schema or an
            ///         empty handler.
            /// @note Thread-safe. A handler registered with registerMethod() or
            ///       registerCancellable() for "tools/list" or "tools/call" takes precedence
            ///       over the built-in one for that method.
            void registerTool(const std::string& name, const std::string& description,
                              const json& inputSchema, ToolHandler handler)
            {
                registerTool(makeToolDefinition(name, description, inputSchema), std::move(handler));
            }

            /// @brief Register (or replace) a tool from a full tool definition.
            ///
            /// Use this form to set optional fields such as "title", "outputSchema"
            /// (MCP 2025-06-18: the handler should then return an object, which is sent as
            /// "structuredContent") or "annotations". The definition is returned by
            /// tools/list as given, with "inputSchema" defaulted to {"type": "object"}.
            /// @param definition Tool object with at least a non-empty string "name".
            /// @param handler    Tool implementation; must be thread-safe on the HTTP transports.
            /// @throws std::invalid_argument for a definition without a name, a non-object
            ///         "inputSchema" or an empty handler.
            /// @note Thread-safe. See registerTool(const std::string&, const std::string&,
            ///       const json&, ToolHandler) for the call semantics.
            void registerTool(const json& definition, ToolHandler handler)
            {
                if (!handler)
                    throw std::invalid_argument("registerTool: empty handler");
                addTool(definition,
                        [h = std::move(handler)](const json& args,
                                                 std::shared_ptr<std::atomic<bool>>) {
                            return h(args);
                        });
            }

            /// @brief Register (or replace) a cancellable tool.
            ///
            /// The handler also receives the cancel token of the tools/call request; poll it
            /// and stop early (e.g. throw) when it becomes true. As with
            /// registerCancellable(), a request whose token is set when the handler
            /// finishes gets no response.
            /// @param name        Tool name (non-empty).
            /// @param description Human-readable description (omitted from tools/list if empty).
            /// @param inputSchema JSON Schema of the arguments; must be an object (null means
            ///                    {"type": "object"}).
            /// @param handler     Tool implementation; must be thread-safe on the HTTP transports.
            /// @throws std::invalid_argument for an empty name, a non-object schema or an
            ///         empty handler.
            /// @note Thread-safe.
            void registerCancellableTool(const std::string& name, const std::string& description,
                                         const json& inputSchema, CancellableToolHandler handler)
            {
                registerCancellableTool(makeToolDefinition(name, description, inputSchema),
                                        std::move(handler));
            }

            /// @brief Register (or replace) a cancellable tool from a full tool definition.
            /// @param definition Tool object with at least a non-empty string "name".
            /// @param handler    Tool implementation; must be thread-safe on the HTTP transports.
            /// @throws std::invalid_argument for a definition without a name, a non-object
            ///         "inputSchema" or an empty handler.
            /// @note Thread-safe.
            void registerCancellableTool(const json& definition, CancellableToolHandler handler)
            {
                if (!handler)
                    throw std::invalid_argument("registerCancellableTool: empty handler");
                addTool(definition, std::move(handler));
            }

            /// @brief Register (or replace) a prompt served by the built-in prompts/list and
            ///        prompts/get handlers.
            ///
            /// prompts/get with an unknown name or without a required argument is answered
            /// with JSON-RPC error -32602. Registering a prompt adds "prompts" to the
            /// capabilities of the initialize result.
            /// @param name        Prompt name (non-empty).
            /// @param description Human-readable description (omitted if empty).
            /// @param arguments   Array of PromptArgument objects ({"name", "description",
            ///                    "required"}); null or an empty array for none.
            /// @param handler     Prompt implementation; must be thread-safe on the HTTP
            ///                    transports.
            /// @throws std::invalid_argument for an empty name, a non-array arguments value
            ///         or an empty handler.
            /// @note Thread-safe. registerMethod("prompts/list" / "prompts/get", ...) takes
            ///       precedence over the built-in handler.
            void registerPrompt(const std::string& name, const std::string& description,
                                const json& arguments, PromptHandler handler)
            {
                if (name.empty())
                    throw std::invalid_argument("registerPrompt: empty name");
                if (!arguments.is_null() && !arguments.is_array())
                    throw std::invalid_argument("registerPrompt: arguments must be an array");
                if (!handler)
                    throw std::invalid_argument("registerPrompt: empty handler");
                json definition = {{"name", name}};
                if (!description.empty())
                    definition["description"] = description;
                if (arguments.is_array() && !arguments.empty())
                    definition["arguments"] = arguments;

                std::unique_lock<std::shared_mutex> lock(m_helpersMutex);
                upsert(m_prompts, name, PromptEntry{std::move(definition), std::move(handler)});
            }

            /// @brief Register (or replace) a resource served by the built-in resources/list
            ///        and resources/read handlers.
            ///
            /// resources/read with an unregistered URI is answered with JSON-RPC error
            /// -32002 (resource not found). Registering a resource adds "resources" to the
            /// capabilities of the initialize result.
            /// @param uri         Resource URI (non-empty; matched exactly).
            /// @param name        Resource name.
            /// @param mimeType    MIME type (omitted if empty).
            /// @param reader      Returns the resource contents; must be thread-safe on the
            ///                    HTTP transports.
            /// @param description Optional human-readable description (omitted if empty).
            /// @throws std::invalid_argument for an empty URI or an empty reader.
            /// @note Thread-safe. registerMethod("resources/list" / "resources/read", ...)
            ///       takes precedence over the built-in handler.
            void registerResource(const std::string& uri, const std::string& name,
                                  const std::string& mimeType, ResourceReader reader,
                                  const std::string& description = std::string())
            {
                if (uri.empty())
                    throw std::invalid_argument("registerResource: empty uri");
                if (!reader)
                    throw std::invalid_argument("registerResource: empty reader");
                json definition = {{"uri", uri}, {"name", name}};
                if (!description.empty())
                    definition["description"] = description;
                if (!mimeType.empty())
                    definition["mimeType"] = mimeType;

                std::unique_lock<std::shared_mutex> lock(m_helpersMutex);
                upsert(m_resources, uri, ResourceEntry{std::move(definition), std::move(reader)});
            }

            /// @brief Page size of the built-in tools/list, prompts/list and resources/list.
            ///
            /// 0 (the default) returns everything in one page. Otherwise each page holds at
            /// most @p pageSize entries and "nextCursor" (an opaque string) is set while
            /// more follow; a cursor the server did not issue gets JSON-RPC error -32602.
            /// @param pageSize Maximum entries per page; 0 disables pagination.
            /// @note Thread-safe.
            void setListPageSize(size_t pageSize)
            {
                m_listPageSize.store(pageSize);
            }

            /// @brief Protocol versions this server supports, newest first.
            /// @return {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"}.
            static const std::vector<std::string>& supportedProtocolVersions()
            {
                return SUPPORTED_VERSIONS;
            }

            /// @brief Protocol version negotiated by a session's initialize.
            /// For HTTP transports pass the session id; for the STDIO transport
            /// (processMessage) pass an empty string.
            /// @return The version, or "" if no initialize has completed for that session.
            /// @note Thread-safe.
            std::string get_protocol_version(const std::string& sessionId) const
            {
                std::lock_guard<std::mutex> lock(m_protocolVersionsMutex);
                auto it = m_protocolVersions.find(sessionId);
                return it != m_protocolVersions.end() ? it->second : std::string();
            }

            /// @brief Push a notifications/progress message to a session's SSE stream
            ///        (via push_event()).
            /// Call from within a tool handler to report long-running operation progress.
            /// @param sessionId   Session to push to (from Mcp-Session-Id header)
            /// @param token       progressToken from the tool call params (string or int)
            /// @param progress    0.0–1.0 or total steps completed (spec allows either)
            /// @param total       Optional: denominator when progress is a step count
            /// @param message     Optional: human-readable status message
            /// @return true if queued; false if the session has no open stream queue.
            /// @note Thread-safe. Not available for STDIO (always returns false).
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

            /// @brief Push a notifications/message (log) message to a session's SSE stream.
            /// Suppressed when level is below the minimum that this session requested with
            /// logging/setLevel (default "warning"). Unknown level names are never
            /// suppressed.
            /// @param sessionId Session to push to
            /// @param level     One of: debug info notice warning error critical alert emergency
            /// @param logger    Logger name (e.g., "my-server")
            /// @param data      Log message (string or structured JSON object)
            /// @return true if queued; false if suppressed or the session has no open stream queue.
            /// @note Thread-safe.
            bool push_log(const std::string& sessionId,
                          const std::string& level,
                          const std::string& logger,
                          const json&        data)
            {
                // Suppress if below the requested log level
                int cur = logLevelIndex(level);
                if (cur >= 0 && cur < minLogLevel(sessionId))
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
            /// For HTTP transports pass the session id; for the STDIO transport
            /// (processMessage) pass an empty string.
            /// @return The capabilities, or null JSON if no initialize has completed for
            ///         that session. Thread-safe.
            json get_client_capabilities(const std::string& sessionId) const
            {
                std::lock_guard<std::mutex> lock(m_clientCapsMutex);
                auto it = m_clientCapabilities.find(sessionId);
                return it != m_clientCapabilities.end() ? it->second : json{};
            }

            /// @brief Server info as returned by GET /health.
            /// @return {"name", "version", "protocolVersion"}: ServerConfig::serverName /
            ///         serverVersion (defaults if empty) and the newest supported protocol
            ///         version ("2025-11-25"), not a per-session negotiated one.
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
            ///
            /// A session has a queue once its GET stream is open, or, for
            /// HTTP_STREAMABLE, right after initialize (events pushed before the GET stream
            /// opens are kept and delivered when it does).
            /// @param sessionId  Session to push to (from initialize response Mcp-Session-Id header)
            /// @param eventData  Complete raw SSE event (e.g. "event: message\ndata: {...}\n\n")
            /// @return true if the session has an open queue and the event was queued
            /// @note With resumability enabled, every event gets an "id:" (unless it
            ///       already has one) and is recorded in the session's history, so a
            ///       client reconnecting with Last-Event-ID receives what it missed.
            bool push_event(const std::string& sessionId, const std::string& eventData)
            {
                std::lock_guard<std::mutex> lock(m_sseQueuesMutex);
                auto it = m_sseQueues.find(sessionId);
                if (it == m_sseQueues.end() || it->second->closed.load())
                    return false;

                std::string data = eventData;
                if (m_config.resumability.enabled)
                {
                    std::string id = sseEventId(data);
                    if (id.empty())
                    {
                        id = std::to_string(++m_eventSequence);
                        data = "id: " + id + "\n" + data;
                    }
                    m_sessionManager.addEvent(sessionId, id, data);
                }
                {
                    std::lock_guard<std::mutex> qlock(it->second->mutex);
                    it->second->events.push(std::move(data));
                    it->second->lastActivity = std::chrono::steady_clock::now();
                }
                it->second->cv.notify_one();
                return true;
            }

            /// @brief Extract the "id:" field of the first event in a raw SSE string.
            /// @param event Raw SSE text.
            /// @return The id value (one leading space and a trailing CR removed), or ""
            ///         if the first event has none.
            static std::string sseEventId(const std::string& event)
            {
                size_t pos = 0;
                while (pos < event.size())
                {
                    size_t end = event.find('\n', pos);
                    if (end == std::string::npos)
                        end = event.size();
                    const std::string line = event.substr(pos, end - pos);
                    if (line.compare(0, 3, "id:") == 0)
                    {
                        std::string value = line.substr(3);
                        if (!value.empty() && value[0] == ' ')
                            value.erase(0, 1);
                        if (!value.empty() && value.back() == '\r')
                            value.pop_back();
                        return value;
                    }
                    if (line.empty() || line == "\r")
                        break;  // end of the first event
                    pos = end + 1;
                }
                return std::string();
            }

            /// @brief Close and remove all SSE queues idle longer than sseIdleTimeoutSeconds
            ///        (or already closed). The MCP sessions themselves are not terminated.
            /// Call periodically from a maintenance thread; the server never calls it itself.
            /// @param sseIdleTimeoutSeconds Idle limit in seconds (default 300). An open stream
            ///        stays active through its periodic keepalives
            ///        (ServerConfig::sseWriteDeadlineSeconds).
            /// @note Thread-safe.
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

            /// @brief Bind and start serving (HTTP or HTTP_STREAMABLE mode). Non-blocking:
            ///        requests are served on background threads until stop().
            ///
            /// Binds only to ServerConfig::host and ServerConfig::port (0 = ephemeral; see
            /// port()) and sizes the handler pool using ServerConfig::workerThreads.
            /// No-op if already running (the loopback guard is still checked first).
            /// SSRF guard: refuses a host other than "127.0.0.1", "localhost", "::1" or
            /// "[::1]" unless ServerConfig::allowNonLoopback is true.
            /// With ServerConfig::unixSocketPath set, binds only that Unix domain socket
            /// instead (host, port and the loopback guard do not apply; port() is -1).
            /// @throws std::runtime_error for the STDIO transport, a refused non-loopback
            ///         host, or a bind/listen failure; std::invalid_argument for a malformed host.
            void listen()
            {
                if (m_config.transport != TransportType::HTTP &&
                    m_config.transport != TransportType::HTTP_STREAMABLE)
                {
                    throw std::runtime_error("listen() only available in HTTP transport modes");
                }

                if (!m_config.unixSocketPath.empty())
                {
                    // Local IPC only: filesystem permissions govern access, so the
                    // loopback guard does not apply.
                    if (m_running.load())
                        return;
                    m_httpServer.setServerName(m_config.serverName.empty() ? std::string("mcp-server") : m_config.serverName);
                    m_httpServer.addListeningUnixSocket(m_config.unixSocketPath, m_config.unixSocketPermissions);
                    m_running = true;
                    m_httpServer.enableThreadPool(m_config.workerThreads);
                    m_httpServer.start();
                    return;
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

                if (m_running.load())
                    return;

                // Bind to the configured host only (never INADDR_ANY implicitly).
                m_httpServer.setServerName(m_config.host + ":" + std::to_string(m_config.port));
                m_httpServer.addListeningPort(m_config.host, m_config.port);

                m_running = true;
                m_httpServer.enableThreadPool(m_config.workerThreads);  // SSE callbacks run on pool; reactor stays free
                m_httpServer.start();
            }

            /// @brief Port the server is listening on (useful with config.port = 0).
            /// @return The port, or -1 before listen() or when listening on
            ///         ServerConfig::unixSocketPath.
            int port() const { return m_httpServer.getListeningPort(); }

            /// @brief Stop serving: close all SSE queues (ending their streams), then stop the
            ///        HTTP server. No-op if not running; called by the destructor.
            /// @note Blocks until the HTTP reactor thread has stopped; handlers already running
            ///       on worker threads are not waited for here.
            void stop()
            {
                if (!m_running.exchange(false))
                    return;

                // Signal all SSE streams to close
                {
                    std::lock_guard<std::mutex> lock(m_sseQueuesMutex);
                    for (auto& entry : m_sseQueues)
                    {
                        entry.second->closed.store(true);
                        entry.second->cv.notify_all();
                    }
                    m_sseQueues.clear();
                }

                m_httpServer.stop();
            }

            /// @brief Process a JSON-RPC message or batch (STDIO mode; works with any transport).
            ///
            /// Handlers run synchronously on the calling thread. All messages share the
            /// session "" (see get_client_capabilities("")). initialize is rejected
            /// inside a batch, and a batch is rejected as a whole (-32600) once initialize
            /// has negotiated protocol version 2025-06-18 or later.
            /// @param jsonRpcMessage JSON-RPC message (or batch array) string
            /// @return JSON-RPC response string; empty when no response is due
            ///         (notifications, or a batch made only of notifications). Parse errors
            ///         yield a -32700 response.
            /// @note May be called concurrently from several threads (handlers must then be
            ///       thread-safe); needed for notifications/cancelled to reach a running request.
            std::string processMessage(const std::string& jsonRpcMessage)
            {
                json body;
                try
                {
                    body = json::parse(jsonRpcMessage);
                }
                catch (const json::parse_error& e)
                {
                    return JsonRpcResponse::failure(nullptr, JsonRpcError::parseError(e.what())).serialize();
                }

                try
                {
                    if (body.is_array())
                    {
                        if (body.empty())
                            return invalidRequestResponse(nullptr, "Empty batch").dump();
                        const std::string version = effectiveVersion(std::string(), nullptr);
                        if (!batchingAllowed(version))
                            return batchRejectedResponse(version).dump();
                        json out = json::array();
                        for (const auto& item : body)
                        {
                            auto outcome = processOne(item, nullptr);
                            if (outcome.response)
                                out.push_back(std::move(*outcome.response));
                        }
                        return out.empty() ? std::string() : out.dump();
                    }

                    InitOutcome init;
                    auto outcome = processOne(body, &init);
                    if (init.succeeded)
                    {
                        storeClientCapabilities("", init.clientCaps);
                        storeProtocolVersion("", init.protocolVersion);
                    }
                    return outcome.response ? outcome.response->dump() : std::string();
                }
                catch (const std::exception& e)
                {
                    return JsonRpcResponse::failure(nullptr, JsonRpcError::internalError(e.what())).serialize();
                }
            }

        private:
            // Supported protocol versions — highest first (negotiation preference order)
            static inline const std::vector<std::string> SUPPORTED_VERSIONS = {
                "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"
            };

            // Version assumed for an HTTP request without MCP-Protocol-Version when none
            // was negotiated (spec: "the server SHOULD assume protocol version 2025-03-26").
            static constexpr const char* DEFAULT_NEGOTIATED_VERSION = "2025-03-26";

            // First version without JSON-RPC batching and with the MCP-Protocol-Version header.
            static constexpr const char* NO_BATCH_VERSION = "2025-06-18";

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
            // Per-client rate limiting (fixed one-minute window)
            // ----------------------------------------------------------------
            struct RateLimitEntry
            {
                int count = 0;
                std::chrono::steady_clock::time_point windowStart{std::chrono::steady_clock::now()};
            };

            // Registered method: exactly one of the two handlers is set
            struct MethodEntry
            {
                MethodHandler simple;
                CancellableMethodHandler cancellable;
            };

            // Result of processing an initialize request. Each call carries its own
            // client capabilities back to the transport (no shared staging state).
            struct InitOutcome
            {
                bool succeeded = false;
                json clientCaps;
                std::string protocolVersion;  // negotiated version
            };

            // Result of processing one JSON-RPC message
            struct MessageOutcome
            {
                std::optional<json> response;  // nullopt → notification, no response
                bool invalid = false;          // message was rejected as -32600 Invalid Request
            };

            ServerConfig m_config;
            HttpServer m_httpServer;
            SessionManager m_sessionManager;
            std::map<std::string, MethodEntry> m_methods;
            mutable std::shared_mutex m_methodsMutex;
            std::atomic<bool> m_running{false};

            // SSE queues: sessionId → queue (push_event writes here, GET handler reads)
            std::map<std::string, std::shared_ptr<SSESessionQueue>> m_sseQueues;
            std::atomic<uint64_t> m_eventSequence{0};  // SSE event ids (resumability)
            std::mutex m_sseQueuesMutex;

            // Rate limiting
            std::map<std::string, RateLimitEntry> m_rateLimitMap;
            std::chrono::steady_clock::time_point m_rateLimitLastPrune{std::chrono::steady_clock::now()};
            std::mutex m_rateLimitMutex;

            // Cancellation tokens: [session, JSON-RPC request id] → cancel flag (set by notifications/cancelled)
            std::map<json, std::shared_ptr<std::atomic<bool>>> m_pendingCancellations;
            mutable std::mutex m_pendingMutex;

            // Client capabilities per session (stored on initialize; "" = STDIO)
            std::map<std::string, json> m_clientCapabilities;
            mutable std::mutex m_clientCapsMutex;

            // Minimum log level index per session for notifications/message, set by
            // logging/setLevel ("" = STDIO); sessions without an entry use "warning".
            static constexpr int kDefaultLogLevel = 3;  // "warning"
            std::map<std::string, int> m_logLevels;
            mutable std::mutex m_logLevelsMutex;

            // Negotiated protocol version per session (stored on initialize; "" = STDIO)
            std::map<std::string, std::string> m_protocolVersions;
            mutable std::mutex m_protocolVersionsMutex;

            // Typed helper registries (registerTool / registerPrompt / registerResource),
            // kept in registration order for the list methods.
            struct ToolEntry
            {
                json definition;
                CancellableToolHandler handler;
            };
            struct PromptEntry
            {
                json definition;
                PromptHandler handler;
            };
            struct ResourceEntry
            {
                json definition;
                ResourceReader reader;
            };
            std::vector<std::pair<std::string, ToolEntry>> m_tools;
            std::vector<std::pair<std::string, PromptEntry>> m_prompts;
            std::vector<std::pair<std::string, ResourceEntry>> m_resources;
            mutable std::shared_mutex m_helpersMutex;
            std::atomic<size_t> m_listPageSize{0};

            int minLogLevel(const std::string& sessionId) const
            {
                std::lock_guard<std::mutex> lock(m_logLevelsMutex);
                auto it = m_logLevels.find(sessionId);
                return it == m_logLevels.end() ? kDefaultLogLevel : it->second;
            }

            // ----------------------------------------------------------------
            // Small helpers
            // ----------------------------------------------------------------

            /// @brief Index of an MCP log level (RFC 5424 order), or -1 if unknown.
            static int logLevelIndex(const std::string& level)
            {
                static const char* const LEVELS[] = {
                    "debug", "info", "notice", "warning", "error", "critical", "alert", "emergency"
                };
                for (int i = 0; i < 8; ++i)
                {
                    if (level == LEVELS[i])
                        return i;
                }
                return -1;
            }

            static bool iequals(const std::string& a, const std::string& b)
            {
                if (a.size() != b.size())
                    return false;
                for (size_t i = 0; i < a.size(); ++i)
                {
                    if (std::tolower(static_cast<unsigned char>(a[i])) !=
                        std::tolower(static_cast<unsigned char>(b[i])))
                        return false;
                }
                return true;
            }

            /// @brief true if the request may use the MCP endpoint based on its Origin
            ///        header (see ServerConfig::allowedOrigins); otherwise answers 403.
            bool checkOrigin(const HttpRequest& req, HttpResponse& res)
            {
                const std::string* origin = findHeader(req, "Origin");
                if (origin == nullptr || isOriginAllowed(*origin))
                    return true;
                applyCorsHeaders(res);
                auto error = JsonRpcError::serverError(-32000, "Forbidden: origin not allowed");
                sendJson(res, 403, JsonRpcResponse::failure(nullptr, error).toJson());
                return false;
            }

            /// @brief Origin policy: loopback hosts, ServerConfig::allowedOrigins ("*" = any)
            ///        and a single-origin cors.allowOrigin are allowed.
            bool isOriginAllowed(const std::string& origin) const
            {
                for (const auto& allowed : m_config.allowedOrigins)
                {
                    if (allowed == "*" || iequals(allowed, origin))
                        return true;
                }
                if (m_config.cors.allowOrigin != "*" && iequals(m_config.cors.allowOrigin, origin))
                    return true;
                // scheme://host[:port] with a loopback host
                size_t sep = origin.find("://");
                if (sep == std::string::npos)
                    return false;
                std::string host = origin.substr(sep + 3);
                if (!host.empty() && host.front() == '[')
                {
                    size_t close = host.find(']');
                    if (close == std::string::npos)
                        return false;
                    std::string rest = host.substr(close + 1);
                    host = host.substr(0, close + 1);
                    if (!rest.empty() && rest.front() != ':')
                        return false;
                }
                else
                {
                    size_t colon = host.find(':');
                    if (colon != std::string::npos)
                        host = host.substr(0, colon);
                }
                return iequals(host, "localhost") || host == "127.0.0.1" || host == "[::1]";
            }

            /// @brief Case-insensitive request header lookup (HTTP field names are
            /// case-insensitive; the server may store them in a normalized form).
            static const std::string* findHeader(const HttpRequest& req, const std::string& name)
            {
                auto it = req.headers.find(name);
                if (it != req.headers.end())
                    return &it->second;
                for (const auto& kv : req.headers)
                {
                    if (iequals(kv.first, name))
                        return &kv.second;
                }
                return nullptr;
            }

            static std::string headerValue(const HttpRequest& req, const std::string& name)
            {
                const std::string* v = findHeader(req, name);
                return v ? *v : std::string();
            }

            /// @brief Compare secrets without an early exit on the first mismatch.
            static bool constantTimeEquals(const std::string& a, const std::string& b)
            {
                const size_t n = (std::max)(a.size(), b.size());
                size_t diff = a.size() ^ b.size();
                for (size_t i = 0; i < n; ++i)
                {
                    unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
                    unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
                    diff |= static_cast<size_t>(ca ^ cb);
                }
                return diff == 0;
            }

            static json invalidRequestResponse(const JsonRpcId& id, const std::string& message)
            {
                return JsonRpcResponse::failure(id, JsonRpcError::invalidRequest(message)).toJson();
            }

            static void sendJson(HttpResponse& res, int status, const json& body)
            {
                res.set_status(status);
                res.set_header("Content-Type", "application/json");
                res.send(body.dump());
            }

            static bool isInitializeRequest(const json& msg)
            {
                if (!msg.is_object() || !msg.contains("id"))
                    return false;
                auto it = msg.find("method");
                return it != msg.end() && it->is_string() && it->get<std::string>() == "initialize";
            }

            std::optional<MethodEntry> findMethod(const std::string& method) const
            {
                std::shared_lock<std::shared_mutex> lock(m_methodsMutex);
                auto it = m_methods.find(method);
                if (it == m_methods.end())
                    return std::nullopt;
                return it->second;
            }

            void storeClientCapabilities(const std::string& sessionId, const json& caps)
            {
                std::lock_guard<std::mutex> lock(m_clientCapsMutex);
                m_clientCapabilities[sessionId] = caps;
            }

            void storeProtocolVersion(const std::string& sessionId, const std::string& version)
            {
                std::lock_guard<std::mutex> lock(m_protocolVersionsMutex);
                m_protocolVersions[sessionId] = version;
            }

            static bool isSupportedVersion(const std::string& version)
            {
                return std::find(SUPPORTED_VERSIONS.begin(), SUPPORTED_VERSIONS.end(), version) !=
                       SUPPORTED_VERSIONS.end();
            }

            /// @brief Protocol version governing a request of @p sessionId: the version its
            /// initialize negotiated, else the MCP-Protocol-Version header value (if any),
            /// else 2025-03-26. @p req is null for STDIO (session ""); on HTTP an empty
            /// session id means "no session".
            std::string effectiveVersion(const std::string& sessionId, const HttpRequest* req) const
            {
                if (req == nullptr || !sessionId.empty())
                {
                    std::string negotiated = get_protocol_version(sessionId);
                    if (!negotiated.empty())
                        return negotiated;
                }
                if (req != nullptr)
                {
                    const std::string* header = findHeader(*req, "MCP-Protocol-Version");
                    if (header != nullptr && !header->empty())
                        return *header;
                }
                return DEFAULT_NEGOTIATED_VERSION;
            }

            /// @brief JSON-RPC batching was removed in MCP 2025-06-18. Versions are
            /// ISO dates, so string order is chronological.
            static bool batchingAllowed(const std::string& version)
            {
                return version < std::string(NO_BATCH_VERSION);
            }

            static json batchRejectedResponse(const std::string& version)
            {
                return invalidRequestResponse(nullptr,
                    "JSON-RPC batching is not supported in protocol version " + version);
            }

            /// @brief Validate the MCP-Protocol-Version header of a request after initialize
            /// (MCP 2025-06-18+). An absent header is accepted (the negotiated version, or
            /// 2025-03-26, is assumed). A value that is not a supported version is answered
            /// with 400; like the official TypeScript SDK, a supported version that differs
            /// from the one the session negotiated is accepted.
            /// @return false if a response has been sent
            bool checkProtocolVersion(const HttpRequest& req, HttpResponse& res)
            {
                const std::string* header = findHeader(req, "MCP-Protocol-Version");
                if (header == nullptr)
                    return true;
                std::string message;
                if (!isSupportedVersion(*header))
                {
                    std::string list;
                    for (const auto& v : SUPPORTED_VERSIONS)
                        list += (list.empty() ? "" : ", ") + v;
                    message = "Bad Request: Unsupported protocol version: " + *header +
                              " (supported versions: " + list + ")";
                }
                if (message.empty())
                    return true;
                auto error = JsonRpcError::serverError(-32000, message);
                sendJson(res, 400, JsonRpcResponse::failure(nullptr, error).toJson());
                return false;
            }

            /// @brief Drop all per-session state kept by the MCP layer (SSE queue, caps).
            void dropSessionState(const std::string& sessionId)
            {
                {
                    std::lock_guard<std::mutex> lock(m_logLevelsMutex);
                    m_logLevels.erase(sessionId);
                }
                {
                    std::lock_guard<std::mutex> lock(m_sseQueuesMutex);
                    auto it = m_sseQueues.find(sessionId);
                    if (it != m_sseQueues.end())
                    {
                        it->second->closed.store(true);
                        it->second->cv.notify_all();
                        m_sseQueues.erase(it);
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(m_clientCapsMutex);
                    m_clientCapabilities.erase(sessionId);
                }
                {
                    std::lock_guard<std::mutex> lock(m_protocolVersionsMutex);
                    m_protocolVersions.erase(sessionId);
                }
            }

            /// @brief Rate-limit key for a request: the TCP peer address, or — only when
            /// ServerConfig::trustProxyHeaders is set — the last X-Forwarded-For hop.
            std::string clientKey(const HttpRequest& req) const
            {
                if (m_config.trustProxyHeaders)
                {
                    std::string xff = headerValue(req, "X-Forwarded-For");
                    auto comma = xff.rfind(',');
                    std::string last = comma == std::string::npos ? xff : xff.substr(comma + 1);
                    auto b = last.find_first_not_of(" \t");
                    auto e = last.find_last_not_of(" \t");
                    if (b != std::string::npos)
                        return last.substr(b, e - b + 1);
                }

                // req.client is "a.b.c.d:port" or "[v6]:port" — strip the port
                const std::string& c = req.client;
                if (c.empty())
                    return "unknown";
                if (c[0] == '[')
                {
                    auto close = c.find(']');
                    return close == std::string::npos ? c : c.substr(1, close - 1);
                }
                auto colon = c.rfind(':');
                if (colon != std::string::npos && c.find(':') == colon)
                    return c.substr(0, colon);
                return c;
            }

            /// @brief Per-client window check.  Returns false and sets 429 if limit exceeded.
            bool checkRateLimit(const HttpRequest& req, HttpResponse& res)
            {
                if (m_config.maxRequestsPerMinute <= 0)
                    return true; // rate limiting disabled

                const std::string key = clientKey(req);
                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(m_rateLimitMutex);

                // Prune expired windows at most once a minute so the map cannot grow unbounded
                if (now - m_rateLimitLastPrune >= std::chrono::seconds(60))
                {
                    for (auto it = m_rateLimitMap.begin(); it != m_rateLimitMap.end(); )
                    {
                        if (now - it->second.windowStart >= std::chrono::seconds(60))
                            it = m_rateLimitMap.erase(it);
                        else
                            ++it;
                    }
                    m_rateLimitLastPrune = now;
                }

                auto& entry = m_rateLimitMap[key];
                if (now - entry.windowStart >= std::chrono::seconds(60))
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

            /// @brief Validate the session id of a non-initialize request.
            /// @param required  true → a missing header is rejected with HTTP 400
            ///                  (Streamable HTTP, MCP 2025-03-26)
            /// @return false if a response has been sent
            bool checkSession(const HttpRequest& req, HttpResponse& res, bool required)
            {
                if (!m_config.session.enabled)
                    return true;

                std::string sessionId = getSessionId(req);
                if (sessionId.empty())
                {
                    if (!required)
                        return true;
                    sendJson(res, 400, invalidRequestResponse(nullptr,
                        "Missing " + m_config.session.headerName + " header"));
                    return false;
                }
                if (!m_sessionManager.validateSession(sessionId))
                {
                    dropSessionState(sessionId);
                    auto error = JsonRpcError::serverError(-32001, "Invalid or expired session");
                    sendJson(res, 404, JsonRpcResponse::failure(nullptr, error).toJson());
                    return false;
                }
                return true;
            }

            /// @brief Create a session for a successful initialize and attach its id
            /// to the response. Returns the new session id ("" if none was created).
            std::string commitInitialize(const InitOutcome& init, HttpResponse& res, bool createQueue)
            {
                if (!init.succeeded || !m_config.session.enabled)
                    return "";
                std::string sessionId = m_sessionManager.createSession();
                if (createQueue)
                {
                    // Pre-create SSE queue so events pushed before the GET stream opens are kept
                    std::lock_guard<std::mutex> lock(m_sseQueuesMutex);
                    m_sseQueues[sessionId] = std::make_shared<SSESessionQueue>();
                }
                storeClientCapabilities(sessionId, init.clientCaps);
                storeProtocolVersion(sessionId, init.protocolVersion);
                res.set_header(m_config.session.headerName, sessionId);
                return sessionId;
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
            //   - Every request except initialize must carry Mcp-Session-Id once
            //     sessions are enabled (missing → 400, unknown/expired → 404)
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
                    if (!checkOrigin(req, res))
                    {
                        return 0;
                    }
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
                        handleHttpOptions(res);
                    }
                    else
                    {
                        res.set_status(405);
                        res.set_header("Allow", "GET, POST, DELETE, OPTIONS");
                        res.send("");
                    }
                    return 0;
                });

                setupHealthRoute();
            }

            /// @brief GET /health → health / discovery JSON (nginx upstream health checks)
            void setupHealthRoute()
            {
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

                if (!checkRateLimit(req, res)) return;
                if (!authenticate(req, res)) return;

                // Negotiate response format
                const bool wantsSSE =
                    headerValue(req, "Accept").find("text/event-stream") != std::string::npos;

                // Helper: send a set of responses either as SSE or JSON
                auto sendResponses = [&](const std::vector<json>& resps, bool isBatch)
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
                            body += "data: " + r.dump() + "\n\n";
                        res.send(body);
                    }
                    else if (!isBatch)
                    {
                        sendJson(res, 200, resps[0]);
                    }
                    else
                    {
                        sendJson(res, 200, json(resps));
                    }
                };

                json body;
                try
                {
                    body = json::parse(req.content);
                }
                catch (const json::parse_error& e)
                {
                    sendJson(res, 400, JsonRpcResponse::failure(nullptr,
                        JsonRpcError::parseError(e.what())).toJson());
                    return;
                }

                try
                {
                    if (body.is_array())
                    {
                        if (body.empty())
                        {
                            sendJson(res, 400, invalidRequestResponse(nullptr, "Empty batch"));
                            return;
                        }
                        if (!checkSession(req, res, true)) return;
                        if (!checkProtocolVersion(req, res)) return;
                        const std::string version = effectiveVersion(getSessionId(req), &req);
                        if (!batchingAllowed(version))
                        {
                            sendJson(res, 400, batchRejectedResponse(version));
                            return;
                        }

                        std::vector<json> responses;
                        for (const auto& item : body)
                        {
                            auto outcome = processOne(item, nullptr, getSessionId(req));
                            if (outcome.response)
                                responses.push_back(std::move(*outcome.response));
                        }
                        sendResponses(responses, true);
                        return;
                    }

                    if (!body.is_object())
                    {
                        sendJson(res, 400, invalidRequestResponse(nullptr,
                            "Request must be a JSON object or array"));
                        return;
                    }

                    const bool isInit = isInitializeRequest(body);
                    if (!isInit && !checkSession(req, res, true)) return;
                    if (!isInit && !checkProtocolVersion(req, res)) return;

                    InitOutcome init;
                    auto outcome = processOne(body, isInit ? &init : nullptr, getSessionId(req));
                    if (!outcome.response)
                    {
                        res.set_status(202);
                        res.send("");
                        return;
                    }
                    if (outcome.invalid)
                    {
                        sendJson(res, 400, *outcome.response);
                        return;
                    }
                    // The session exists only once initialize has succeeded
                    commitInitialize(init, res, true);
                    sendResponses({*outcome.response}, false);
                }
                catch (const std::exception& e)
                {
                    sendJson(res, 500, JsonRpcResponse::failure(nullptr,
                        JsonRpcError::internalError(e.what())).toJson());
                }
            }

            /// @brief Setup routes based on transport type
            void setupHttpRoutes()
            {
                std::string endpoint = m_config.endpoint;

                setupHealthRoute();

                // POST: Handle JSON-RPC requests
                m_httpServer.route(endpoint, [this](const HttpRequest& req, HttpResponse& res) -> int {
                    if (!checkOrigin(req, res))
                    {
                        return 0;
                    }
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
                        handleHttpOptions(res);
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

            /// @brief Handle HTTP POST request (JSON-RPC, HTTP+SSE transport).
            /// The session header is optional here (validated only when present).
            void handleHttpPost(const HttpRequest& req, HttpResponse& res)
            {
                applyCorsHeaders(res);

                if (!checkRateLimit(req, res)) return;
                if (!authenticate(req, res)) return;

                // Check Content-Type
                if (headerValue(req, "Content-Type").find("application/json") == std::string::npos)
                {
                    sendJson(res, 400, invalidRequestResponse(nullptr,
                        "Content-Type must be application/json"));
                    return;
                }

                json body;
                try
                {
                    body = json::parse(req.content);
                }
                catch (const json::parse_error& e)
                {
                    sendJson(res, 400, JsonRpcResponse::failure(nullptr,
                        JsonRpcError::parseError(e.what())).toJson());
                    return;
                }

                try
                {
                    if (body.is_array())
                    {
                        if (body.empty())
                        {
                            sendJson(res, 400, invalidRequestResponse(nullptr, "Empty batch"));
                            return;
                        }
                        if (!checkSession(req, res, false)) return;
                        if (!checkProtocolVersion(req, res)) return;
                        const std::string version = effectiveVersion(getSessionId(req), &req);
                        if (!batchingAllowed(version))
                        {
                            sendJson(res, 400, batchRejectedResponse(version));
                            return;
                        }

                        json responses = json::array();
                        for (const auto& item : body)
                        {
                            auto outcome = processOne(item, nullptr, getSessionId(req));
                            if (outcome.response)
                                responses.push_back(std::move(*outcome.response));
                        }
                        if (responses.empty())
                        {
                            res.set_status(202);
                            res.send("");
                            return;
                        }
                        sendJson(res, 200, responses);
                        return;
                    }

                    if (!body.is_object())
                    {
                        sendJson(res, 400, invalidRequestResponse(nullptr,
                            "Request must be a JSON object or array"));
                        return;
                    }

                    const bool isInit = isInitializeRequest(body);
                    if (!isInit && !checkSession(req, res, false)) return;
                    if (!isInit && !checkProtocolVersion(req, res)) return;

                    InitOutcome init;
                    auto outcome = processOne(body, isInit ? &init : nullptr, getSessionId(req));
                    if (!outcome.response)
                    {
                        // JSON-RPC notification — 202 Accepted (no body)
                        res.set_status(202);
                        res.send("");
                        return;
                    }
                    if (outcome.invalid)
                    {
                        sendJson(res, 400, *outcome.response);
                        return;
                    }

                    std::string sessionId = commitInitialize(init, res, false);

                    const bool wantsSSE =
                        headerValue(req, "Accept").find("text/event-stream") != std::string::npos;
                    if (isInit && wantsSSE && m_config.responseMode == ServerConfig::ResponseMode::STREAM)
                    {
                        // Send response via SSE (one complete event, then end of stream)
                        res.set_status(200);
                        res.set_header("Content-Type", "text/event-stream");
                        res.set_header("Cache-Control", "no-cache");
                        res.set_header("Connection", "keep-alive");

                        SSEEvent event;
                        event.id = "init-1";
                        event.data = outcome.response->dump();

                        if (m_config.resumability.enabled && !sessionId.empty())
                        {
                            m_sessionManager.addEvent(sessionId, event.id, event.format());
                        }

                        res.send(event.format());
                        return;
                    }

                    sendJson(res, 200, *outcome.response);
                }
                catch (const std::exception& e)
                {
                    sendJson(res, 500, JsonRpcResponse::failure(nullptr,
                        JsonRpcError::internalError(e.what())).toJson());
                }
            }

            /// @brief Handle HTTP GET request — real SSE push stream (backport from FMcpNativeTransport)
            ///
            /// Uses the per-session blocking event queue shared with push_event().
            /// The send_chunk_stream callback waits up to 200 ms per call for an event, sends
            /// an empty SSE comment when idle and ": keepalive" every writeDeadlineSeconds, and
            /// returns "" (ending the stream) only when the queue is closed or destroyed.
            void handleHttpGet(const HttpRequest& req, HttpResponse& res)
            {
                applyCorsHeaders(res);

                if (!authenticate(req, res)) return;
                if (!checkRateLimit(req, res)) return;

                // Get session ID: prefer Mcp-Session-Id header (2025-03-26 spec),
                // fall back to ?session= query param for compatibility.
                std::string sessionId = getSessionId(req);
                if (sessionId.empty())
                {
                    std::map<std::string, std::string> params;
                    try
                    {
                        params = req.parse_query();
                    }
                    catch (const std::exception&)
                    {
                        // malformed query → treated as missing session
                    }
                    auto sessionIt = params.find("session");
                    if (sessionIt == params.end() || sessionIt->second.empty())
                    {
                        res.set_status(400);
                        res.send("Missing " + m_config.session.headerName + " header");
                        return;
                    }
                    sessionId = sessionIt->second;
                }
                if (!m_sessionManager.validateSession(sessionId))
                {
                    dropSessionState(sessionId);
                    res.set_status(404);
                    res.send("Invalid or expired session");
                    return;
                }
                if (!checkProtocolVersion(req, res)) return;

                // Check for Last-Event-ID (resumability)
                std::string lastEventId = headerValue(req, "Last-Event-ID");

                // Setup SSE stream headers
                res.set_status(200);
                res.set_header("Content-Type", "text/event-stream");
                res.set_header("Cache-Control", "no-cache");
                res.set_header("Connection", "keep-alive");
                res.set_header(m_config.session.headerName, sessionId);

                // Install a fresh queue for this stream. Events already queued for the
                // session (e.g. pushed between initialize and this GET) are carried over,
                // and any previous stream for the session is told to close.
                // When resuming (Last-Event-ID + resumability), the session history is
                // the single source of truth: it holds everything after that id,
                // including events still pending on the previous stream, so those are
                // not carried over (they would be delivered twice).
                const bool resuming = !lastEventId.empty() && m_config.resumability.enabled;
                auto queue = std::make_shared<SSESessionQueue>();
                {
                    std::lock_guard<std::mutex> lock(m_sseQueuesMutex);
                    auto existing = m_sseQueues.find(sessionId);
                    if (existing != m_sseQueues.end())
                    {
                        auto& old = existing->second;
                        std::lock_guard<std::mutex> qlock(old->mutex);
                        while (!resuming && !old->events.empty())
                        {
                            queue->events.push(std::move(old->events.front()));
                            old->events.pop();
                        }
                        old->closed.store(true);
                        old->cv.notify_all();
                    }
                    if (resuming)
                    {
                        // Under m_sseQueuesMutex, so no push_event() can slip in between
                        // the replay and the new queue becoming visible.
                        for (auto& event : m_sessionManager.getEventsSince(sessionId, lastEventId))
                        {
                            queue->events.push(std::move(event));
                        }
                    }
                    m_sseQueues[sessionId] = queue;
                }

                // Write deadline: if no event arrives within this many seconds, send a keepalive
                // comment and reset; close the stream if the session has been closed.
                const int writeDeadlineSeconds = m_config.sseWriteDeadlineSeconds > 0
                    ? m_config.sseWriteDeadlineSeconds : 30;

                // send_chunk_stream callback — called by HTTP server for each chunk.
                // Returns "" to signal end-of-stream.
                //
                // We use a short poll interval and send a minimal SSE comment each time
                // so the executing thread yields between keepalive cycles.
                auto weakQueue = std::weak_ptr<SSESessionQueue>(queue);
                auto lastKeepalive = std::make_shared<std::chrono::steady_clock::time_point>(
                    std::chrono::steady_clock::now());
                res.send_chunk_stream([weakQueue, writeDeadlineSeconds, lastKeepalive]() -> std::string
                {
                    auto q = weakQueue.lock();
                    if (!q)
                        return ""; // queue destroyed — close stream

                    std::unique_lock<std::mutex> lock(q->mutex);
                    // Short poll so the thread is never blocked > 200 ms.
                    bool signalled = q->cv.wait_for(
                        lock,
                        std::chrono::milliseconds(200),
                        [&q] { return !q->events.empty() || q->closed.load(); });

                    if (q->closed.load() && q->events.empty())
                        return ""; // close stream

                    if (!signalled)
                    {
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            now - *lastKeepalive).count();
                        if (elapsed >= writeDeadlineSeconds)
                        {
                            *lastKeepalive = now;
                            q->lastActivity = now;
                            return ": keepalive\n\n";
                        }
                        return ": \n\n"; // silent SSE comment — client ignores
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
                applyCorsHeaders(res);

                if (!authenticate(req, res)) return;

                if (!m_config.session.allowClientTermination)
                {
                    res.set_status(405);
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
                if (!checkProtocolVersion(req, res)) return;

                const bool existed = m_sessionManager.terminateSession(sessionId);
                dropSessionState(sessionId);
                if (existed)
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
            void handleHttpOptions(HttpResponse& res)
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
            /// Header names are matched case-insensitively. Fails closed: when auth is
            /// enabled but no validator/secret is configured, requests get 500.
            /// @return true if authenticated or auth not required, false otherwise
            bool authenticate(const HttpRequest& req, HttpResponse& res)
            {
                if (!m_config.auth.enabled)
                {
                    return true;
                }

                // Capability token check (backport from FMcpNativeTransport).
                if (m_config.auth.type == ServerConfig::AuthConfig::Type::CAPABILITY_TOKEN)
                {
                    const std::string* cap = findHeader(req, "X-MCP-Capability-Token");
                    if (cap == nullptr || cap->empty())
                    {
                        res.set_status(401);
                        res.set_header("WWW-Authenticate", "MCP-Capability-Token");
                        res.send("X-MCP-Capability-Token required");
                        return false;
                    }
                    bool ok = false;
                    if (m_config.auth.validator)
                    {
                        ok = m_config.auth.validator(*cap);
                    }
                    else if (m_config.auth.secretOrPublicKey.has_value() &&
                             !m_config.auth.secretOrPublicKey->empty())
                    {
                        ok = constantTimeEquals(*cap, *m_config.auth.secretOrPublicKey);
                    }
                    else
                    {
                        res.set_status(500);
                        res.send("Server authentication misconfigured");
                        return false;
                    }
                    if (!ok)
                    {
                        res.set_status(401);
                        res.send("Invalid capability token");
                        return false;
                    }
                    return true;
                }

                const std::string* authHeader = findHeader(req, m_config.auth.headerName);
                if (authHeader == nullptr)
                {
                    res.set_status(401);
                    res.set_header("WWW-Authenticate", "Bearer");
                    res.send("Authentication required");
                    return false;
                }

                std::string token = *authHeader;

                // Strip "Bearer " prefix if present (auth scheme is case-insensitive)
                if (m_config.auth.type == ServerConfig::AuthConfig::Type::BEARER)
                {
                    if (token.size() >= 7 && iequals(token.substr(0, 7), "Bearer "))
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
                // JWT validation if secret provided and no custom validator. An empty
                // HMAC key would let anyone mint valid tokens, so it counts as
                // misconfigured (falls through to the 500 below).
                if (m_config.auth.type == ServerConfig::AuthConfig::Type::BEARER &&
                    m_config.auth.secretOrPublicKey.has_value() &&
                    !m_config.auth.secretOrPublicKey->empty())
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

                // Secret comparison for API key type
                if (m_config.auth.type == ServerConfig::AuthConfig::Type::API_KEY &&
                    m_config.auth.secretOrPublicKey.has_value() &&
                    !m_config.auth.secretOrPublicKey->empty())
                {
                    if (!constantTimeEquals(token, m_config.auth.secretOrPublicKey.value()))
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

            /// @brief Get session ID from request headers (case-insensitive)
            std::string getSessionId(const HttpRequest& req) const
            {
                return headerValue(req, m_config.session.headerName);
            }

            /// Session of the message being dispatched on this thread. Handlers run
            /// synchronously inside processOne(), so a thread-local is sufficient.
            static std::string& currentSession()
            {
                thread_local std::string session;
                return session;
            }

            struct SessionScope
            {
                std::string previous;
                explicit SessionScope(const std::string& session) : previous(currentSession())
                {
                    currentSession() = session;
                }
                ~SessionScope() { currentSession() = previous; }
                SessionScope(const SessionScope&) = delete;
                SessionScope& operator=(const SessionScope&) = delete;
            };

            /// Cancellation tokens are keyed by (session, request id) so that ids reused
            /// across sessions never collide.
            static json cancellationKey(const JsonRpcId& id)
            {
                return json::array({currentSession(), jsonRpcIdToJson(id)});
            }

            /// @brief Validate and dispatch one JSON-RPC message.
            /// @param init  non-null only where an initialize request is allowed
            ///              (a single, non-batched message); receives its outcome.
            /// @param session  session the message belongs to ("" for STDIO / sessionless);
            ///                  scopes request ids for cancellation.
            MessageOutcome processOne(const json& msg, InitOutcome* init,
                                      const std::string& session = std::string())
            {
                SessionScope scope(session);
                MessageOutcome out;
                auto invalid = [&out](const JsonRpcId& id, const std::string& why) {
                    out.response = invalidRequestResponse(id, why);
                    out.invalid = true;
                    return out;
                };

                if (!msg.is_object())
                    return invalid(nullptr, "Request must be a JSON object");

                JsonRpcId id;  // monostate → notification
                auto idIt = msg.find("id");
                if (idIt != msg.end() && !jsonRpcIdFromJson(*idIt, id))
                    return invalid(nullptr, "Invalid id: must be a string, integer or null");

                // Invalid messages without a usable id are answered with id null
                const JsonRpcId replyId = jsonRpcIdPresent(id) ? id : JsonRpcId{nullptr};

                auto verIt = msg.find("jsonrpc");
                if (verIt != msg.end() && !(verIt->is_string() && verIt->get<std::string>() == "2.0"))
                    return invalid(replyId, "jsonrpc must be \"2.0\"");

                auto methodIt = msg.find("method");
                if (methodIt == msg.end() && jsonRpcIdPresent(id) &&
                    (msg.contains("result") || msg.contains("error")))
                {
                    // A JSON-RPC response from the client. This server never sends
                    // requests to clients, so there is nothing to match it with; accept
                    // it without a reply (HTTP 202, as the spec requires for responses).
                    LOG_DEBUG("%s", "MCPServer: ignoring a JSON-RPC response sent by the client");
                    return out;
                }
                if (methodIt == msg.end() || !methodIt->is_string())
                    return invalid(replyId, "Missing or invalid method");
                const std::string method = methodIt->get<std::string>();

                json params = json::object();
                auto paramsIt = msg.find("params");
                if (paramsIt != msg.end())
                {
                    if (!paramsIt->is_object() && !paramsIt->is_array())
                        return invalid(replyId, "params must be an object or array");
                    params = *paramsIt;
                }

                if (!jsonRpcIdPresent(id))
                {
                    handleNotification(method, params);
                    return out;  // notifications never get a response
                }

                bool cancelled = false;
                JsonRpcResponse response = handleRequest(method, id, params, init, cancelled);
                if (!cancelled)
                    out.response = response.toJson();  // no reply to a cancelled request
                return out;
            }

            /// @brief Choose the protocol version to answer an initialize with.
            /// Per MCP spec: the client's version if supported, otherwise the latest
            /// version this server supports (the client decides whether to proceed).
            static std::string negotiateVersion(const std::string& clientVersion)
            {
                for (const auto& v : SUPPORTED_VERSIONS)
                {
                    if (v == clientVersion)
                        return v;
                }
                return SUPPORTED_VERSIONS.front();
            }

            /// @brief Handle a JSON-RPC request (a message with an id)
            /// @param cancelled Set to true when the request was cancelled while it ran
            ///        (notifications/cancelled); the caller then sends no response.
            JsonRpcResponse handleRequest(const std::string& method, const JsonRpcId& id,
                                          const json& params, InitOutcome* init, bool& cancelled)
            {
                if (method == "initialize")
                {
                    if (init == nullptr)
                    {
                        return JsonRpcResponse::failure(id, JsonRpcError::invalidRequest(
                            "initialize must not be part of a JSON-RPC batch"));
                    }

                    std::string clientVer;
                    if (params.is_object() && params.contains("protocolVersion") &&
                        params["protocolVersion"].is_string())
                    {
                        clientVer = params["protocolVersion"].get<std::string>();
                    }
                    const std::string negotiated = negotiateVersion(clientVer);

                    json caps = json::object();
                    if (params.is_object() && params.contains("capabilities"))
                        caps = params["capabilities"];

                    JsonRpcResponse response;
                    auto entry = findMethod("initialize");
                    if (entry && entry->simple)
                    {
                        try
                        {
                            json result = entry->simple(params);
                            if (!result.is_object())
                                result = json::object();
                            // Patch protocolVersion to the negotiated value
                            result["protocolVersion"] = negotiated;
                            // Add the capabilities of the typed helpers the handler did
                            // not declare itself (its own entries are kept as they are)
                            json helperCaps = helperCapabilities();
                            if (!helperCaps.empty())
                            {
                                if (!result.contains("capabilities") || !result["capabilities"].is_object())
                                    result["capabilities"] = json::object();
                                for (auto& cap : helperCaps.items())
                                {
                                    if (!result["capabilities"].contains(cap.key()))
                                        result["capabilities"][cap.key()] = cap.value();
                                }
                            }
                            response = JsonRpcResponse::success(id, result);
                        }
                        catch (const JsonRpcError& err)
                        {
                            return JsonRpcResponse::failure(id, err);
                        }
                        catch (const std::exception& e)
                        {
                            return JsonRpcResponse::failure(id, JsonRpcError::internalError(e.what()));
                        }
                    }
                    else
                    {
                        // No user handler: return a minimal valid initialize response
                        response = JsonRpcResponse::success(id, {
                            {"protocolVersion", negotiated},
                            {"capabilities",    helperCapabilities()},
                            {"serverInfo",      {{"name", server_info()["name"]},
                                                 {"version", server_info()["version"]}}}
                        });
                    }
                    init->succeeded = true;
                    init->clientCaps = std::move(caps);
                    init->protocolVersion = negotiated;
                    return response;
                }

                auto entry = findMethod(method);
                if (!entry)
                    entry = findHelperMethod(method);
                if (!entry)
                {
                    return JsonRpcResponse::failure(id, JsonRpcError::methodNotFound(method));
                }

                try
                {
                    json result = entry->cancellable
                        ? invokeCancellable(entry->cancellable, id, params, cancelled)
                        : entry->simple(params);
                    return JsonRpcResponse::success(id, result);
                }
                catch (const JsonRpcError& error)
                {
                    return JsonRpcResponse::failure(id, error);
                }
                catch (const std::exception& e)
                {
                    return JsonRpcResponse::failure(id, JsonRpcError::internalError(e.what()));
                }
            }

            /// @brief Run a cancellable handler with its token registered under
            /// (current session, JSON-RPC id), so notifications/cancelled{requestId} from
            /// the same session can find it.
            /// @param cancelled Receives whether the token was set when the handler
            ///        finished (returned or threw).
            json invokeCancellable(const CancellableMethodHandler& handler, const JsonRpcId& id,
                                   const json& params, bool& cancelled)
            {
                auto token = std::make_shared<std::atomic<bool>>(false);
                const json key = cancellationKey(id);
                {
                    std::lock_guard<std::mutex> lock(m_pendingMutex);
                    m_pendingCancellations[key] = token;
                }

                struct Unregister
                {
                    MCPServer* self;
                    const json& key;
                    const std::shared_ptr<std::atomic<bool>>& token;
                    bool& cancelled;
                    ~Unregister()
                    {
                        cancelled = token->load();
                        std::lock_guard<std::mutex> lock(self->m_pendingMutex);
                        auto it = self->m_pendingCancellations.find(key);
                        if (it != self->m_pendingCancellations.end() && it->second == token)
                            self->m_pendingCancellations.erase(it);
                    }
                } unregister{this, key, token, cancelled};

                return handler(params, token);
            }

            /// @brief Handle JSON-RPC notification
            void handleNotification(const std::string& method, const json& params)
            {
                auto entry = findMethod(method);
                if (!entry)
                {
                    LOG_WARN("MCPServer: Unknown notification method: %s", method.c_str());
                    return;
                }

                try
                {
                    // Notifications don't return values
                    if (entry->cancellable)
                        entry->cancellable(params, std::make_shared<std::atomic<bool>>(false));
                    else
                        entry->simple(params);
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("MCPServer: Error handling notification %s: %s",
                             method.c_str(), e.what());
                    (void)e;
                }
                catch (...)
                {
                    LOG_ERROR("MCPServer: Error handling notification %s", method.c_str());
                }
            }

            // ── Typed helper implementation ──────────────────────────────────────────

            static json makeToolDefinition(const std::string& name, const std::string& description,
                                           const json& inputSchema)
            {
                json definition = {{"name", name}};
                if (!description.empty())
                    definition["description"] = description;
                definition["inputSchema"] = inputSchema;
                return definition;
            }

            void addTool(json definition, CancellableToolHandler handler)
            {
                if (!definition.is_object() || !definition.contains("name") ||
                    !definition["name"].is_string() || definition["name"].get<std::string>().empty())
                {
                    throw std::invalid_argument("registerTool: the tool needs a non-empty name");
                }
                if (!definition.contains("inputSchema") || definition["inputSchema"].is_null())
                    definition["inputSchema"] = {{"type", "object"}};
                else if (!definition["inputSchema"].is_object())
                    throw std::invalid_argument("registerTool: inputSchema must be a JSON object");

                const std::string name = definition["name"].get<std::string>();
                std::unique_lock<std::shared_mutex> lock(m_helpersMutex);
                upsert(m_tools, name, ToolEntry{std::move(definition), std::move(handler)});
            }

            /// Replace the entry with key @p key in place, or append it.
            template <typename Entry>
            static void upsert(std::vector<std::pair<std::string, Entry>>& entries,
                               const std::string& key, Entry entry)
            {
                for (auto& e : entries)
                {
                    if (e.first == key)
                    {
                        e.second = std::move(entry);
                        return;
                    }
                }
                entries.emplace_back(key, std::move(entry));
            }

            /// Capabilities implied by the typed helpers in use ({} if none).
            json helperCapabilities() const
            {
                json caps = json::object();
                std::shared_lock<std::shared_mutex> lock(m_helpersMutex);
                if (!m_tools.empty())
                    caps["tools"] = json::object();
                if (!m_prompts.empty())
                    caps["prompts"] = json::object();
                if (!m_resources.empty())
                    caps["resources"] = json::object();
                return caps;
            }

            /// Built-in handler for a helper-provided method, used only when no handler is
            /// registered under that name and at least one entry of that kind exists.
            std::optional<MethodEntry> findHelperMethod(const std::string& method)
            {
                bool tools = false, prompts = false, resources = false;
                {
                    std::shared_lock<std::shared_mutex> lock(m_helpersMutex);
                    tools = !m_tools.empty();
                    prompts = !m_prompts.empty();
                    resources = !m_resources.empty();
                }
                MethodEntry entry;
                if (tools && method == "tools/list")
                    entry.simple = [this](const json& params) {
                        return listPage(m_tools, "tools", params);
                    };
                else if (tools && method == "tools/call")
                    entry.cancellable = [this](const json& params,
                                               std::shared_ptr<std::atomic<bool>> token) {
                        return callTool(params, std::move(token));
                    };
                else if (prompts && method == "prompts/list")
                    entry.simple = [this](const json& params) {
                        return listPage(m_prompts, "prompts", params);
                    };
                else if (prompts && method == "prompts/get")
                    entry.simple = [this](const json& params) { return getPrompt(params); };
                else if (resources && method == "resources/list")
                    entry.simple = [this](const json& params) {
                        return listPage(m_resources, "resources", params);
                    };
                else if (resources && method == "resources/read")
                    entry.simple = [this](const json& params) { return readResource(params); };
                else
                    return std::nullopt;
                return entry;
            }

            /// One page of a helper registry for a */list request: {key: [...],
            /// "nextCursor"?}. The cursor is the decimal index of the next entry.
            template <typename Entry>
            json listPage(const std::vector<std::pair<std::string, Entry>>& entries,
                          const char* key, const json& params) const
            {
                size_t start = 0;
                if (params.is_object() && params.contains("cursor") && !params["cursor"].is_null())
                {
                    const json& cursor = params["cursor"];
                    const std::string text = cursor.is_string() ? cursor.get<std::string>() : "";
                    if (text.empty() || text.size() > 18 ||
                        text.find_first_not_of("0123456789") != std::string::npos)
                    {
                        throw JsonRpcError::invalidParams("Invalid cursor");
                    }
                    start = static_cast<size_t>(std::stoull(text));
                }
                const size_t pageSize = m_listPageSize.load();

                json items = json::array();
                std::shared_lock<std::shared_mutex> lock(m_helpersMutex);
                if (start > entries.size())
                    throw JsonRpcError::invalidParams("Invalid cursor");
                size_t end = entries.size();
                if (pageSize > 0 && end - start > pageSize)
                    end = start + pageSize;
                for (size_t i = start; i < end; ++i)
                    items.push_back(entries[i].second.definition);

                json result = {{key, std::move(items)}};
                if (end < entries.size())
                    result["nextCursor"] = std::to_string(end);
                return result;
            }

            /// Wrap a tool handler's return value into a CallToolResult.
            static json toolResult(json value)
            {
                auto text = [](const std::string& s) {
                    return json::array({{{"type", "text"}, {"text", s}}});
                };
                if (value.is_object() && value.contains("content"))
                    return value;
                if (value.is_null())
                    return {{"content", json::array()}};
                if (value.is_string())
                    return {{"content", text(value.get<std::string>())}};
                if (value.is_object())
                {
                    json result = {{"content", text(value.dump())}};
                    result["structuredContent"] = std::move(value);
                    return result;
                }
                return {{"content", text(value.dump())}};
            }

            json callTool(const json& params, std::shared_ptr<std::atomic<bool>> token)
            {
                if (!params.is_object() || !params.contains("name") || !params["name"].is_string())
                    throw JsonRpcError::invalidParams("tools/call: missing tool name");
                const std::string name = params["name"].get<std::string>();
                json args = json::object();
                if (params.contains("arguments") && !params["arguments"].is_null())
                {
                    if (!params["arguments"].is_object())
                        throw JsonRpcError::invalidParams("tools/call: arguments must be an object");
                    args = params["arguments"];
                }

                CancellableToolHandler handler;
                {
                    std::shared_lock<std::shared_mutex> lock(m_helpersMutex);
                    for (const auto& t : m_tools)
                    {
                        if (t.first == name)
                        {
                            handler = t.second.handler;
                            break;
                        }
                    }
                }
                if (!handler)
                    throw JsonRpcError::invalidParams("Unknown tool: " + name);

                // Tool execution errors are reported in the result (isError) so the model
                // can see them; JsonRpcError still becomes a protocol error.
                auto failure = [](const std::string& message) -> json {
                    return {{"content", json::array({{{"type", "text"}, {"text", message}}})},
                            {"isError", true}};
                };
                try
                {
                    return toolResult(handler(args, std::move(token)));
                }
                catch (const JsonRpcError&)
                {
                    throw;
                }
                catch (const std::exception& e)
                {
                    return failure(e.what());
                }
                catch (...)
                {
                    return failure("Unknown error in tool '" + name + "'");
                }
            }

            json getPrompt(const json& params)
            {
                if (!params.is_object() || !params.contains("name") || !params["name"].is_string())
                    throw JsonRpcError::invalidParams("prompts/get: missing prompt name");
                const std::string name = params["name"].get<std::string>();
                json args = json::object();
                if (params.contains("arguments") && !params["arguments"].is_null())
                {
                    if (!params["arguments"].is_object())
                        throw JsonRpcError::invalidParams("prompts/get: arguments must be an object");
                    args = params["arguments"];
                }

                std::optional<PromptEntry> entry;
                {
                    std::shared_lock<std::shared_mutex> lock(m_helpersMutex);
                    for (const auto& p : m_prompts)
                    {
                        if (p.first == name)
                        {
                            entry = p.second;
                            break;
                        }
                    }
                }
                if (!entry)
                    throw JsonRpcError::invalidParams("Unknown prompt: " + name);

                for (const auto& arg : entry->definition.value("arguments", json::array()))
                {
                    if (arg.is_object() && arg.value("required", false) && arg.contains("name") &&
                        arg["name"].is_string() && !args.contains(arg["name"].get<std::string>()))
                    {
                        throw JsonRpcError::invalidParams("Missing required argument: " +
                                                          arg["name"].get<std::string>());
                    }
                }

                json value = entry->handler(args);
                json result;
                if (value.is_object() && value.contains("messages"))
                    result = std::move(value);
                else if (value.is_array())
                    result = {{"messages", std::move(value)}};
                else if (value.is_string())
                    result = {{"messages", json::array({
                        {{"role", "user"}, {"content", {{"type", "text"}, {"text", value}}}}})}};
                else
                    throw JsonRpcError::internalError("Prompt '" + name + "' returned no messages");
                if (!result.contains("description") && entry->definition.contains("description"))
                    result["description"] = entry->definition["description"];
                return result;
            }

            json readResource(const json& params)
            {
                if (!params.is_object() || !params.contains("uri") || !params["uri"].is_string())
                    throw JsonRpcError::invalidParams("resources/read: missing uri");
                const std::string uri = params["uri"].get<std::string>();

                std::optional<ResourceEntry> entry;
                {
                    std::shared_lock<std::shared_mutex> lock(m_helpersMutex);
                    for (const auto& r : m_resources)
                    {
                        if (r.first == uri)
                        {
                            entry = r.second;
                            break;
                        }
                    }
                }
                if (!entry)
                {
                    JsonRpcError error = JsonRpcError::serverError(-32002, "Resource not found");
                    error.data = json{{"uri", uri}};
                    throw error;
                }

                json value = entry->reader(uri);
                if (value.is_object() && value.contains("contents"))
                    return value;
                json contents;
                if (value.is_array())
                    contents = std::move(value);
                else if (value.is_object())
                    contents = json::array({std::move(value)});
                else if (value.is_string())
                    contents = json::array({{{"text", value}}});
                else
                    throw JsonRpcError::internalError("Resource '" + uri + "' returned no contents");
                for (auto& item : contents)
                {
                    if (!item.is_object())
                        continue;
                    if (!item.contains("uri"))
                        item["uri"] = uri;
                    if (!item.contains("mimeType") && entry->definition.contains("mimeType"))
                        item["mimeType"] = entry->definition["mimeType"];
                }
                return {{"contents", std::move(contents)}};
            }
        };

    } // namespace server
} // namespace mcp
SOCKETSHPP_NS_END
