// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Minimal MCP (Model Context Protocol) server with MCPServer.
///
/// This example demonstrates:
/// - MCPServer on the Streamable HTTP transport at http://127.0.0.1:<port>/mcp
/// - registerTool() / registerCancellableTool(): built-in tools/list and tools/call
///   with two tools, "echo" and "wait", and the tools capability advertised
///   automatically in the initialize result
/// - A cancellable tool ("wait" stops early on notifications/cancelled)
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
        ServerConfig cfg;
        cfg.transport = TransportType::HTTP_STREAMABLE;
        cfg.host = "127.0.0.1";  // MCPServer refuses non-loopback hosts unless allowNonLoopback is set
        cfg.port = argc > 1 ? std::stoi(argv[1]) : 8080;
        cfg.endpoint = "/mcp";
        cfg.serverName = "example-mcp-server";
        cfg.serverVersion = "1.0.0";

        MCPServer server(cfg);

        // registerTool() provides tools/list and tools/call, and the default initialize
        // result (protocol version negotiation, serverInfo from cfg) advertises the tools
        // capability. A handler returning a string produces one text content item; a
        // handler that throws produces a result with "isError": true and the message.
        server.registerTool(
            "echo", "Return the given text",
            {{"type", "object"}, {"properties", {{"text", {{"type", "string"}}}}}, {"required", {"text"}}},
            [](const json& args) -> json {
                if (!args.contains("text") || !args["text"].is_string())
                {
                    throw std::invalid_argument("echo: 'text' must be a string");
                }
                return args["text"].get<std::string>();
            });

        // A cancellable tool: handlers run on MCPServer's worker threads, so a
        // notifications/cancelled for this request (same session, params.requestId =
        // this request's id) is processed while "wait" runs and sets `cancelled`.
        server.registerCancellableTool(
            "wait", "Wait for the given number of seconds (1-60); cancellable",
            {{"type", "object"}, {"properties", {{"seconds", {{"type", "integer"}}}}}, {"required", {"seconds"}}},
            [](const json& args, std::shared_ptr<std::atomic<bool>> cancelled) -> json {
                if (!args.contains("seconds") || !args["seconds"].is_number_integer())
                {
                    throw std::invalid_argument("wait: 'seconds' must be an integer");
                }
                const int seconds = args["seconds"].get<int>();
                if (seconds < 1 || seconds > 60)
                {
                    throw std::invalid_argument("wait: 'seconds' must be between 1 and 60");
                }
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
                while (std::chrono::steady_clock::now() < deadline)
                {
                    if (cancelled->load())
                    {
                        throw std::runtime_error("cancelled");  // no response is sent for it
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                return "Waited " + std::to_string(seconds) + " s";
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
