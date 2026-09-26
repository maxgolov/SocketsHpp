// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief A small HTTP server built against SocketsHpp installed by vcpkg.
///
/// The build finds the library with find_package(SocketsHpp CONFIG REQUIRED) and
/// links SocketsHpp::SocketsHpp; vcpkg also provides nlohmann-json, used here to
/// build the JSON responses.

#include <sockets.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <thread>

using namespace SOCKETSHPP_NS::http::server;
using json = nlohmann::json;

namespace
{
    std::atomic<bool> g_running{true};
    constexpr int kPort = 9000;
}

int main()
{
    std::signal(SIGINT, [](int) { g_running = false; });
    std::signal(SIGTERM, [](int) { g_running = false; });

    try
    {
        HttpServer server("127.0.0.1", kPort);

        // "/" also catches unknown paths (routes match by longest prefix).
        server.route("/", [](const HttpRequest&, HttpResponse& res) -> int {
            res.set_content(
                "<html><body><h1>SocketsHpp via vcpkg</h1><ul>"
                "<li><a href='/info'>GET /info</a> - server information</li>"
                "<li><a href='/echo?msg=Hello'>GET /echo?msg=...</a> - echo a query parameter</li>"
                "<li><a href='/json'>GET /json</a> - a sample JSON document</li>"
                "</ul></body></html>",
                "text/html");
            return 200;
        });

        server.route("/info", [](const HttpRequest&, HttpResponse& res) -> int {
            json info = {
                {"server", "SocketsHpp"},
                {"installedWith", "vcpkg"},
                {"dependencies", {"nlohmann-json", "bshoshany-thread-pool"}},
            };
            res.set_content(info.dump(), "application/json");
            return 200;
        });

        server.route("/echo", [](const HttpRequest& req, HttpResponse& res) -> int {
            std::map<std::string, std::string> params;
            try
            {
                params = req.parse_query();
            }
            catch (const std::exception& e)
            {
                res.set_content(json{{"error", e.what()}}.dump(), "application/json");
                return 400;  // malformed query string, e.g. a bad %-escape
            }
            auto it = params.find("msg");
            json body = {{"echo", it != params.end() ? it->second : "No message provided"}};
            res.set_content(body.dump(), "application/json");  // dump() escapes the input
            return 200;
        });

        server.route("/json", [](const HttpRequest&, HttpResponse& res) -> int {
            json doc = {
                {"library", "SocketsHpp"},
                {"headerOnly", true},
                {"cmake", {{"find_package", "SocketsHpp CONFIG REQUIRED"}, {"target", "SocketsHpp::SocketsHpp"}}},
            };
            res.set_content(doc.dump(2), "application/json");
            return 200;
        });

        server.start();
        std::cout << "Listening on http://127.0.0.1:" << kPort << " (Ctrl+C to stop)\n"
                  << "  curl http://127.0.0.1:" << kPort << "/info\n"
                  << "  curl \"http://127.0.0.1:" << kPort << "/echo?msg=HelloVcpkg\"\n";

        while (g_running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        server.stop();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
