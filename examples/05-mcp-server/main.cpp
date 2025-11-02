// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Model Context Protocol (MCP) server example
///
/// This example demonstrates:
/// - MCP HTTP+SSE transport
/// - CORS configuration for web clients
/// - Session management
/// - DELETE method for session cleanup
/// - Base64 encoding for binary data
///
/// Note: This is a simplified MCP server showing the transport layer.
/// Full JSON-RPC 2.0 message handling would be needed for production.

#include <sockets.hpp>
#include <iostream>
#include <map>
#include <mutex>

using namespace SOCKETSHPP_NS::http::server;
using namespace SOCKETSHPP_NS::utils;

// Simple session store
std::map<std::string, std::time_t> sessions;
std::mutex sessions_mutex;

int main()
{
    try
    {
        HttpServer server(8080);
        std::cout << "MCP Server starting on http://localhost:8080" << std::endl;

        // Configure CORS for web-based MCP clients
        HttpServer::CorsConfig cors;
        cors.allow_origin = "*";  // In production, specify allowed origins
        cors.allow_methods = "GET, POST, DELETE, OPTIONS";
        cors.allow_headers = "Content-Type, Authorization";
        cors.max_age = 3600;

        // OPTIONS handler for CORS preflight
        server.route("/sse", [&cors](const HttpRequest& req, HttpResponse& res) {
            if (req.method == "OPTIONS") {
                res.set_header("Access-Control-Allow-Origin", cors.allow_origin);
                res.set_header("Access-Control-Allow-Methods", cors.allow_methods);
                res.set_header("Access-Control-Allow-Headers", cors.allow_headers);
                res.set_header("Access-Control-Max-Age", std::to_string(cors.max_age));
                res.set_status(204);
                res.send("");
                return;
            }

            // SSE endpoint for MCP messages
            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");
            res.set_header("Access-Control-Allow-Origin", cors.allow_origin);

            // Create session
            std::string session_id;
            {
                std::lock_guard<std::mutex> lock(sessions_mutex);
                // Simple UUID-like session ID (in production use proper UUID)
                session_id = "session-" + std::to_string(std::time(nullptr));
                sessions[session_id] = std::time(nullptr);
            }

            std::cout << "New MCP session: " << session_id << std::endl;

            // Send initialization message
            SSEEvent init;
            init.event = "message";
            init.id = "1";
            init.data = R"({
                "jsonrpc": "2.0",
                "method": "initialized",
                "params": {
                    "sessionId": ")" + session_id + R"(",
                    "serverInfo": {
                        "name": "SocketsHpp MCP Server",
                        "version": "1.0.0"
                    },
                    "capabilities": {
                        "tools": {},
                        "prompts": {},
                        "resources": {}
                    }
                }
            })";
            res.send_chunk(init.format());

            // Send welcome tool list
            SSEEvent tools;
            tools.event = "message";
            tools.id = "2";
            tools.data = R"({
                "jsonrpc": "2.0",
                "method": "tools/list",
                "result": {
                    "tools": [
                        {
                            "name": "echo",
                            "description": "Echoes back the input",
                            "inputSchema": {
                                "type": "object",
                                "properties": {
                                    "message": {"type": "string"}
                                }
                            }
                        }
                    ]
                }
            })";
            res.send_chunk(tools.format());

            // Keep connection alive with periodic pings
            for (int i = 0; i < 5; i++) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                
                SSEEvent ping;
                ping.event = "ping";
                ping.data = std::to_string(std::time(nullptr));
                res.send_chunk(ping.format());
            }

            std::cout << "MCP session ended: " << session_id << std::endl;
        });

        // DELETE handler for session cleanup
        server.route("/session", [](const HttpRequest& req, HttpResponse& res) {
            if (req.method == "DELETE") {
                // Extract session ID from Authorization header or query param
                std::string session_id = "demo-session";  // Simplified
                
                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    sessions.erase(session_id);
                }
                
                std::cout << "Deleted session: " << session_id << std::endl;
                
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_status(204);  // No Content
                res.send("");
            } else if (req.method == "OPTIONS") {
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Access-Control-Allow-Methods", "DELETE, OPTIONS");
                res.set_status(204);
                res.send("");
            } else {
                res.set_status(405);  // Method Not Allowed
                res.send("Only DELETE and OPTIONS allowed");
            }
        });

        // Info endpoint (optional MCP metadata)
        server.route("/info", [](const HttpRequest& req, HttpResponse& res) {
            res.set_header("Content-Type", "application/json");
            res.set_header("Access-Control-Allow-Origin", "*");
            res.send(R"({
                "name": "SocketsHpp MCP Server",
                "version": "1.0.0",
                "protocol": "mcp/1.0",
                "transport": "http-sse",
                "capabilities": {
                    "tools": true,
                    "prompts": false,
                    "resources": false
                }
            })");
        });

        // Base64 demo endpoint
        server.route("/base64", [](const HttpRequest& req, HttpResponse& res) {
            if (req.method == "POST") {
                std::string input = req.body;
                std::string encoded = base64::encode(input);
                
                res.set_header("Content-Type", "application/json");
                res.send(R"({"original":")" + input + R"(","encoded":")" + encoded + R"("})");
            } else {
                res.send("Send POST request with data to encode");
            }
        });

        std::cout << "MCP Server ready!" << std::endl;
        std::cout << "Endpoints:" << std::endl;
        std::cout << "  GET  /sse      - SSE event stream (MCP transport)" << std::endl;
        std::cout << "  DELETE /session - End MCP session" << std::endl;
        std::cout << "  GET  /info     - Server metadata" << std::endl;
        std::cout << "  POST /base64   - Base64 encoding demo" << std::endl;

        server.listen();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
