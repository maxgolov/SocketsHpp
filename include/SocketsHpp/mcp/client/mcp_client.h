// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file mcp_client.h
/// @brief MCP (Model Context Protocol) client over HTTP, Streamable HTTP or STDIO:
///        mcp::client::MCPClient.

#include <SocketsHpp/config.h>
#include <SocketsHpp/http/client/http_client.h>
#include <SocketsHpp/http/client/sse_client.h>
#include <SocketsHpp/http/common/json_rpc.h>
#include <SocketsHpp/mcp/common/mcp_config.h>
#include <SocketsHpp/utils/process.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

SOCKETSHPP_NS_BEGIN
namespace mcp
{
    namespace client
    {
        using namespace http::client;
        using namespace http::common;
        using json = nlohmann::json;  ///< nlohmann::json alias.

        /// @brief Synchronous MCP client.
        ///
        /// Transports (ClientConfig::transport):
        ///   - TransportType::STDIO: connect() launches the server process from
        ///     ClientConfig::stdio with piped stdin/stdout and exchanges newline-delimited
        ///     JSON-RPC messages with it. A reader thread matches responses to requests by
        ///     id and dispatches notifications; the server's stderr is inherited, or
        ///     delivered line by line to onStderr(). Server-to-client `ping` requests are
        ///     answered; other server requests get -32601 (method not found).
        ///   - TransportType::HTTP and TransportType::HTTP_STREAMABLE: JSON-RPC over HTTP
        ///     POST to HttpConfig::url; responses may be application/json or a
        ///     text/event-stream carrying the response (reading stops as soon as the
        ///     response for the request id has arrived).
        ///
        /// Typical use: connect(), initialize(), then the request methods; disconnect()
        /// (or destruction) ends the session: HTTP DELETE for HTTP transports; for STDIO
        /// stdin is closed, the process gets StdioConfig::shutdownTimeoutMs to exit, then
        /// it is terminated (SIGTERM, then SIGKILL on POSIX) and always reaped.
        ///
        /// initialize() offers the newest protocol version (or ClientConfig::protocolVersion)
        /// and rejects a server answer that is not in supportedProtocolVersions(). After a
        /// negotiated version of 2025-06-18 or newer, HTTP requests carry the
        /// MCP-Protocol-Version header.
        ///
        /// Request methods block the calling thread until the response arrives and
        /// throw std::runtime_error on transport failures, timeouts, cancellation and
        /// HTTP status codes other than 200, and JsonRpcError (not derived from
        /// std::exception) on a JSON-RPC error response. They may be called from several
        /// threads at once (each HTTP request uses its own connection; STDIO requests
        /// are multiplexed on the pipe).
        ///
        /// Timeouts and retries: ClientConfig::readTimeoutSeconds bounds the wait for a
        /// STDIO response (on timeout `notifications/cancelled` is sent); for HTTP the
        /// socket read timeout is HttpConfig::timeoutSeconds, or readTimeoutSeconds when
        /// that is <= 0. HTTP requests whose connection could not be established (DNS
        /// or connect failure, so nothing was sent) are retried up to
        /// ClientConfig::maxRetries times, ClientConfig::retryBackoffMs apart; JSON-RPC
        /// errors, HTTP errors and failures after the request was sent are never retried.
        ///
        /// Server-to-client notifications (HTTP) arrive on a GET SSE stream that is opened
        /// when a notification handler is registered or HttpConfig::enableResumability is
        /// set; it sends the configured headers plus Mcp-Session-Id and auto-reconnects
        /// with Last-Event-ID. Notifications embedded in an SSE POST response are
        /// dispatched too.
        class MCPClient
        {
        public:
            /// @brief Notification handler; receives the notification's params
            ///        (an empty object if absent).
            using NotificationCallback = std::function<void(const json&)>;

            /// @brief Connection status callback: (true, message) from connect(),
            ///        (false, error) from the notification stream thread on stream errors or
            ///        from the STDIO reader thread when the server process closes its stdout.
            using StatusCallback = std::function<void(bool connected, const std::string& message)>;

            /// @brief Receives each line the STDIO server writes to stderr (without the
            ///        line terminator).
            using StderrCallback = std::function<void(const std::string& line)>;

            /// @brief Cancellation flag for request(): set it to true (from any thread) to
            ///        abandon the request; the client then sends `notifications/cancelled`.
            using CancelToken = std::shared_ptr<std::atomic<bool>>;

            /// @brief Protocol versions this client accepts from a server, newest first:
            ///        "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05".
            /// @return The list.
            static const std::vector<std::string>& supportedProtocolVersions()
            {
                static const std::vector<std::string> versions = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
                return versions;
            }

            /// @brief Create a disconnected client.
            MCPClient() = default;

            /// @brief Non-copyable.
            MCPClient(const MCPClient&) = delete;
            /// @brief Non-copyable.
            MCPClient& operator=(const MCPClient&) = delete;

            /// @brief Calls disconnect().
            ~MCPClient()
            {
                disconnect();
            }

