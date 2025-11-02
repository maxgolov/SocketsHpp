// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/http/client/http_client.h>
#include <SocketsHpp/http/client/sse_client.h>
#include <SocketsHpp/http/common/json_rpc.h>
#include <SocketsHpp/mcp/common/mcp_config.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

SOCKETSHPP_NS_BEGIN
namespace mcp
{
    namespace client
    {
        using namespace http::client;
        using namespace http::common;
        using json = nlohmann::json;

        /// @brief MCP Client implementing Model Context Protocol
        class MCPClient
        {
        public:
            /// @brief Notification callback type
            using NotificationCallback = std::function<void(const json&)>;

            /// @brief Connection status callback
            using StatusCallback = std::function<void(bool connected, const std::string& message)>;

            /// @brief Constructor
            explicit MCPClient()
                : m_connected(false)
                , m_requestId(1)
            {
            }

            /// @brief Destructor
            ~MCPClient()
            {
                disconnect();
            }

            /// @brief Connect to MCP server
            /// @param config Client configuration
            /// @return true if connection successful
            bool connect(const ClientConfig& config)
            {
                m_config = config;

                if (config.transport == TransportType::HTTP)
                {
                    return connectHttp();
                }
                else if (config.transport == TransportType::STDIO)
                {
                    // STDIO transport not yet implemented
                    LOG_ERROR("MCPClient: STDIO transport not yet supported");
                    return false;
                }

                LOG_ERROR("MCPClient: Unknown transport type");
                return false;
            }

            /// @brief Disconnect from MCP server
            void disconnect()
            {
                if (!m_connected)
                {
                    return;
                }

                try
                {
                    // Send shutdown notification (no response expected)
                    JsonRpcNotification notif;
                    notif.method = "shutdown";
                    sendNotification(notif);
                }
                catch (...)
                {
                    // Ignore errors during shutdown
                }

                m_connected = false;

                if (m_sseClient)
                {
                    m_sseClient->close();
                }

                if (m_sseThread.joinable())
                {
                    m_sseThread.join();
                }
            }

            /// @brief Initialize MCP connection
            /// @param clientInfo Client information (name, version)
            /// @return Server capabilities and information
            json initialize(const json& clientInfo)
            {
                json params = {
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", json::object()},
                    {"clientInfo", clientInfo}
                };

                auto response = sendRequest("initialize", params);
                
                // Store server capabilities
                if (response.contains("capabilities"))
                {
                    m_serverCapabilities = response["capabilities"];
                }

                return response;
            }

            /// @brief Ping server (health check)
            /// @return Empty response on success
            json ping()
            {
                return sendRequest("ping", json::object());
            }

            /// @brief List available tools
            /// @return List of tools with names, descriptions, and schemas
            json listTools()
            {
                auto response = sendRequest("tools/list", json::object());
                return response.value("tools", json::array());
            }

            /// @brief Call a tool
            /// @param name Tool name
            /// @param arguments Tool arguments
            /// @return Tool execution result
            json callTool(const std::string& name, const json& arguments = json::object())
            {
                json params = {
                    {"name", name},
                    {"arguments", arguments}
                };

                return sendRequest("tools/call", params);
            }

            /// @brief List available prompts
            /// @return List of prompts with names and descriptions
            json listPrompts()
            {
                auto response = sendRequest("prompts/list", json::object());
                return response.value("prompts", json::array());
            }

            /// @brief Get a prompt
            /// @param name Prompt name
            /// @param arguments Prompt arguments
            /// @return Prompt messages
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

            /// @brief List available resources
            /// @return List of resources with URIs and metadata
            json listResources()
            {
                auto response = sendRequest("resources/list", json::object());
                return response.value("resources", json::array());
            }

            /// @brief Read a resource
            /// @param uri Resource URI
            /// @return Resource contents
            json readResource(const std::string& uri)
            {
                json params = {
                    {"uri", uri}
                };

                return sendRequest("resources/read", params);
            }

            /// @brief Subscribe to resource updates
            /// @param uri Resource URI
            /// @return Acknowledgment
            json subscribeResource(const std::string& uri)
            {
                json params = {
                    {"uri", uri}
                };

                return sendRequest("resources/subscribe", params);
            }

            /// @brief Unsubscribe from resource updates
            /// @param uri Resource URI
            /// @return Acknowledgment
            json unsubscribeResource(const std::string& uri)
            {
                json params = {
                    {"uri", uri}
                };

                return sendRequest("resources/unsubscribe", params);
            }

            /// @brief List resource templates
            /// @return List of resource templates
            json listResourceTemplates()
            {
                auto response = sendRequest("resources/templates/list", json::object());
                return response.value("resourceTemplates", json::array());
            }

            /// @brief Register notification handler
            /// @param method Notification method (e.g., "notifications/message")
            /// @param handler Handler function
            void onNotification(const std::string& method, NotificationCallback handler)
            {
                std::lock_guard<std::mutex> lock(m_notificationMutex);
                m_notificationHandlers[method] = handler;
            }

            /// @brief Register connection status callback
            /// @param callback Status callback
            void onStatus(StatusCallback callback)
            {
                m_statusCallback = callback;
            }

            /// @brief Check if connected
            bool isConnected() const { return m_connected; }

