// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief HTTP server with a few routes.
///
/// This example demonstrates:
/// - Creating an HttpServer and registering routes
/// - HTML, plain-text and JSON responses (Content-Type passed to set_content())
/// - Query parameters, request bodies, 400 / 404 / 405 responses
/// - Graceful shutdown on Ctrl+C
///
/// Usage: http-server [port]   (default 8080)

#include <sockets.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <map>
#include <string>
#include <thread>

using namespace SOCKETSHPP_NS::http::server;
using json = nlohmann::json;

namespace
{
    std::atomic<bool> g_running{true};
    void onSignal(int) { g_running = false; }
}  // namespace

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        const int port = argc > 1 ? std::stoi(argv[1]) : 8080;
        HttpServer server("0.0.0.0", port);  // listens on all IPv4 interfaces

        // Routes match by prefix, longest first. "/" is a prefix of every URI, so this
        // handler also receives every path that no longer route claims: answer 404
        // unless the path is exactly "/".
        server.route("/", [](const HttpRequest& req, HttpResponse& res) -> int {
            const std::string path = req.uri.substr(0, req.uri.find('?'));
            if (path != "/")
            {
                res.set_content("Not found: " + path + "\n", "text/plain");
                return 404;
            }
            res.set_content(R"(<!DOCTYPE html>
<html>
<head><title>SocketsHpp HTTP Server</title></head>
<body>
  <h1>Welcome to SocketsHpp HTTP Server!</h1>
  <ul>
    <li><a href="/hello">GET /hello</a></li>
    <li><a href="/api/info">GET /api/info</a></li>
    <li><a href="/echo?msg=test">GET /echo?msg=test</a></li>
  </ul>
</body>
</html>
)",
                            "text/html; charset=utf-8");
            return 200;
        });

        server.route("/hello", [](const HttpRequest&, HttpResponse& res) -> int {
            res.set_content("Hello from SocketsHpp HTTP Server!\n", "text/plain");
            return 200;
        });

        server.route("/api/info", [](const HttpRequest&, HttpResponse& res) -> int {
            json info = {
                {"server", "SocketsHpp"},
                {"endpoints", {"/", "/hello", "/api/info", "/echo", "/api/data"}},
            };
            res.set_content(info.dump(), "application/json");
            return 200;
        });

        // Query parameters: /echo?msg=...
        server.route("/echo", [](const HttpRequest& req, HttpResponse& res) -> int {
            std::map<std::string, std::string> params;
            try
            {
                params = req.parse_query();  // throws on a malformed query such as ?msg=%zz
            }
            catch (const std::exception& e)
            {
                res.set_content(std::string("Bad query string: ") + e.what() + "\n", "text/plain");
                return 400;
            }
            auto it = params.find("msg");
            res.set_content("Echo: " + (it != params.end() ? it->second : "No message provided") + "\n",
                            "text/plain");
            return 200;
        });

        // POST-only route that parses a JSON request body
        server.route("/api/data", [](const HttpRequest& req, HttpResponse& res) -> int {
            if (req.method != "POST")
            {
                res.set_header("Allow", "POST");
                res.set_content("Only POST allowed\n", "text/plain");
                return 405;
            }
            json body = json::parse(req.content, nullptr, /*allow_exceptions=*/false);
            if (body.is_discarded())
            {
                res.set_content(json{{"error", "request body is not valid JSON"}}.dump(), "application/json");
                return 400;
            }
            json reply = {{"status", "ok"}, {"receivedBytes", req.content.size()}, {"type", body.type_name()}};
            res.set_content(reply.dump(), "application/json");
            return 200;
        });

        server.start();  // non-blocking: requests are served on background threads
        std::cout << "HTTP server running on http://localhost:" << port << " - press Ctrl+C to stop"
                  << std::endl;

        while (g_running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "Shutting down..." << std::endl;
        server.stop();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
