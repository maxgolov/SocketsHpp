// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Simple HTTP server example with routing
///
/// This example demonstrates:
/// - Creating an HTTP server
/// - Defining routes with handlers
/// - Sending JSON and HTML responses
/// - Handling different HTTP methods

#include <sockets.hpp>
#include <iostream>

using namespace SOCKETSHPP_NS::http::server;

int main()
{
    try
    {
        // Create HTTP server on port 8080
        HttpServer server("0.0.0.0", 8080);
        std::cout << "HTTP Server starting on http://localhost:8080" << std::endl;

        // Root endpoint - HTML page
        server.route("/", [](const HttpRequest& req, HttpResponse& res) -> int {
            res.set_header("Content-Type", "text/html");
            res.set_content(R"(
                <!DOCTYPE html>
                <html>
                <head><title>SocketsHpp HTTP Server</title></head>
                <body>
                    <h1>Welcome to SocketsHpp HTTP Server!</h1>
                    <p>Try these endpoints:</p>
                    <ul>
                        <li><a href="/hello">GET /hello</a></li>
                        <li><a href="/api/info">GET /api/info</a></li>
                        <li><a href="/echo?msg=test">GET /echo?msg=test</a></li>
                    </ul>
                </body>
                </html>
            )");
            return 0;
        });

        // Hello endpoint
        server.route("/hello", [](const HttpRequest& req, HttpResponse& res) -> int {
            res.set_header("Content-Type", "text/plain");
            res.set_content("Hello from SocketsHpp HTTP Server!");
            return 0;
        });

        // JSON API endpoint
        server.route("/api/info", [](const HttpRequest& req, HttpResponse& res) -> int {
            res.set_header("Content-Type", "application/json");
            res.set_content(R"({
                "server": "SocketsHpp",
                "version": "1.0",
                "endpoints": ["/", "/hello", "/api/info", "/echo"]
            })");
            return 0;
        });

        // Echo endpoint with query parameters
        server.route("/echo", [](const HttpRequest& req, HttpResponse& res) -> int {
            auto params = req.getQueryParams();
            std::string msg = "No message provided";
            
            auto it = params.find("msg");
            if (it != params.end()) {
                msg = it->second;
            }

            res.set_header("Content-Type", "text/plain");
            res.set_content("Echo: " + msg);
            return 0;
        });

        // POST endpoint for JSON data
        server.route("/api/data", [](const HttpRequest& req, HttpResponse& res) -> int {
            if (req.method == "POST") {
                std::string body = req.getBody();
                res.set_header("Content-Type", "application/json");
                res.set_content(R"({"received": )" + std::to_string(body.length()) + R"( bytes", "status": "ok"})");
            } else {
                res.set_status(405); // Method Not Allowed
                res.set_content("Only POST allowed");
            }
            return 0;
        });

        std::cout << "Server running! Press Ctrl+C to stop" << std::endl;
        std::cout << "Visit http://localhost:8080 in your browser" << std::endl;

        // Start server (blocks until stopped)
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
