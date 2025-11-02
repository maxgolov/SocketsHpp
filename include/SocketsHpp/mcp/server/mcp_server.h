// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/common/json_rpc.h>
#include <SocketsHpp/mcp/common/mcp_config.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
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
        class MCPServer
        {
        public:
            /// @brief Method handler function type
            /// @param params JSON-RPC parameters
            /// @return JSON-RPC result
            /// @throws JsonRpcError on error
            using MethodHandler = std::function<json(const json& params)>;

            /// @brief Create MCP server with configuration
            /// @param config Server configuration
            explicit MCPServer(const ServerConfig& config)
                : m_config(config)
                , m_httpServer(config.host, config.port)
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

                // Setup routes based on transport type
                if (config.transport == TransportType::HTTP)
                {
                    setupHttpRoutes();
                }
                // STDIO transport handled externally (read from stdin, write to stdout)
            }

            /// @brief Register a method handler
            /// @param method Method name (e.g., "initialize", "tools/list")
            /// @param handler Handler function
            void registerMethod(const std::string& method, MethodHandler handler)
            {
                m_methods[method] = std::move(handler);
            }

            /// @brief Start server (HTTP mode only)
            void listen()
            {
                if (m_config.transport != TransportType::HTTP)
                {
                    throw std::runtime_error("listen() only available in HTTP transport mode");
                }

                m_running = true;
                m_httpServer.listen();
            }

            /// @brief Stop server
            void stop()
            {
                m_running = false;
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
            ServerConfig m_config;
            HttpServer m_httpServer;
            SessionManager m_sessionManager;
            std::map<std::string, MethodHandler> m_methods;
            bool m_running;

            /// @brief Setup HTTP routes for MCP protocol
            void setupHttpRoutes()
            {
                std::string endpoint = m_config.endpoint;

                // POST: Handle JSON-RPC requests
                m_httpServer.route(endpoint, [this](const HttpRequest& req, HttpResponse& res) {
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
                });
            }

            /// @brief Handle HTTP POST request (JSON-RPC)
            void handleHttpPost(const HttpRequest& req, HttpResponse& res)
            {
                // Apply CORS headers
                applyCorsHeaders(res);

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
                    auto request = JsonRpcRequest::parse(req.content);
                    
                    // Special handling for initialize - create session
                    if (request.method == "initialize")
                    {
                        std::string sessionId = m_sessionManager.createSession();
                        auto response = handleRequest(request);
                        
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

            /// @brief Handle HTTP GET request (SSE stream)
            void handleHttpGet(const HttpRequest& req, HttpResponse& res)
            {
                // Apply CORS headers
                applyCorsHeaders(res);

                // Authenticate if required
                if (!authenticate(req, res))
                {
                    return;
                }

                // Get session ID from query params
                auto params = req.parse_query();
                auto sessionIt = params.find("session");
                if (sessionIt == params.end())
                {
                    res.set_status(400);
                    res.send("Missing session parameter");
                    return;
                }

                std::string sessionId = sessionIt->second;
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

                // Setup SSE stream
                res.set_header("Content-Type", "text/event-stream");
                res.set_header("Cache-Control", "no-cache");
                res.set_header("Connection", "keep-alive");
                res.set_header(m_config.session.headerName, sessionId);

                // Replay missed events if resuming
                if (!lastEventId.empty() && m_config.resumability.enabled)
                {
                    auto missedEvents = m_sessionManager.getEventsSince(sessionId, lastEventId);
                    for (const auto& event : missedEvents)
                    {
                        res.send_chunk(event);
                    }
                }

                // Keep connection open for server-to-client messages
                // In a real implementation, you'd maintain this connection
                // and send events as they occur. For now, we just keep it open.
                res.send_chunk(""); // Send empty line to establish connection
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

            /// @brief Authenticate request
            /// @return true if authenticated or auth not required, false otherwise
            bool authenticate(const HttpRequest& req, HttpResponse& res)
            {
                if (!m_config.auth.enabled)
                {
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
