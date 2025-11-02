// Minimal C++ ModelContext Protocol client using SocketsHpp
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include <SocketsHpp/mcp/client/mcp_client.h>

using namespace SocketsHpp;
using json = nlohmann::json;

int main() {
    MCPClient client("http://127.0.0.1:3001/"); // Call TS MCP server
    json input = {};
    auto resp = client.callTool("get_weather", input);
    if (resp && resp->contains("kind")) {
        std::cout << "[C++] Response from TS server: " << resp->dump() << std::endl;
    } else {
        std::cout << "[C++] Failed to get weather from TS server" << std::endl;
    }
    return 0;
}