            /// @brief Configure the client for a server (disconnecting any previous session).
            ///
            /// For HTTP transports no network I/O happens here: the first request is sent
            /// by initialize(). For STDIO the server process is started (see StdioConfig).
            /// @param config Client configuration (copied)
            /// @return true on success; false for STDIO when the command is empty, the
            ///         envFile cannot be read or the process cannot be started.
            bool connect(const ClientConfig& config)
            {
                disconnect();
                m_config = config;
                {
                    std::lock_guard<std::mutex> lock(m_sessionMutex);
                    m_protocolVersion.clear();
                }
                m_serverCapabilities = json();

                switch (config.transport)
                {
                case TransportType::HTTP:
                case TransportType::HTTP_STREAMABLE:
                    return connectHttp();
                case TransportType::STDIO:
                    return connectStdio();
                }

                LOG_ERROR("%s", "MCPClient: Unknown transport type");
                return false;
            }

            /// @brief Disconnect from the MCP server. No-op if not connected.
            ///
            /// HTTP: closes the notification stream, sends HTTP DELETE with the configured
            /// headers and Mcp-Session-Id when the server issued a session id (best effort;
            /// errors ignored) and joins the stream thread.
            /// STDIO: closes the server's stdin, waits up to StdioConfig::shutdownTimeoutMs
            /// for it to exit, then terminates it (SIGTERM, another shutdownTimeoutMs, then
            /// SIGKILL on POSIX; TerminateProcess() on Windows), reaps it and joins the
            /// reader threads. Requests still waiting fail with std::runtime_error.
            /// @note Blocks until done. Must not be called from a notification, status or
            ///       stderr handler.
            void disconnect()
            {
                if (!m_connected.exchange(false))
                {
                    return;
                }

                if (m_config.transport == TransportType::STDIO)
                {
                    disconnectStdio();
                }
                else
                {
                    disconnectHttp();
                }

                std::lock_guard<std::mutex> lock(m_sessionMutex);
                m_sessionId.clear();
                m_protocolVersion.clear();
            }

            /// @brief Set the clientInfo sent by initialize() without arguments
            ///        (default {"name": "SocketsHpp-MCP-Client", "version": "1.0"}).
            /// @param clientInfo Implementation info object ("name", "version", ...).
            /// @warning Not synchronized: call before initialize().
            void setClientInfo(const json& clientInfo) { m_clientInfo = clientInfo; }

            /// @brief Set the client capabilities sent by initialize() (default {}), e.g.
            ///        {"roots": {"listChanged": true}}.
            /// @param capabilities ClientCapabilities object.
            /// @warning Not synchronized: call before initialize().
            void setClientCapabilities(const json& capabilities) { m_clientCapabilities = capabilities; }

            /// @brief Initialize the MCP session with the clientInfo from setClientInfo().
            /// @return As initialize(const json&).
            /// @throws As initialize(const json&).
            json initialize() { return initialize(m_clientInfo); }

            /// @brief Initialize the MCP session.
            ///
            /// Sends `initialize` with ClientConfig::protocolVersion (default: "2024-11-05"
            /// for TransportType::HTTP, else the newest supported version), the
            /// capabilities from setClientCapabilities() and @p clientInfo. Checks that the
            /// server's protocolVersion is in supportedProtocolVersions() and remembers it
            /// (protocolVersion()), stores the Mcp-Session-Id response header and the server
            /// capabilities, then sends the required `notifications/initialized`. On HTTP,
            /// opens the notification stream if a handler is registered or
            /// HttpConfig::enableResumability is set.
            /// @param clientInfo Client information, e.g. {"name": ..., "version": ...}
            /// @return The initialize result (protocolVersion, capabilities, serverInfo, ...).
            /// @throws std::runtime_error if not connected, on transport/HTTP errors, or if
            ///         the server chose an unsupported protocol version (the client is then
            ///         disconnected); JsonRpcError on a JSON-RPC error response.
            json initialize(const json& clientInfo)
            {
                std::string offered = m_config.protocolVersion;
                if (offered.empty())
                {
                    offered = m_config.transport == TransportType::HTTP ? "2024-11-05"
                                                                         : supportedProtocolVersions().front();
                }
                json params = {
                    {"protocolVersion", offered},
                    {"capabilities", m_clientCapabilities.is_object() ? m_clientCapabilities : json::object()},
                    {"clientInfo", clientInfo}
                };

                auto response = request("initialize", params);

                const std::string version = (response.contains("protocolVersion") &&
                                             response["protocolVersion"].is_string())
                                                ? response["protocolVersion"].get<std::string>()
                                                : std::string();
                const auto& supported = supportedProtocolVersions();
                if (std::find(supported.begin(), supported.end(), version) == supported.end())
                {
                    disconnect();
                    throw std::runtime_error("MCP server chose unsupported protocol version '" + version + "'");
                }
                {
                    std::lock_guard<std::mutex> lock(m_sessionMutex);
                    m_protocolVersion = version;
                }

                // Store server capabilities
                if (response.contains("capabilities"))
                {
                    m_serverCapabilities = response["capabilities"];
                }

                // Lifecycle: the client MUST send notifications/initialized after a
                // successful initialize response.
                notify("notifications/initialized", json());

                // The server→client notification stream needs the session id, which is
                // only known now. It is opened when resumability is enabled or when a
                // notification handler is registered (see onNotification()).
                if (m_config.transport != TransportType::STDIO &&
                    (m_config.http.enableResumability || hasNotificationHandlers()))
                {
                    ensureSSEStream();
                }

                return response;
            }

            /// @brief Protocol version negotiated by the last successful initialize()
            ///        ("" before it and after disconnect()). Thread-safe.
            /// @return The version, e.g. "2025-03-26".
            std::string protocolVersion() const
            {
                std::lock_guard<std::mutex> lock(m_sessionMutex);
                return m_protocolVersion;
            }

