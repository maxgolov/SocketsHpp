// C++ HTTP client calling TypeScript JSON-RPC server
// Demonstrates cross-language interop: C++ client calling TypeScript server

#include <sockets.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

using namespace SocketsHpp::http::client;
using json = nlohmann::json;

// Helper to make JSON-RPC calls
json callMcpMethod(const std::string& method, const json& params = json::object())
{
    HttpClient client;
    HttpClientResponse resp;
    
    json rpcReq = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params},
        {"id", 1}
    };
    
    std::string body = rpcReq.dump();
    bool success = client.post("http://localhost:3001/mcp", body, resp);

    if (!success || resp.code != 200)
    {
        throw std::runtime_error("HTTP error: " + std::to_string(resp.code));
    }

    if (resp.body.empty())
    {
        throw std::runtime_error("Empty response from server");
    }

    json rpcResp = json::parse(resp.body);
    if (rpcResp.contains("error"))
    {
        throw std::runtime_error("JSON-RPC error: " + rpcResp["error"]["message"].get<std::string>());
    }

    return rpcResp["result"];
}

int main()
{
    try
    {
        std::cout << "[C++] Connecting to TypeScript MCP server..." << std::endl;

        // Step 1: Initialize
        json initResult = callMcpMethod("initialize", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", json::object()},
            {"clientInfo", {{"name", "cpp-client"}, {"version", "1.0.0"}}}
        });
        std::cout << "[C++] Initialized. Server: " << initResult.dump(2) << std::endl;

        // Step 2: List tools
        json toolsResult = callMcpMethod("tools/list");
        std::cout << "[C++] Available tools:\n" << toolsResult.dump(2) << std::endl;

        // Step 3: Call a tool
        json callResult = callMcpMethod("tools/call", {
            {"name", "greet"},
            {"arguments", {{"name", "Alice"}}}
        });
        std::cout << "[C++] Tool result:\n" << callResult.dump(2) << std::endl;

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[C++] Error: " << e.what() << std::endl;
        return 1;
    }
}
