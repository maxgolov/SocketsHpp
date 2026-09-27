// C++ MCP client (SocketsHpp MCPClient, Streamable HTTP transport).
// It connects to the TypeScript server (ts/server.ts, official MCP TypeScript SDK), lists
// its tools and calls them. Exits non-zero on any failure, so it doubles as an interop check.
//
//   ./cpp_client [url]        (default http://127.0.0.1:3001/mcp)

#include <sockets.hpp>

#include <iostream>
#include <string>

using namespace SocketsHpp::mcp;
using SocketsHpp::http::common::JsonRpcError;
using SocketsHpp::mcp::client::MCPClient;
using json = nlohmann::json;

namespace
{
    std::string textOf(const json& result)
    {
        std::string text;
        for (const auto& item : result.value("content", json::array()))
        {
            if (item.value("type", "") == "text")
                text += item.value("text", "");
        }
        return text;
    }
}  // namespace

int main(int argc, char** argv)
{
    const std::string url = argc > 1 ? argv[1] : "http://127.0.0.1:3001/mcp";
    try
    {
        ClientConfig config;
        config.transport = TransportType::HTTP_STREAMABLE;
        config.http.url = url;

        MCPClient client;
        if (!client.connect(config))
            throw std::runtime_error("connect failed");

        json init = client.initialize({{"name", "cpp-mcp-client"}, {"version", "1.0.0"}});
        std::cout << "[C++ client] Connected to " << init["serverInfo"].value("name", "?") << " "
                  << init["serverInfo"].value("version", "?") << " at " << url
                  << " (protocol " << init.value("protocolVersion", "?") << ")" << std::endl;

        json tools = client.listTools();
        std::cout << "[C++ client] Tools:";
        for (const auto& tool : tools)
            std::cout << " " << tool.value("name", "?");
        std::cout << std::endl;

        std::cout << "[C++ client] get_weather -> " << textOf(client.callTool("get_weather")) << std::endl;

        const std::string greeting = textOf(client.callTool("greet", {{"name", "C++"}}));
        std::cout << "[C++ client] greet -> " << greeting << std::endl;
        if (greeting.find("C++") == std::string::npos)
            throw std::runtime_error("unexpected greet result: " + greeting);

        client.disconnect();  // HTTP DELETE: ends the session on the server
        std::cout << "[C++ client] OK" << std::endl;
        return 0;
    }
    catch (const JsonRpcError& e)
    {
        std::cerr << "[C++ client] FAILED: JSON-RPC error " << e.code << ": " << e.message << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[C++ client] FAILED: " << e.what() << std::endl;
    }
    return 1;
}