            /// @brief Send `ping` (health check).
            /// @return The result (normally an empty object).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json ping()
            {
                return request("ping", json::object());
            }

            /// @brief Send `tools/list` (first page only; pagination cursors are not followed).
            /// @return The result's "tools" array (empty array if absent).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json listTools()
            {
                auto response = request("tools/list", json::object());
                return response.value("tools", json::array());
            }

            /// @brief Send `tools/call`.
            /// @param name Tool name
            /// @param arguments Tool arguments (default: empty object)
            /// @param cancel Optional cancellation flag (see request()).
            /// @return The full tool result object (e.g. "content", "isError").
            /// @throws std::runtime_error or JsonRpcError, as for request().
            json callTool(const std::string& name, const json& arguments = json::object(),
                          const CancelToken& cancel = nullptr)
            {
                json params = {
                    {"name", name},
                    {"arguments", arguments}
                };

                return request("tools/call", params, cancel);
            }

            /// @brief Send `prompts/list` (first page only).
            /// @return The result's "prompts" array (empty array if absent).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json listPrompts()
            {
                auto response = request("prompts/list", json::object());
                return response.value("prompts", json::array());
            }

            /// @brief Send `prompts/get`.
            /// @param name Prompt name
            /// @param arguments Prompt arguments (omitted from the request when empty)
            /// @return The full result object (e.g. "description", "messages").
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json getPrompt(const std::string& name, const json& arguments = json::object())
            {
                json params = {
                    {"name", name}
                };

                if (!arguments.empty())
                {
                    params["arguments"] = arguments;
                }

                return request("prompts/get", params);
            }

            /// @brief Send `resources/list` (first page only).
            /// @return The result's "resources" array (empty array if absent).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json listResources()
            {
                auto response = request("resources/list", json::object());
                return response.value("resources", json::array());
            }

            /// @brief Send `resources/read`.
            /// @param uri Resource URI
            /// @return The full result object (e.g. "contents").
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json readResource(const std::string& uri)
            {
                json params = {
                    {"uri", uri}
                };

                return request("resources/read", params);
            }

            /// @brief Send `resources/subscribe`. Updates arrive as notifications
            ///        (register "notifications/resources/updated" with onNotification()).
            /// @param uri Resource URI
            /// @return The result (normally an empty object).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json subscribeResource(const std::string& uri)
            {
                json params = {
                    {"uri", uri}
                };

                return request("resources/subscribe", params);
            }

            /// @brief Send `resources/unsubscribe`.
            /// @param uri Resource URI
            /// @return The result (normally an empty object).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json unsubscribeResource(const std::string& uri)
            {
                json params = {
                    {"uri", uri}
                };

                return request("resources/unsubscribe", params);
            }

            /// @brief Send `resources/templates/list` (first page only).
            /// @return The result's "resourceTemplates" array (empty array if absent).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json listResourceTemplates()
            {
                auto response = request("resources/templates/list", json::object());
                return response.value("resourceTemplates", json::array());
            }

            /// @brief Send any JSON-RPC request and wait for its result.
            /// @param method Method name, e.g. "tools/call".
            /// @param params Parameters (default: empty object; omitted when null).
            /// @param cancel Optional flag: when it becomes true while waiting, the client
            ///        stops waiting (HTTP: aborts the connection), sends
            ///        `notifications/cancelled` for the request and throws.
            /// @return The response's "result" (empty object if absent).
            /// @throws std::runtime_error if not connected, on transport failures, HTTP
            ///         status codes other than 200, a STDIO timeout
            ///         (ClientConfig::readTimeoutSeconds), cancellation, or when the STDIO
            ///         server exits; JsonRpcError on a JSON-RPC error response.
            json request(const std::string& method, const json& params = json::object(),
                         const CancelToken& cancel = nullptr)
            {
                if (!m_connected)
                {
                    throw std::runtime_error("Not connected to MCP server");
                }
                return m_config.transport == TransportType::STDIO ? stdioRequest(method, params, cancel)
                                                                   : httpRequest(method, params, cancel);
            }

            /// @brief Send a JSON-RPC notification (best effort; failures are ignored).
            ///        No-op when not connected.
            /// @param method Notification method, e.g. "notifications/roots/list_changed".
            /// @param params Parameters (default: empty object; omitted when null).
            void notify(const std::string& method, const json& params = json::object())
            {
                if (!m_connected)
                {
                    return;
                }
                json msg = {{"jsonrpc", "2.0"}, {"method", method}};
                if (!params.is_null())
                {
                    msg["params"] = params;
                }
                if (m_config.transport == TransportType::STDIO)
                {
                    writeStdioMessage(msg);
                }
                else
                {
                    HttpClientRequest httpReq = makePost(msg.dump());
                    postWithRetries(httpReq, nullptr, nullptr);  // 202 Accepted expected; ignored
                }
            }

            /// @brief Register (or replace) the handler for a notification method.
            /// @param method Notification method (e.g., "notifications/message")
            /// @param handler Handler; runs on the notification stream thread (HTTP), the
            ///        STDIO reader thread, or on the calling thread for notifications
            ///        embedded in an SSE POST response. Exceptions it throws are logged
            ///        and ignored.
            /// @note Thread-safe. On HTTP, registering a handler opens the server→client
            ///       notification stream (immediately if already initialized, otherwise
            ///       after initialize()).
            void onNotification(const std::string& method, NotificationCallback handler)
            {
                {
                    std::lock_guard<std::mutex> lock(m_notificationMutex);
                    m_notificationHandlers[method] = handler;
                }
                if (m_connected.load() && m_config.transport != TransportType::STDIO && !sessionId().empty())
                {
                    ensureSSEStream();
                }
            }

