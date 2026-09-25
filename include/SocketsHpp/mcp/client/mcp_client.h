// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file mcp_client.h
/// @brief MCP (Model Context Protocol) client over HTTP: mcp::client::MCPClient.

#include <SocketsHpp/config.h>
#include <SocketsHpp/http/client/http_client.h>
#include <SocketsHpp/http/client/sse_client.h>
#include <SocketsHpp/http/common/json_rpc.h>
#include <SocketsHpp/mcp/common/mcp_config.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
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
        /// Supported transports: TransportType::HTTP and TransportType::HTTP_STREAMABLE
        /// (both send JSON-RPC over HTTP POST to HttpConfig::url; responses may be
        /// application/json or a text/event-stream carrying the response). STDIO is not
        /// supported (connect() returns false).
        ///
        /// Typical use: connect(), initialize(), then the request methods; disconnect()
        /// (or destruction) ends the session with HTTP DELETE.
        ///
        /// Request methods block the calling thread until the HTTP response arrives and
        /// throw std::runtime_error on transport/HTTP failures (any status other than
        /// 200) and JsonRpcError (not derived from std::exception) on a JSON-RPC error.
        ///
        /// Server-to-client notifications arrive on a GET SSE stream that is opened when
        /// a notification handler is registered or HttpConfig::enableResumability is set;
        /// it sends the configured headers plus Mcp-Session-Id and auto-reconnects with
        /// Last-Event-ID. Notifications embedded in an SSE POST response are dispatched too.
        class MCPClient
        {
        public:
            /// @brief Notification handler; receives the notification's params
            ///        (an empty object if absent).
            using NotificationCallback = std::function<void(const json&)>;

            /// @brief Connection status callback: (true, "Connected to <url>") from connect(),
            ///        (false, error) from the notification stream thread on stream errors.
            using StatusCallback = std::function<void(bool connected, const std::string& message)>;

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
            /// For HTTP transports no network I/O happens here: the HTTP client is set up
            /// (HttpConfig::timeoutSeconds, ClientConfig::connectTimeoutSeconds) and the
            /// first request is sent by initialize().
            /// @param config Client configuration (copied)
            /// @return true for HTTP / HTTP_STREAMABLE; false for STDIO (unsupported).
            bool connect(const ClientConfig& config)
            {
                disconnect();
                m_config = config;

                switch (config.transport)
                {
                case TransportType::HTTP:
                case TransportType::HTTP_STREAMABLE:
                    return connectHttp();
                case TransportType::STDIO:
                    LOG_ERROR("%s", "MCPClient: STDIO transport not yet supported");
                    return false;
                }

                LOG_ERROR("%s", "MCPClient: Unknown transport type");
                return false;
            }

            /// @brief Disconnect from the MCP server. No-op if not connected.
            /// Closes the notification stream, sends HTTP DELETE with the configured headers
            /// and Mcp-Session-Id when the server issued a session id (best effort; errors
            /// ignored), joins the stream thread and clears the session id.
            /// @note Blocks until the DELETE completes and the stream thread exits. Must
            ///       not be called from a notification handler running on the stream thread.
            void disconnect()
            {
                if (!m_connected.exchange(false))
                {
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(m_sseMutex);
                    if (m_sseClient)
                    {
                        m_sseClient->close();  // stop auto-reconnect, unblock the reader
                    }
                }

                const std::string sid = sessionId();
                if (!sid.empty() && m_httpClient)
                {
                    try
                    {
                        HttpClientRequest httpReq = makeHttpRequest(METHOD_DELETE);
                        HttpClientResponse httpResp;
                        m_httpClient->send(httpReq, httpResp);  // best effort; 405 is allowed
                    }
                    catch (...)
                    {
                        // Ignore errors during shutdown
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m_sseMutex);
                    if (m_sseThread.joinable())
                    {
                        m_sseThread.join();
                    }
                    m_sseClient.reset();
                }

                std::lock_guard<std::mutex> lock(m_sessionMutex);
                m_sessionId.clear();
            }

            /// @brief Initialize the MCP session.
            ///
            /// Sends `initialize` (protocolVersion "2025-03-26" for HTTP_STREAMABLE,
            /// "2024-11-05" for HTTP, empty client capabilities), stores the Mcp-Session-Id
            /// response header and the server capabilities, then sends the required
            /// `notifications/initialized`. Opens the notification stream if a handler is
            /// registered or HttpConfig::enableResumability is set.
            /// @param clientInfo Client information, e.g. {"name": ..., "version": ...}
            /// @return The initialize result (protocolVersion, capabilities, serverInfo, ...).
            /// @throws std::runtime_error if not connected or on transport/HTTP errors;
            ///         JsonRpcError on a JSON-RPC error response.
            json initialize(const json& clientInfo)
            {
                json params = {
                    {"protocolVersion", m_config.transport == TransportType::HTTP_STREAMABLE
                                            ? "2025-03-26" : "2024-11-05"},
                    {"capabilities", json::object()},
                    {"clientInfo", clientInfo}
                };

                auto response = sendRequest("initialize", params);

                // Store server capabilities
                if (response.contains("capabilities"))
                {
                    m_serverCapabilities = response["capabilities"];
                }

                // Lifecycle: the client MUST send notifications/initialized after a
                // successful initialize response.
                JsonRpcNotification initialized;
                initialized.method = "notifications/initialized";
                sendNotification(initialized);

                // The server→client notification stream needs the session id, which is
                // only known now. It is opened when resumability is enabled or when a
                // notification handler is registered (see onNotification()).
                if (m_config.http.enableResumability || hasNotificationHandlers())
                {
                    ensureSSEStream();
                }

                return response;
            }

            /// @brief Send `ping` (health check).
            /// @return The result (normally an empty object).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json ping()
            {
                return sendRequest("ping", json::object());
            }

            /// @brief Send `tools/list` (first page only; pagination cursors are not followed).
            /// @return The result's "tools" array (empty array if absent).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json listTools()
            {
                auto response = sendRequest("tools/list", json::object());
                return response.value("tools", json::array());
            }

            /// @brief Send `tools/call`.
            /// @param name Tool name
            /// @param arguments Tool arguments (default: empty object)
            /// @return The full tool result object (e.g. "content", "isError").
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json callTool(const std::string& name, const json& arguments = json::object())
            {
                json params = {
                    {"name", name},
                    {"arguments", arguments}
                };

                return sendRequest("tools/call", params);
            }

            /// @brief Send `prompts/list` (first page only).
            /// @return The result's "prompts" array (empty array if absent).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json listPrompts()
            {
                auto response = sendRequest("prompts/list", json::object());
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

                return sendRequest("prompts/get", params);
            }

            /// @brief Send `resources/list` (first page only).
            /// @return The result's "resources" array (empty array if absent).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json listResources()
            {
                auto response = sendRequest("resources/list", json::object());
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

                return sendRequest("resources/read", params);
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

                return sendRequest("resources/subscribe", params);
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

                return sendRequest("resources/unsubscribe", params);
            }

            /// @brief Send `resources/templates/list` (first page only).
            /// @return The result's "resourceTemplates" array (empty array if absent).
            /// @throws std::runtime_error or JsonRpcError, as for initialize().
            json listResourceTemplates()
            {
                auto response = sendRequest("resources/templates/list", json::object());
                return response.value("resourceTemplates", json::array());
            }

            /// @brief Register (or replace) the handler for a notification method.
            /// @param method Notification method (e.g., "notifications/message")
            /// @param handler Handler; runs on the notification stream thread, or on the
            ///        calling thread for notifications embedded in an SSE POST response.
            /// @note Thread-safe. Registering a handler opens the server→client notification
            ///       stream (immediately if already initialized, otherwise after initialize()).
            void onNotification(const std::string& method, NotificationCallback handler)
            {
                {
                    std::lock_guard<std::mutex> lock(m_notificationMutex);
                    m_notificationHandlers[method] = handler;
                }
                if (m_connected.load() && !sessionId().empty())
                {
                    ensureSSEStream();
                }
            }

            /// @brief Register the connection status callback.
            /// @param callback Status callback; may be invoked on the notification stream thread.
            /// @warning Not synchronized: call before connect().
            void onStatus(StatusCallback callback)
            {
                m_statusCallback = callback;
            }

            /// @brief Whether connect() succeeded and disconnect() has not been called since
            ///        (does not probe the server). Thread-safe.
            bool isConnected() const { return m_connected.load(); }

            /// @brief Session id issued by the server on initialize ("" if none). Thread-safe.
            std::string sessionId() const
            {
                std::lock_guard<std::mutex> lock(m_sessionMutex);
                return m_sessionId;
            }

            /// @brief Server capabilities from the last initialize() (null before it).
            /// @warning Not synchronized with a concurrent initialize().
            const json& getServerCapabilities() const { return m_serverCapabilities; }

        private:
            ClientConfig m_config;
            std::atomic<bool> m_connected{false};
            std::atomic<std::int64_t> m_requestId{1};
            json m_serverCapabilities;

            std::string m_sessionId;
            mutable std::mutex m_sessionMutex;

            // HTTP transport
            std::unique_ptr<HttpClient> m_httpClient;
            std::unique_ptr<SSEClient> m_sseClient;
            std::thread m_sseThread;
            std::mutex m_sseMutex;  // guards m_sseClient / m_sseThread start and stop

            // Notification handling
            std::map<std::string, NotificationCallback> m_notificationHandlers;
            std::mutex m_notificationMutex;
            StatusCallback m_statusCallback;

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

            /// @brief Connect via HTTP transport
            bool connectHttp()
            {
                m_httpClient = std::make_unique<HttpClient>();
                m_httpClient->setUserAgent("SocketsHpp-MCP-Client/1.0");

                // HttpClient timeouts are in milliseconds; the config is in seconds
                if (m_config.http.timeoutSeconds > 0)
                {
                    m_httpClient->setReadTimeout(secondsToMs(m_config.http.timeoutSeconds));
                }
                if (m_config.connectTimeoutSeconds > 0)
                {
                    m_httpClient->setConnectTimeout(secondsToMs(m_config.connectTimeoutSeconds));
                }

                // The connection itself is established lazily by initialize().
                m_connected = true;

                if (m_statusCallback)
                {
                    m_statusCallback(true, "Connected to " + m_config.http.url);
                }

                return true;
            }

            bool hasNotificationHandlers()
            {
                std::lock_guard<std::mutex> lock(m_notificationMutex);
                return !m_notificationHandlers.empty();
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
                try
                {
                    json data = json::parse(event.data);
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
                catch (const std::exception& e)
                {
                    LOG_ERROR("MCPClient: Error handling SSE event: %s", e.what());
                    (void)e;
                }
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
                if (handler)
                {
                    handler(msg.contains("params") ? msg["params"] : json::object());
                }
                else
                {
                    LOG_WARN("MCPClient: Unhandled notification: %s", method.c_str());
                }
            }

            /// @brief Build an HTTP request to the MCP endpoint with configured headers
            /// and the session id (if any).
            HttpClientRequest makeHttpRequest(const char* method)
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
                return httpReq;
            }

            HttpClientRequest makePost(const std::string& body)
            {
                HttpClientRequest httpReq = makeHttpRequest(METHOD_POST);
                httpReq.setContentType("application/json");
                // Streamable HTTP: the client MUST accept both JSON and SSE responses
                httpReq.setHeader("Accept", "application/json, text/event-stream");
                httpReq.body = body;
                return httpReq;
            }

            /// @brief Extract the JSON-RPC response for `id` from an HTTP response body
            /// (application/json, or text/event-stream carrying JSON-RPC messages).
            /// Notifications found in an SSE body are dispatched to handlers.
            json extractResponse(const HttpClientResponse& httpResp, const json& id)
            {
                auto matches = [&id](const json& m) {
                    return m.is_object() && m.contains("id") && m["id"] == id &&
                           (m.contains("result") || m.contains("error"));
                };

                const std::string contentType = responseHeader(httpResp, "Content-Type");
                if (contentType.find("text/event-stream") == std::string::npos)
                {
                    json body = json::parse(httpResp.body);
                    if (body.is_array())
                    {
                        for (const auto& m : body)
                        {
                            if (matches(m))
                                return m;
                        }
                        throw std::runtime_error("No JSON-RPC response for request in batch reply");
                    }
                    return body;
                }

                // Parse SSE: events are separated by blank lines; data lines are joined by '\n'
                json found;
                std::string data;
                bool haveData = false;
                auto flush = [&]() {
                    if (!haveData)
                        return;
                    json m = json::parse(data, nullptr, false);
                    data.clear();
                    haveData = false;
                    if (m.is_discarded())
                        return;
                    if (m.is_object() && !m.contains("id") && m.contains("method"))
                        dispatchNotification(m);
                    else if (found.is_null() && matches(m))
                        found = std::move(m);
                };

                size_t pos = 0;
                const std::string& text = httpResp.body;
                while (pos <= text.size())
                {
                    size_t eol = text.find('\n', pos);
                    std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line.empty())
                    {
                        flush();
                    }
                    else if (line.compare(0, 5, "data:") == 0)
                    {
                        std::string value = line.substr(5);
                        if (!value.empty() && value[0] == ' ')
                            value.erase(0, 1);
                        if (haveData)
                            data += '\n';
                        data += value;
                        haveData = true;
                    }
                    if (eol == std::string::npos)
                        break;
                    pos = eol + 1;
                }
                flush();

                if (found.is_null())
                    throw std::runtime_error("No JSON-RPC response in SSE stream");
                return found;
            }

            /// @brief Send JSON-RPC request and wait for response
            /// @param method Method name
            /// @param params Method parameters
            /// @return Response result
            /// @throws JsonRpcError on error response
            json sendRequest(const std::string& method, const json& params)
            {
                if (!m_connected)
                {
                    throw std::runtime_error("Not connected to MCP server");
                }

                // Create JSON-RPC request
                JsonRpcRequest request;
                request.jsonrpc = "2.0";
                request.method = method;
                request.params = params;
                request.id = m_requestId++;

                HttpClientRequest httpReq = makePost(request.serialize());
                HttpClientResponse httpResp;
                if (!m_httpClient->send(httpReq, httpResp))
                {
                    throw std::runtime_error("HTTP request failed");
                }

                if (httpResp.code != 200)
                {
                    throw std::runtime_error("HTTP error: " + std::to_string(httpResp.code) + " " + httpResp.message);
                }

                // The session id is assigned by the server in the initialize response
                if (method == "initialize")
                {
                    std::string sessionHeader = responseHeader(httpResp, "Mcp-Session-Id");
                    std::lock_guard<std::mutex> lock(m_sessionMutex);
                    m_sessionId = sessionHeader;
                }

                json message = extractResponse(httpResp, jsonRpcIdToJson(request.id));
                auto response = JsonRpcResponse::parse(message.dump());

                if (response.error.has_value())
                {
                    throw response.error.value();
                }

                return response.result.value_or(json::object());
            }

            /// @brief Send JSON-RPC notification (no response expected)
            /// @param notification Notification to send
            void sendNotification(const JsonRpcNotification& notification)
            {
                if (!m_connected)
                {
                    return;
                }

                HttpClientRequest httpReq = makePost(notification.serialize());
                HttpClientResponse httpResp;
                m_httpClient->send(httpReq, httpResp);
                // Ignore response for notifications (202 Accepted expected)
            }
        };

    } // namespace client
} // namespace mcp
SOCKETSHPP_NS_END
