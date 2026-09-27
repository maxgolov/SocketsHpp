// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file server.cpp
/// @brief MCP server on the stdio transport (StdioServerTransport).
///
/// Reads newline-delimited JSON-RPC from stdin and writes responses to stdout; logs go
/// to stderr. Tools:
/// - "add": {"a": number, "b": number} -> the sum
/// - "count": {"to": 1-100} -> counts with notifications/progress and a log message
///   every step (100 ms apart); cancellable with notifications/cancelled
///
/// Usage: normally started by an MCP client (e.g. mcp-stdio-client, or a
/// VS Code mcp.json "stdio" entry). To try it by hand, type one JSON message per line.

#include <sockets.hpp>

#include <atomic>
#include <chrono>
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
    // A tools/call result with a single text item
    json textResult(const std::string& text)
    {
        return {{"content", json::array({{{"type", "text"}, {"text", text}}})}};
    }
}  // namespace

int main()
{
    ServerConfig cfg;  // transport defaults to STDIO: nothing is bound
    cfg.serverName = "example-stdio-server";
    MCPServer server(cfg);
    StdioServerTransport stdio(server);

    server.registerMethod("initialize", [](const json&) -> json {
        return {
            {"capabilities", {{"tools", json::object()}, {"logging", json::object()}}},
            {"serverInfo", {{"name", "example-stdio-server"}, {"version", "1.0.0"}}},
        };
    });

    server.registerMethod("tools/list", [](const json&) -> json {
        return {{"tools", json::array({
            {{"name", "add"},
             {"description", "Add two numbers"},
             {"inputSchema", {{"type", "object"},
                              {"properties", {{"a", {{"type", "number"}}}, {"b", {{"type", "number"}}}}},
                              {"required", {"a", "b"}}}}},
            {{"name", "count"},
             {"description", "Count to N (1-100), reporting progress; cancellable"},
             {"inputSchema", {{"type", "object"},
                              {"properties", {{"to", {{"type", "integer"}}}}},
                              {"required", {"to"}}}}},
        })}};
    });

    // Cancellable, so a long "count" stops on notifications/cancelled.
    server.registerCancellable("tools/call", [&stdio](const json& params,
                                                      std::shared_ptr<std::atomic<bool>> cancelled) -> json {
        const std::string name = params.value("name", "");
        const json args = params.value("arguments", json::object());
        if (name == "add")
        {
            if (!args.contains("a") || !args.contains("b") || !args["a"].is_number() || !args["b"].is_number())
                throw JsonRpcError::invalidParams("add needs numbers a and b");
            const double sum = args["a"].get<double>() + args["b"].get<double>();
            std::cerr << "[server] add -> " << sum << std::endl;  // stderr, never stdout
            return textResult(json(sum).dump());
        }
        if (name == "count")
        {
            const int to = args.value("to", 0);
            if (to < 1 || to > 100)
                throw JsonRpcError::invalidParams("count needs 'to' in 1..100");
            // The progress token, if the client asked for progress, is in params._meta.
            const json token = params.contains("_meta") ? params["_meta"].value("progressToken", json()) : json();
            for (int i = 1; i <= to; ++i)
            {
                if (cancelled->load())
                    throw std::runtime_error("cancelled");  // no response is sent
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!token.is_null())
                    stdio.progress(token, i, to);
                stdio.log("info", "count", "reached " + std::to_string(i));
            }
            return textResult("Counted to " + std::to_string(to));
        }
        throw JsonRpcError::invalidParams("Unknown tool: " + name);
    });

    std::cerr << "[server] ready on stdio" << std::endl;
    stdio.runStdio();  // until stdin closes
    std::cerr << "[server] stdin closed, exiting" << std::endl;
    return 0;
}