            /// @brief Register the connection status callback.
            /// @param callback Status callback; may be invoked on the notification stream
            ///        thread or the STDIO reader thread.
            /// @warning Not synchronized: call before connect().
            void onStatus(StatusCallback callback)
            {
                m_statusCallback = callback;
            }

            /// @brief Capture the STDIO server's stderr: when set before connect(), stderr
            ///        is piped and each line is passed to @p callback on a dedicated
            ///        thread; otherwise the server inherits this process's stderr.
            /// @param callback Line handler (nullptr restores inheriting).
            /// @warning Not synchronized: call before connect().
            void onStderr(StderrCallback callback)
            {
                m_stderrCallback = callback;
            }

            /// @brief Whether connect() succeeded and disconnect() has not been called since;
            ///        for STDIO also false once the server process closed its stdout. Does
            ///        not probe the server. Thread-safe.
            /// @return The connection state.
            bool isConnected() const
            {
                return m_connected.load() && !(m_config.transport == TransportType::STDIO && m_stdioClosed.load());
            }

            /// @brief Session id issued by the server on initialize ("" if none). Thread-safe.
            /// @return The session id.
            std::string sessionId() const
            {
                std::lock_guard<std::mutex> lock(m_sessionMutex);
                return m_sessionId;
            }

            /// @brief Process id of the STDIO server process.
            /// @return The pid, or -1 when no process is running under this client.
            /// @warning Not synchronized with connect() / disconnect().
            std::int64_t serverProcessId() const
            {
                return m_process ? m_process->pid() : -1;
            }

            /// @brief Server capabilities from the last initialize() (null before it).
            /// @return The capabilities object.
            /// @warning Not synchronized with a concurrent initialize().
            const json& getServerCapabilities() const { return m_serverCapabilities; }

        private:
            ClientConfig m_config;
            std::atomic<bool> m_connected{false};
            std::atomic<std::int64_t> m_requestId{1};
            json m_serverCapabilities;
            json m_clientInfo = {{"name", "SocketsHpp-MCP-Client"}, {"version", "1.0"}};
            json m_clientCapabilities = json::object();

            std::string m_sessionId;
            std::string m_protocolVersion;
            mutable std::mutex m_sessionMutex;  // guards m_sessionId and m_protocolVersion

            // HTTP transport
            std::unique_ptr<SSEClient> m_sseClient;
            std::thread m_sseThread;
            std::mutex m_sseMutex;  // guards m_sseClient / m_sseThread start and stop

            // STDIO transport
            struct PendingRequest
            {
                bool done = false;
                json message;  // the response; null when the transport closed
            };
            std::unique_ptr<utils::ChildProcess> m_process;
            std::thread m_stdoutThread;
            std::thread m_stderrThread;
            std::mutex m_writeMutex;  // serializes writes to the server's stdin
            std::mutex m_pendingMutex;
            std::condition_variable m_pendingCv;
            std::map<std::string, std::shared_ptr<PendingRequest>> m_pending;  // id.dump() -> request
            std::atomic<bool> m_stdioClosed{false};
            StderrCallback m_stderrCallback;

            // Notification handling
            std::map<std::string, NotificationCallback> m_notificationHandlers;
            std::mutex m_notificationMutex;
            StatusCallback m_statusCallback;

            /// Longest accepted line (STDIO) or response body (HTTP).
            static constexpr size_t kMaxMessageBytes = static_cast<size_t>(256) << 20;

