// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file client.cpp
/// @brief MCP client on the stdio transport: launches the example server as a child
///        process and calls its tools.
///
/// Usage: mcp-stdio-client [server-executable [args...]]
///        (default: mcp-stdio-server next to this executable)
///
/// Exits with 0 when every call succeeded.

#include <sockets.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace SOCKETSHPP_NS::mcp;
using namespace SOCKETSHPP_NS::mcp::client;
using json = nlohmann::json;

namespace
{
    // Path of the example server: same directory as this program.
    std::string defaultServerPath(const std::string& self)
    {
        const size_t slash = self.find_last_of("/\\");
        const std::string dir = slash == std::string::npos ? std::string(".") : self.substr(0, slash);
#ifdef _WIN32
        return dir + "\\mcp-stdio-server.exe";
#else
        return dir + "/mcp-stdio-server";
#endif
    }
}  // namespace

int main(int argc, char* argv[])
{
    ClientConfig cfg;
    cfg.transport = TransportType::STDIO;
    cfg.stdio.command = argc > 1 ? argv[1] : defaultServerPath(argv[0]);
    for (int i = 2; i < argc; ++i)
        cfg.stdio.args.push_back(argv[i]);
    cfg.readTimeoutSeconds = 30;  // per request

    MCPClient client;
    client.onStderr([](const std::string& line) { std::cerr << "  (server stderr) " << line << "\n"; });
    client.onNotification("notifications/message", [](const json& p) {
        std::cout << "  log: " << p.value("data", json()).dump() << "\n";
    });
    client.onNotification("notifications/progress", [](const json& p) {
        std::cout << "  progress: " << p.value("progress", 0.0) << "/" << p.value("total", 0.0) << "\n";
    });

    if (!client.connect(cfg))
    {
        std::cerr << "cannot start " << cfg.stdio.command << "\n";
        return 1;
    }

    try
    {
        client.setClientInfo({{"name", "example-stdio-client"}, {"version", "1.0.0"}});
        const json init = client.initialize();
        std::cout << "Connected to " << init["serverInfo"].dump() << ", protocol "
                  << client.protocolVersion() << "\n";

        // Ask for info-level logs (the default is warning).
        client.request("logging/setLevel", {{"level", "info"}});

        for (const auto& tool : client.listTools())
            std::cout << "Tool: " << tool.value("name", "") << " - " << tool.value("description", "") << "\n";

        const json sum = client.callTool("add", {{"a", 2}, {"b", 3}});
        std::cout << "add(2, 3) = " << sum["content"][0]["text"].get<std::string>() << "\n";

        // A request with a progress token; its progress and log notifications are
        // printed by the handlers above while it runs.
        const json counted = client.request("tools/call", {{"name", "count"},
                                                           {"arguments", {{"to", 3}}},
                                                           {"_meta", {{"progressToken", "count-1"}}}});
        std::cout << counted["content"][0]["text"].get<std::string>() << "\n";

        // Cancel a long call after 300 ms.
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        std::thread canceller([cancel] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            *cancel = true;
        });
        try
        {
            client.callTool("count", {{"to", 100}}, cancel);
            std::cout << "count(100) finished (not expected)\n";
        }
        catch (const std::runtime_error& e)
        {
            std::cout << "count(100): " << e.what() << "\n";
        }
        canceller.join();
    }
    catch (const SOCKETSHPP_NS::http::common::JsonRpcError& e)
    {
        std::cerr << "JSON-RPC error " << e.code << ": " << e.message << "\n";
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    client.disconnect();  // closes the server's stdin; it exits on its own
    std::cout << "Disconnected\n";
    return 0;
}
