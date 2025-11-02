// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Example demonstrating SocketsHpp consumption via vcpkg
///
/// This example shows:
/// - Installing SocketsHpp as a vcpkg package
/// - Using find_package() to locate the library
/// - Creating a simple HTTP server with minimal code

#include <sockets.hpp>
#include <SocketsHpp/http/server/http_server.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

using namespace SOCKETSHPP_NS;
using namespace SOCKETSHPP_NS::http::server;

int main()
{
    std::cout << "========================================\n";
    std::cout << " SocketsHpp vcpkg Consumption Example\n";
    std::cout << "========================================\n\n";

    std::cout << "This example demonstrates:\n";
    std::cout << "  - Installing SocketsHpp via vcpkg\n";
    std::cout << "  - Using CMake find_package()\n";
    std::cout << "  - Creating a minimal HTTP server\n\n";

    try
    {
        // Create HTTP server on port 9000
        HttpServer server("localhost", 9000);

        // Route 1: Root endpoint
        server.route("/", [](const HttpRequest& req, HttpResponse& res) -> int {
            res.set_header("Content-Type", "text/html");
            res.set_content(
                "<html><body>"
                "<h1>SocketsHpp via vcpkg</h1>"
                "<p>This server was built using SocketsHpp installed through vcpkg!</p>"
                "<h2>Available Endpoints:</h2>"
                "<ul>"
                "<li><a href='/info'>GET /info</a> - Server information</li>"
                "<li><a href='/echo?msg=Hello'>GET /echo?msg=...</a> - Echo service</li>"
                "<li><a href='/json'>GET /json</a> - JSON response</li>"
                "</ul>"
                "<h3>vcpkg Integration Benefits:</h3>"
                "<ul>"
                "<li>Automatic dependency management (nlohmann-json, cpp-jwt, thread-pool)</li>"
                "<li>Cross-platform builds (Windows, Linux, macOS)</li>"
                "<li>Header-only library - no linking required</li>"
                "<li>Easy version management</li>"
                "</ul>"
                "</body></html>"
            );
            return 200;
        });

        // Route 2: Server info
        server.route("/info", [](const HttpRequest& req, HttpResponse& res) -> int {
            res.set_header("Content-Type", "application/json");
            res.set_content(
                "{"
                "\"server\":\"SocketsHpp\","
                "\"version\":\"1.0.0\","
                "\"installation\":\"vcpkg\","
                "\"dependencies\":[\"nlohmann-json\",\"cpp-jwt\",\"bshoshany-thread-pool\"],"
                "\"features\":[\"HTTP/1.1\",\"SSE\",\"MCP\",\"Authentication\",\"Compression\"]"
                "}"
            );
            return 200;
        });

        // Route 3: Echo service
        server.route("/echo", [](const HttpRequest& req, HttpResponse& res) -> int {
            std::string message = "No message provided";
            
            // Parse query parameters
            auto params = req.query_params;
            auto it = params.find("msg");
            if (it != params.end()) {
                message = it->second;
            }

            res.set_header("Content-Type", "application/json");
            res.set_content(
                "{\"echo\":\"" + message + "\"}"
            );
            return 200;
        });

        // Route 4: JSON response
        server.route("/json", [](const HttpRequest& req, HttpResponse& res) -> int {
            res.set_header("Content-Type", "application/json");
            res.set_content(
                "{"
                "\"vcpkg\":{"
                "\"overlay_ports\":\"Use --overlay-ports=../../ports for development\","
                "\"manifest_mode\":\"Automatic dependency installation\","
                "\"toolchain\":\"CMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake\""
                "},"
                "\"installation\":{"
                "\"windows\":\"vcpkg install socketshpp --overlay-ports=../../ports\","
                "\"linux\":\"Same as Windows, works cross-platform\","
                "\"automatic\":\"vcpkg.json in your project enables manifest mode\""
                "}"
                "}"
            );
            return 200;
        });

        std::cout << "Server starting on http://localhost:9000\n";
        std::cout << "Press Ctrl+C to stop\n\n";
        std::cout << "Try these commands:\n";
        std::cout << "  curl http://localhost:9000/\n";
        std::cout << "  curl http://localhost:9000/info\n";
        std::cout << "  curl http://localhost:9000/echo?msg=HelloVcpkg\n";
        std::cout << "  curl http://localhost:9000/json\n\n";

        // Start server
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