            static int secondsToMs(int seconds)
            {
                return seconds >= INT_MAX / 1000 ? INT_MAX : seconds * 1000;
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

            /// @brief Case-insensitive response header lookup
            static std::string responseHeader(const HttpClientResponse& resp, const std::string& name)
            {
                for (const auto& kv : resp.headers)
                {
                    if (iequals(kv.first, name))
                        return kv.second;
                }
                return "";
            }

            /// @brief Turn a JSON-RPC response message into its result.
            /// @throws JsonRpcError for an error response.
            static json unwrapResponse(const json& message)
            {
                auto response = JsonRpcResponse::parse(message.dump());
                if (response.error.has_value())
                {
                    throw response.error.value();
                }
                return response.result.value_or(json::object());
            }

            bool hasNotificationHandlers()
            {
                std::lock_guard<std::mutex> lock(m_notificationMutex);
                return !m_notificationHandlers.empty();
            }

            /// @brief Invoke the registered handler for a notification message
            void dispatchNotification(const json& msg)
            {
                const std::string method = msg.value("method", std::string());
                NotificationCallback handler;
                {
                    std::lock_guard<std::mutex> lock(m_notificationMutex);
                    auto it = m_notificationHandlers.find(method);
                    if (it != m_notificationHandlers.end())
                        handler = it->second;
                }
                if (!handler)
                {
                    LOG_WARN("MCPClient: Unhandled notification: %s", method.c_str());
                    return;
                }
                try
                {
                    handler(msg.contains("params") ? msg["params"] : json::object());
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("MCPClient: notification handler for %s threw: %s", method.c_str(), e.what());
                    (void)e;
                }
                catch (...)
                {
                    LOG_ERROR("MCPClient: notification handler for %s threw", method.c_str());
                }
            }

            // ── HTTP transport ─────────────────────────────────────────────────

            /// @brief Connect via HTTP transport (no I/O; see connect()).
            bool connectHttp()
            {
                // The connection itself is established lazily by initialize().
                m_connected = true;

                if (m_statusCallback)
                {
                    m_statusCallback(true, "Connected to " + m_config.http.url);
                }

                return true;
            }

            void disconnectHttp()
            {
                {
                    std::lock_guard<std::mutex> lock(m_sseMutex);
                    if (m_sseClient)
                    {
                        m_sseClient->close();  // stop auto-reconnect, unblock the reader
                    }
                }

                const std::string sid = sessionId();
                if (!sid.empty())
                {
                    try
                    {
                        HttpClientRequest httpReq = makeHttpRequest(METHOD_DELETE);
                        HttpClientResponse httpResp;
                        newHttpClient()->send(httpReq, httpResp);  // best effort; 405 is allowed
                    }
                    catch (...)
                    {
                        // Ignore errors during shutdown
                    }
                }

                std::lock_guard<std::mutex> lock(m_sseMutex);
                if (m_sseThread.joinable())
                {
                    m_sseThread.join();
                }
                m_sseClient.reset();
            }

            /// @brief A fresh HttpClient with the configured timeouts (one per request, so
            ///        requests from several threads never share a connection).
            std::unique_ptr<HttpClient> newHttpClient() const
            {
                auto client = std::make_unique<HttpClient>();
                client->setUserAgent("SocketsHpp-MCP-Client/1.0");

                // HttpClient timeouts are in milliseconds; the config is in seconds
                const int readSeconds = m_config.http.timeoutSeconds > 0 ? m_config.http.timeoutSeconds
                                                                          : m_config.readTimeoutSeconds;
                if (readSeconds > 0)
                {
                    client->setReadTimeout(secondsToMs(readSeconds));
                }
                if (m_config.connectTimeoutSeconds > 0)
                {
                    client->setConnectTimeout(secondsToMs(m_config.connectTimeoutSeconds));
                }
                return client;
            }

            /// @brief Open the notification stream once (after initialize).
            void ensureSSEStream()
            {
                std::lock_guard<std::mutex> lock(m_sseMutex);
                if (!m_sseThread.joinable())
                {
                    setupSSEStream();
                }
            }

            /// @brief Setup SSE stream for server notifications. Caller holds m_sseMutex.
            void setupSSEStream()
            {
                m_sseClient = std::make_unique<SSEClient>();
                m_sseClient->setAutoReconnect(true, 3000);

                // The stream carries the same configured headers as regular requests
                // (e.g. Authorization) plus the session id in the Mcp-Session-Id header;
                // keeping it out of the URL keeps it out of access logs.
                for (const auto& header : m_config.http.headers)
                {
                    m_sseClient->setRequestHeader(header.first, header.second);
                }
                const std::string sid = sessionId();
                if (!sid.empty())
                {
                    m_sseClient->setRequestHeader("Mcp-Session-Id", sid);
                }
                const std::string version = protocolVersionHeader();
                if (!version.empty())
                {
                    m_sseClient->setRequestHeader("MCP-Protocol-Version", version);
                }
                const std::string sseUrl = m_config.http.url;

                SSEClient* sse = m_sseClient.get();
                m_sseThread = std::thread([this, sse, sseUrl]() {
                    sse->connect(
                        sseUrl,
                        [this](const SSEEvent& event) {
                            handleSSEEvent(event);
                        },
                        [this](const std::string& error) {
                            LOG_ERROR("MCPClient SSE error: %s", error.c_str());
                            if (m_statusCallback)
                            {
                                m_statusCallback(false, error);
                            }
                        }
                    );
                });
            }

            /// @brief Handle SSE event from server
            void handleSSEEvent(const SSEEvent& event)
            {
                json data = json::parse(event.data, nullptr, false);
                if (data.is_object() && !data.contains("id") && data.contains("method"))
                {
                    dispatchNotification(data);
                }
                else
                {
                    // Responses are delivered on the POST that carried the request
                    LOG_WARN("%s", "MCPClient: Received unexpected JSON-RPC message via SSE");
                }
            }

            /// @brief Negotiated version when it needs the MCP-Protocol-Version header
            ///        (2025-06-18 and newer), else "".
            std::string protocolVersionHeader() const
            {
                const std::string version = protocolVersion();
                return version >= "2025-06-18" ? version : std::string();
            }

            /// @brief Build an HTTP request to the MCP endpoint with configured headers,
            /// the session id (if any) and MCP-Protocol-Version (if negotiated >= 2025-06-18).
            HttpClientRequest makeHttpRequest(const char* method) const
            {
                HttpClientRequest httpReq;
                httpReq.method = method;
                httpReq.uri = m_config.http.url;

                for (const auto& header : m_config.http.headers)
                {
                    httpReq.setHeader(header.first, header.second);
                }

                const std::string sid = sessionId();
                if (!sid.empty())
                {
                    httpReq.setHeader("Mcp-Session-Id", sid);
                }
                const std::string version = protocolVersionHeader();
                if (!version.empty())
                {
                    httpReq.setHeader("MCP-Protocol-Version", version);
                }
                return httpReq;
            }

            HttpClientRequest makePost(const std::string& body) const
            {
                HttpClientRequest httpReq = makeHttpRequest(METHOD_POST);
                httpReq.setContentType("application/json");
                // Streamable HTTP: the client MUST accept both JSON and SSE responses
                httpReq.setHeader("Accept", "application/json, text/event-stream");
                httpReq.body = body;
                return httpReq;
            }

            /// @brief Outcome of one POST (possibly after retries).
            struct PostResult
            {
                bool ok = false;           ///< HttpClient::send() returned true
                bool requestSent = false;  ///< request bytes reached the network
                bool cancelled = false;    ///< the CancelToken fired
                bool sse = false;          ///< the body was text/event-stream
                bool tooLarge = false;     ///< body exceeded kMaxMessageBytes
                HttpClientResponse response;
                std::string body;          ///< non-SSE body
                json found;                ///< the JSON-RPC response (SSE body only)
            };

            /// @brief Send one POST. An SSE body is parsed as it arrives: notifications are
            /// dispatched, and once the response for @p id is seen the connection is
            /// closed instead of waiting for the server to end the stream.
            PostResult postOnce(HttpClientRequest& req, const json* id, const CancelToken& cancel)
            {
                PostResult r;
                auto client = newHttpClient();
                HttpClient* cp = client.get();
                SSEParser parser;
                bool decided = false;

                auto matches = [id](const json& m) {
                    return id != nullptr && m.is_object() && m.contains("id") && m["id"] == *id &&
                           (m.contains("result") || m.contains("error"));
                };
                auto handleMessage = [&](const json& m) {
                    if (m.is_object() && !m.contains("id") && m.contains("method"))
                        dispatchNotification(m);
                    else if (r.found.is_null() && matches(m))
                        r.found = m;
                };

                r.response.chunkCallback = [&](const std::string& data) {
                    if (!decided)
                    {
                        decided = true;
                        r.sse = r.response.code == 200 &&
                                r.response.getHeader("Content-Type").find("text/event-stream") != std::string::npos;
                    }
                    if (!r.sse)
                    {
                        if (r.body.size() + data.size() > kMaxMessageBytes)
                        {
                            r.tooLarge = true;
                            cp->cancel();
                            return;
                        }
                        r.body += data;
                        return;
                    }
                    for (const auto& ev : parser.parseChunk(data))
                    {
                        if (!ev.isValid())
                            continue;
                        json m = json::parse(ev.data, nullptr, false);
                        if (m.is_array())
                        {
                            for (const auto& item : m)
                                handleMessage(item);
                        }
                        else if (!m.is_discarded())
                        {
                            handleMessage(m);
                        }
                    }
                    if (!r.found.is_null())
                        cp->cancel();  // got our response: stop reading the stream
                };

                std::atomic<bool> finished{false};
                std::atomic<bool> cancelled{false};
                std::thread watcher;
                if (cancel)
                {
                    watcher = std::thread([&]() {
                        while (!finished.load())
                        {
                            if (cancel->load())
                            {
                                cancelled = true;
                                cp->cancel();  // repeated: a connect() in progress is not interrupted
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                    });
                }
                r.ok = client->send(req, r.response);
                finished = true;
                if (watcher.joinable())
                    watcher.join();
                r.requestSent = client->lastRequestSent();
                r.cancelled = cancelled.load();
                r.response.chunkCallback = nullptr;
                return r;
            }

            /// @brief postOnce(), retried (ClientConfig::maxRetries, retryBackoffMs) while
            /// the connection could not be established and nothing was sent.
            PostResult postWithRetries(HttpClientRequest& req, const json* id, const CancelToken& cancel)
            {
                for (int attempt = 0;; ++attempt)
                {
                    PostResult r = postOnce(req, id, cancel);
                    if (r.ok || r.requestSent || r.cancelled || !r.found.is_null() ||
                        attempt >= m_config.maxRetries || !m_connected.load())
                    {
                        return r;
                    }
                    LOG_WARN("MCPClient: cannot connect to %s, retrying (%d/%d)", m_config.http.url.c_str(),
                             attempt + 1, m_config.maxRetries);
                    const auto until = std::chrono::steady_clock::now() +
                                       std::chrono::milliseconds(std::max(0, m_config.retryBackoffMs));
                    while (std::chrono::steady_clock::now() < until && m_connected.load() &&
                           !(cancel && cancel->load()))
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }
            }

            /// @brief Send a JSON-RPC request over HTTP and wait for its result.
            json httpRequest(const std::string& method, const json& params, const CancelToken& cancel)
            {
                const std::int64_t requestId = m_requestId++;
                const json id = requestId;
                json msg = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
                if (!params.is_null())
                {
                    msg["params"] = params;
                }

                HttpClientRequest httpReq = makePost(msg.dump());
                PostResult r = postWithRetries(httpReq, &id, cancel);

                if (r.cancelled && r.found.is_null())
                {
                    notify("notifications/cancelled", {{"requestId", id}, {"reason", "Request cancelled by the client"}});
                    throw std::runtime_error("Request cancelled: " + method);
                }
                if (r.tooLarge)
                {
                    throw std::runtime_error("HTTP response too large");
                }
                if (r.found.is_null())
                {
                    if (!r.ok)
                    {
                        throw std::runtime_error("HTTP request failed");
                    }
                    if (r.response.code != 200)
                    {
                        throw std::runtime_error("HTTP error: " + std::to_string(r.response.code) + " " +
                                                 r.response.message);
                    }
                }

                // The session id is assigned by the server in the initialize response
                if (method == "initialize")
                {
                    std::string sessionHeader = responseHeader(r.response, "Mcp-Session-Id");
                    std::lock_guard<std::mutex> lock(m_sessionMutex);
                    m_sessionId = sessionHeader;
                }

                if (!r.found.is_null())
                {
                    return unwrapResponse(r.found);
                }
                if (r.sse)
                {
                    throw std::runtime_error("No JSON-RPC response in SSE stream");
                }

                json body = json::parse(r.body);
                if (body.is_array())
                {
                    for (const auto& m : body)
                    {
                        if (m.is_object() && m.contains("id") && m["id"] == id &&
                            (m.contains("result") || m.contains("error")))
                            return unwrapResponse(m);
                    }
                    throw std::runtime_error("No JSON-RPC response for request in batch reply");
                }
                return unwrapResponse(body);
            }

            // ── STDIO transport ────────────────────────────────────────────────

            /// @brief Launch the server process and start the reader thread(s).
            bool connectStdio()
            {
                const StdioConfig& cfg = m_config.stdio;
                auto fail = [this](const std::string& why) {
                    LOG_ERROR("MCPClient: %s", why.c_str());
                    if (m_statusCallback)
                    {
                        m_statusCallback(false, why);
                    }
                    return false;
                };
                if (cfg.command.empty())
                {
                    return fail("STDIO transport needs StdioConfig::command");
                }

                utils::ProcessOptions options;
                options.command = cfg.command;
                options.args = cfg.args;
                options.cwd = cfg.cwd;
                try
                {
                    options.env = cfg.resolvedEnvironment();
                }
                catch (const std::exception& e)
                {
                    return fail(e.what());
                }
                options.stderrMode = m_stderrCallback ? utils::StderrMode::Pipe : utils::StderrMode::Inherit;

                auto process = std::make_unique<utils::ChildProcess>();
                std::string error;
                if (!process->start(options, &error))
                {
                    return fail("cannot start MCP server: " + error);
                }

                m_process = std::move(process);
                m_stdioClosed = false;
                m_connected = true;
                m_stdoutThread = std::thread([this]() { stdoutLoop(); });
                if (m_stderrCallback)
                {
                    m_stderrThread = std::thread([this]() { stderrLoop(); });
                }

                if (m_statusCallback)
                {
                    m_statusCallback(true, "Started " + cfg.command + " (pid " + std::to_string(m_process->pid()) + ")");
                }
                return true;
            }

            void disconnectStdio()
            {
                utils::ChildProcess* process = m_process.get();
                if (process == nullptr)
                {
                    return;
                }

                // MCP stdio shutdown: close stdin, wait, SIGTERM, wait, SIGKILL.
                const int grace = std::max(0, m_config.stdio.shutdownTimeoutMs);
                process->closeStdin();
                if (!process->waitForExit(grace))
                {
                    process->terminate();
                    if (!process->waitForExit(grace))
                    {
                        process->kill();
                        process->waitForExit(-1);
                    }
                }
                // A grandchild may still hold the pipes open; do not wait for its EOF.
                process->interruptReads();

                for (std::thread* t : {&m_stdoutThread, &m_stderrThread})
                {
                    if (!t->joinable())
                        continue;
                    if (t->get_id() == std::this_thread::get_id())
                        t->detach();  // called from a handler on that thread (unsupported)
                    else
                        t->join();
                }

                failPendingRequests();
                std::lock_guard<std::mutex> lock(m_writeMutex);
                m_process.reset();
            }

            /// @brief Wake every waiting STDIO request with "transport closed".
            void failPendingRequests()
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                for (auto& kv : m_pending)
                {
                    kv.second->done = true;
                    kv.second->message = json();
                }
                m_pendingCv.notify_all();
            }

            /// @brief Write one message as a line to the server's stdin.
            bool writeStdioMessage(const json& msg)
            {
                const std::string line = msg.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
                std::lock_guard<std::mutex> lock(m_writeMutex);
                return m_process && m_process->writeStdin(line.data(), line.size());
            }

            /// @brief Reader thread: split stdout into lines and handle each message.
            void stdoutLoop()
            {
                std::string buffer;
                std::vector<char> chunk(64 * 1024);
                bool discarding = false;
                while (m_connected.load())
                {
                    const long n = m_process->readStdout(chunk.data(), chunk.size());
                    if (n <= 0)
                        break;
                    buffer.append(chunk.data(), static_cast<size_t>(n));
                    size_t start = 0;
                    for (;;)
                    {
                        const size_t nl = buffer.find('\n', start);
                        if (nl == std::string::npos)
                            break;
                        if (!discarding)
                            handleStdioLine(buffer.substr(start, nl - start));
                        discarding = false;
                        start = nl + 1;
                        if (!m_connected.load())
                            return;  // disconnect() was called by a handler
                    }
                    buffer.erase(0, start);
                    if (buffer.size() > kMaxMessageBytes)
                    {
                        LOG_ERROR("%s", "MCPClient: discarding an over-long line from the MCP server");
                        buffer.clear();
                        discarding = true;
                    }
                }

                m_stdioClosed = true;
                failPendingRequests();
                if (m_connected.load() && m_statusCallback)
                {
                    m_statusCallback(false, "MCP server process closed its output");
                }
            }

            /// @brief Stderr thread: pass each line to the stderr callback.
            void stderrLoop()
            {
                std::string buffer;
                std::vector<char> chunk(4096);
                auto emit = [this](const std::string& line) {
                    try
                    {
                        m_stderrCallback(line);
                    }
                    catch (...)
                    {
                        LOG_ERROR("%s", "MCPClient: stderr handler threw");
                    }
                };
                for (;;)
                {
                    const long n = m_process->readStderr(chunk.data(), chunk.size());
                    if (n <= 0)
                        break;
                    buffer.append(chunk.data(), static_cast<size_t>(n));
                    size_t nl;
                    while ((nl = buffer.find('\n')) != std::string::npos)
                    {
                        std::string line = buffer.substr(0, nl);
                        buffer.erase(0, nl + 1);
                        if (!line.empty() && line.back() == '\r')
                            line.pop_back();
                        emit(line);
                    }
                    if (buffer.size() > kMaxMessageBytes)
                    {
                        emit(buffer);
                        buffer.clear();
                    }
                }
                if (!buffer.empty())
                    emit(buffer);
            }

            void handleStdioLine(std::string line)
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.find_first_not_of(" \t") == std::string::npos)
                    return;
                json msg = json::parse(line, nullptr, false);
                if (msg.is_discarded())
                {
                    LOG_WARN("%s", "MCPClient: ignoring a non-JSON line from the MCP server");
                    return;
                }
                if (msg.is_array())
                {
                    for (const auto& item : msg)
                        handleStdioMessage(item);
                }
                else
                {
                    handleStdioMessage(msg);
                }
            }