            /// @brief Get server capabilities
            const json& getServerCapabilities() const { return m_serverCapabilities; }

        private:
            ClientConfig m_config;
            bool m_connected;
            std::atomic<int> m_requestId;
            json m_serverCapabilities;
            std::string m_sessionId;

            // HTTP transport
            std::unique_ptr<HttpClient> m_httpClient;
            std::unique_ptr<SSEClient> m_sseClient;
            std::thread m_sseThread;

            // Notification handling
            std::map<std::string, NotificationCallback> m_notificationHandlers;
            std::mutex m_notificationMutex;
            StatusCallback m_statusCallback;

            /// @brief Connect via HTTP transport
            bool connectHttp()
            {
                m_httpClient = std::make_unique<HttpClient>();
                m_httpClient->setUserAgent("SocketsHpp-MCP-Client/1.0");

                // Setup HTTP client with config
                if (m_config.http.timeout > 0)
                {
                    m_httpClient->setReadTimeout(m_config.http.timeout);
                    m_httpClient->setConnectTimeout(m_config.http.timeout);
                }

                // Test connection with initialization (will be called again by user)
                // For now, just verify we can reach the endpoint
                m_connected = true;

                // Setup SSE stream for server→client messages if resumability enabled
                if (m_config.http.enableResumability)
                {
                    setupSSEStream();
                }

                if (m_statusCallback)
                {
                    m_statusCallback(true, "Connected to " + m_config.http.url);
                }

                return true;
            }

            /// @brief Setup SSE stream for server notifications
            void setupSSEStream()
            {
                m_sseClient = std::make_unique<SSEClient>();
                
                // Enable auto-reconnect
                m_sseClient->setAutoReconnect(true, 3000);

                // Start SSE stream in separate thread
                m_sseThread = std::thread([this]() {
                    std::string sseUrl = m_config.http.url;
                    
                    // Append session parameter if we have one
                    if (!m_sessionId.empty())
                    {
                        sseUrl += "?session=" + m_sessionId;
                    }

                    m_sseClient->connect(
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
                    // Parse event data as JSON-RPC notification
                    json data = json::parse(event.data);

                    // Check if it's a notification
                    if (!data.contains("id"))
                    {
                        auto notif = JsonRpcNotification::parse(event.data);
                        handleNotification(notif);
                    }
                    else
                    {
                        // It's a response to a previous request
                        // In MCP, responses come via POST, not SSE
                        // This might be an error or protocol violation
                        LOG_WARN("MCPClient: Received JSON-RPC response via SSE (unexpected)");
                    }
                }
                catch (const json::parse_error& e)
                {
                    LOG_ERROR("MCPClient: Failed to parse SSE event data: %s", e.what());
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("MCPClient: Error handling SSE event: %s", e.what());
                }
            }

            /// @brief Handle JSON-RPC notification from server
            void handleNotification(const JsonRpcNotification& notif)
            {
                std::lock_guard<std::mutex> lock(m_notificationMutex);
                
                auto it = m_notificationHandlers.find(notif.method);
                if (it != m_notificationHandlers.end())
                {
                    json params = notif.params.value_or(json::object());
                    it->second(params);
                }
                else
                {
                    LOG_WARN("MCPClient: Unhandled notification: %s", notif.method.c_str());
                }
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

                std::string requestStr = request.serialize();

                // Send via HTTP POST
                HttpClientRequest httpReq;
                httpReq.method = METHOD_POST;
                httpReq.uri = m_config.http.url;
                httpReq.setContentType("application/json");
                httpReq.body = requestStr;

                // Add custom headers from config
                for (const auto& header : m_config.http.headers)
                {
                    httpReq.setHeader(header.first, header.second);
                }

                // Add session header if we have one
                if (!m_sessionId.empty())
                {
                    httpReq.setHeader("Mcp-Session-Id", m_sessionId);
                }

                HttpClientResponse httpResp;
                bool success = m_httpClient->send(httpReq, httpResp);

                if (!success)
                {
                    throw std::runtime_error("HTTP request failed");
                }

                if (httpResp.code != 200)
                {
                    throw std::runtime_error("HTTP error: " + std::to_string(httpResp.code) + " " + httpResp.message);
                }

                // Extract session ID from response if present
                std::string sessionHeader = httpResp.getHeader("Mcp-Session-Id");
                if (!sessionHeader.empty() && m_sessionId.empty())
                {
                    m_sessionId = sessionHeader;
                }

                // Parse JSON-RPC response
                auto response = JsonRpcResponse::parse(httpResp.body);

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

                std::string notifStr = notification.serialize();

                HttpClientRequest httpReq;
                httpReq.method = METHOD_POST;
                httpReq.uri = m_config.http.url;
                httpReq.setContentType("application/json");
                httpReq.body = notifStr;

                for (const auto& header : m_config.http.headers)
                {
                    httpReq.setHeader(header.first, header.second);
                }

                if (!m_sessionId.empty())
                {
                    httpReq.setHeader("Mcp-Session-Id", m_sessionId);
                }

                HttpClientResponse httpResp;
                m_httpClient->send(httpReq, httpResp);
                // Ignore response for notifications
            }
        };

    } // namespace client
} // namespace mcp
SOCKETSHPP_NS_END
