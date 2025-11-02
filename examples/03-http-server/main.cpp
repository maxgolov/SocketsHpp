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
        HttpServer server(8080);
        std::cout << "HTTP Server starting on http://localhost:8080" << std::endl;

        // Root endpoint - HTML page
        server.route("/", [](const HttpRequest& req, HttpResponse& res) {
            res.set_header("Content-Type", "text/html");
            res.send(R"(
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
        });

        // Hello endpoint
        server.route("/hello", [](const HttpRequest& req, HttpResponse& res) {
            res.set_header("Content-Type", "text/plain");
            res.send("Hello from SocketsHpp HTTP Server!");
        });

        // JSON API endpoint
        server.route("/api/info", [](const HttpRequest& req, HttpResponse& res) {
            res.set_header("Content-Type", "application/json");
            res.send(R"({
                "server": "SocketsHpp",
                "version": "1.0",
                "endpoints": ["/", "/hello", "/api/info", "/echo"]
            })");
        });

        // Echo endpoint with query parameters
        server.route("/echo", [](const HttpRequest& req, HttpResponse& res) {
            auto params = req.parse_query();
            std::string msg = "No message provided";
            
            auto it = params.find("msg");
            if (it != params.end()) {
                msg = it->second;
            }

            res.set_header("Content-Type", "text/plain");
            res.send("Echo: " + msg);
        });

        // POST endpoint for JSON data
        server.route("/api/data", [](const HttpRequest& req, HttpResponse& res) {
            if (req.method == "POST") {
                std::string body = req.body;
                res.set_header("Content-Type", "application/json");
                res.send(R"({"received": )" + std::to_string(body.length()) + R"( bytes", "status": "ok"})");
            } else {
                res.set_status(405); // Method Not Allowed
                res.send("Only POST allowed");
            }
        });

        std::cout << "Server running! Press Ctrl+C to stop" << std::endl;
        std::cout << "Visit http://localhost:8080 in your browser" << std::endl;

        // Start server (blocks until stopped)
        server.listen();

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
