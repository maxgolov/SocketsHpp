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
#include <chrono>
#include <ctime>
#include <thread>

using namespace SOCKETSHPP_NS::http::server;
using namespace SOCKETSHPP_NS::utils;

// Simple session store
std::map<std::string, std::time_t> sessions;
std::mutex sessions_mutex;

int main()
{
    try
    {
        HttpServer server("0.0.0.0", 8080);
        server.enableThreadPool(4);  // SSE stream callbacks may block between events
        std::cout << "MCP Server starting on http://localhost:8080" << std::endl;

        // Configure CORS for web-based MCP clients (note: CorsConfig not in current API, manual headers)
        const std::string allow_origin = "*";
        const std::string allow_methods = "GET, POST, DELETE, OPTIONS";
        const std::string allow_headers = "Content-Type, Authorization";
        const int max_age = 3600;

        // OPTIONS handler for CORS preflight
        server.route("/sse", [&](const HttpRequest& req, HttpResponse& res) -> int {
            if (req.method == "OPTIONS") {
                res.set_header("Access-Control-Allow-Origin", allow_origin);
                res.set_header("Access-Control-Allow-Methods", allow_methods);
                res.set_header("Access-Control-Allow-Headers", allow_headers);
                res.set_header("Access-Control-Max-Age", std::to_string(max_age));
                res.set_status(204);
                res.set_content("");
                return 0;
            }

            // SSE endpoint for MCP messages
            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");
            res.set_header("Access-Control-Allow-Origin", allow_origin);

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
            // Stream the messages with send_chunk_stream(): the callback is called
            // repeatedly and each returned chunk is sent immediately ("" ends the stream).
            // It runs on the thread pool, so the pauses between pings don't block
            // other clients.
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

            int step = 0;
            res.send_chunk_stream(
                [step, init, tools]() mutable -> std::string {
                    switch (step++)
                    {
                    case 0:
                        return init.format();   // initialization message
                    case 1:
                        return tools.format();  // welcome tool list
                    default:
                        if (step > 7)
                        {
                            return "";  // end of stream after 5 pings
                        }
                        // Keep the connection alive with periodic pings
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                        SSEEvent ping;
                        ping.event = "ping";
                        ping.data = std::to_string(std::time(nullptr));
                        return ping.format();
                    }
                },
                [session_id]() { std::cout << "MCP session ended: " << session_id << std::endl; });
            return 200;
        });

        // DELETE handler for session cleanup
        server.route("/session", [&](const HttpRequest& req, HttpResponse& res) -> int {
            if (req.method == "DELETE") {
                // Extract session ID from Authorization header or query param
                std::string session_id = "demo-session";  // Simplified
                
                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    sessions.erase(session_id);
                }
                
                std::cout << "Deleted session: " << session_id << std::endl;
                
                res.set_header("Access-Control-Allow-Origin", allow_origin);
                res.set_status(204);  // No Content
                res.set_content("");
            } else if (req.method == "OPTIONS") {
                res.set_header("Access-Control-Allow-Origin", allow_origin);
                res.set_header("Access-Control-Allow-Methods", "DELETE, OPTIONS");
                res.set_status(204);
                res.set_content("");
            } else {
                res.set_status(405);  // Method Not Allowed
                res.set_content("Only DELETE and OPTIONS allowed");
            }
            return 0;
        });

        // Info endpoint (optional MCP metadata)
        server.route("/info", [&](const HttpRequest& req, HttpResponse& res) -> int {
            res.set_header("Content-Type", "application/json");
            res.set_header("Access-Control-Allow-Origin", allow_origin);
            res.set_content(R"({
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
            return 0;
        });

        // Base64 demo endpoint
        server.route("/base64", [&](const HttpRequest& req, HttpResponse& res) -> int {
            if (req.method == "POST") {
                std::string input = req.content;
                std::string encoded = base64::encode(input);
                
                res.set_header("Content-Type", "application/json");
                res.set_content(R"({"original":")" + input + R"(","encoded":")" + encoded + R"("})");
            } else {
                res.set_content("Send POST request with data to encode");
            }
            return 0;
        });

        std::cout << "MCP Server ready!" << std::endl;
        std::cout << "Endpoints:" << std::endl;
        std::cout << "  GET  /sse      - SSE event stream (MCP transport)" << std::endl;
        std::cout << "  DELETE /session - End MCP session" << std::endl;
        std::cout << "  GET  /info     - Server metadata" << std::endl;
        std::cout << "  POST /base64   - Base64 encoding demo" << std::endl;

        server.start();
        
        // Keep server running
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