            void handleStdioMessage(const json& msg)
            {
                if (!msg.is_object())
                    return;
                if (msg.contains("method"))
                {
                    if (msg.contains("id"))
                        answerServerRequest(msg);
                    else
                        dispatchNotification(msg);
                    return;
                }
                if (msg.contains("id") && (msg.contains("result") || msg.contains("error")))
                {
                    std::lock_guard<std::mutex> lock(m_pendingMutex);
                    auto it = m_pending.find(msg["id"].dump());
                    if (it == m_pending.end())
                    {
                        LOG_WARN("%s", "MCPClient: response for an unknown (or abandoned) request id");
                        return;
                    }
                    it->second->done = true;
                    it->second->message = msg;
                    m_pendingCv.notify_all();
                }
            }

            /// @brief Answer a server-to-client request: `ping` succeeds, anything else is
            ///        -32601 (this client offers no sampling / roots / elicitation).
            void answerServerRequest(const json& msg)
            {
                const std::string method = msg["method"].is_string() ? msg["method"].get<std::string>() : std::string();
                json reply = {{"jsonrpc", "2.0"}, {"id", msg["id"]}};
                if (method == "ping")
                    reply["result"] = json::object();
                else
                    reply["error"] = JsonRpcError::methodNotFound(method).toJson();
                writeStdioMessage(reply);
            }

