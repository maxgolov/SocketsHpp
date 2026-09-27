// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Minimal MCP (Model Context Protocol) server with MCPServer.
///
/// This example demonstrates:
/// - MCPServer on the Streamable HTTP transport at http://127.0.0.1:<port>/mcp
/// - An initialize handler advertising the tools capability
/// - tools/list and tools/call with two tools: "echo" and "wait"
/// - A cancellable handler ("wait" stops early on notifications/cancelled)
/// - Sessions (Mcp-Session-Id) and graceful shutdown
///
/// Usage: mcp-server [port]   (default 8080)

#include <sockets.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace SOCKETSHPP_NS::mcp;
using namespace SOCKETSHPP_NS::mcp::server;
using SOCKETSHPP_NS::http::common::JsonRpcError;
using json = nlohmann::json;

namespace
{
    std::atomic<bool> g_running{true};
    void onSignal(int) { g_running = false; }

    // A tools/call result with a single text item
    json textResult(const std::string& text)
    {
        return {{"content", json::array({{{"type", "text"}, {"text", text}}})}};
    }
}  // namespace

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        ServerConfig cfg;
        cfg.transport = TransportType::HTTP_STREAMABLE;
        cfg.host = "127.0.0.1";  // MCPServer refuses non-loopback hosts unless allowNonLoopback is set
        cfg.port = argc > 1 ? std::stoi(argv[1]) : 8080;
        cfg.endpoint = "/mcp";
        cfg.serverName = "example-mcp-server";
        cfg.serverVersion = "1.0.0";

        MCPServer server(cfg);

        // The protocol version and session are handled by MCPServer; this handler
        // supplies the rest of the initialize result.
        server.registerMethod("initialize", [](const json&) -> json {
            return {
                {"capabilities", {{"tools", json::object()}}},
                {"serverInfo", {{"name", "example-mcp-server"}, {"version", "1.0.0"}}},
            };
        });

        server.registerMethod("tools/list", [](const json&) -> json {
            return {{"tools", json::array({
                {{"name", "echo"},
                 {"description", "Return the given text"},
                 {"inputSchema", {{"type", "object"},
                                  {"properties", {{"text", {{"type", "string"}}}}},
                                  {"required", {"text"}}}}},
                {{"name", "wait"},
                 {"description", "Wait for the given number of seconds (1-60); cancellable"},
                 {"inputSchema", {{"type", "object"},
                                  {"properties", {{"seconds", {{"type", "integer"}}}}},
                                  {"required", {"seconds"}}}}},
            })}};
        });

        // Handlers run on MCPServer's worker threads, so a notifications/cancelled for
        // this request (same session, params.requestId = this request's id) is
        // processed while "wait" runs and sets `cancelled`.
        server.registerCancellable(
            "tools/call", [](const json& params, std::shared_ptr<std::atomic<bool>> cancelled) -> json {
                const std::string name = params.value("name", "");
                const json args = params.value("arguments", json::object());

                if (name == "echo")
                {
                    if (!args.contains("text") || !args["text"].is_string())
                    {
                        throw JsonRpcError::invalidParams("echo: 'text' must be a string");
                    }
                    return textResult(args["text"].get<std::string>());
                }
                if (name == "wait")
                {
                    if (!args.contains("seconds") || !args["seconds"].is_number_integer())
                    {
                        throw JsonRpcError::invalidParams("wait: 'seconds' must be an integer");
                    }
                    const int seconds = args["seconds"].get<int>();
                    if (seconds < 1 || seconds > 60)
                    {
                        throw JsonRpcError::invalidParams("wait: 'seconds' must be between 1 and 60");
                    }
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
                    while (std::chrono::steady_clock::now() < deadline)
                    {
                        if (cancelled->load())
                        {
                            throw std::runtime_error("cancelled");  // sent back as a -32603 error
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    return textResult("Waited " + std::to_string(seconds) + " s");
                }
                throw JsonRpcError::invalidParams("Unknown tool: " + name);
            });

        server.listen();  // non-blocking
        std::cout << "MCP server listening at http://127.0.0.1:" << server.port() << "/mcp" << std::endl;

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