            /// @brief Send a JSON-RPC request over STDIO and wait for its result.
            json stdioRequest(const std::string& method, const json& params, const CancelToken& cancel)
            {
                if (m_stdioClosed.load())
                {
                    throw std::runtime_error("MCP server process has exited");
                }
                const json id = m_requestId++;
                const std::string key = id.dump();
                auto pending = std::make_shared<PendingRequest>();
                {
                    std::lock_guard<std::mutex> lock(m_pendingMutex);
                    m_pending[key] = pending;
                }
                auto forget = [this, &key]() {
                    std::lock_guard<std::mutex> lock(m_pendingMutex);
                    m_pending.erase(key);
                };

                json msg = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
                if (!params.is_null())
                {
                    msg["params"] = params;
                }
                if (!writeStdioMessage(msg))
                {
                    forget();
                    throw std::runtime_error("Failed to write to the MCP server's stdin");
                }

                const bool hasDeadline = m_config.readTimeoutSeconds > 0;
                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(hasDeadline ? m_config.readTimeoutSeconds : 0);
                std::unique_lock<std::mutex> lock(m_pendingMutex);
                for (;;)
                {
                    if (pending->done)
                        break;
                    const char* abandon = nullptr;
                    if (cancel && cancel->load())
                        abandon = "Request cancelled by the client";
                    else if (hasDeadline && std::chrono::steady_clock::now() >= deadline)
                        abandon = "Request timed out";
                    if (abandon != nullptr)
                    {
                        m_pending.erase(key);
                        lock.unlock();
                        notify("notifications/cancelled", {{"requestId", id}, {"reason", abandon}});
                        throw std::runtime_error(std::string(abandon) + ": " + method);
                    }
                    auto wake = hasDeadline ? deadline : std::chrono::steady_clock::now() + std::chrono::hours(1);
                    if (cancel)
                        wake = std::min(wake, std::chrono::steady_clock::now() + std::chrono::milliseconds(10));
                    m_pendingCv.wait_until(lock, wake);
                }
                m_pending.erase(key);
                const json message = pending->message;
                lock.unlock();

                if (message.is_null())
                {
                    throw std::runtime_error("MCP server process exited before answering " + method);
                }
                return unwrapResponse(message);
            }
        };

    } // namespace client
} // namespace mcp
SOCKETSHPP_NS_END
