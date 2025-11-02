// Minimal C++ ModelContext Protocol server using SocketsHpp
#include <iostream>
#include <string>
#include <random>
#include <nlohmann/json.hpp>
#include <SocketsHpp/mcp/server/mcp_server.h>

using namespace SocketsHpp;
using json = nlohmann::json;

std::string random_weather() {
    static const char* kinds[] = { "sunny", "cloudy", "rainy", "stormy", "snowy", "windy" };
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_int_distribution<int> dist(0, 5);
    return kinds[dist(rng)];
}

int main() {
    MCPServer server("127.0.0.1", 3000);
    server.registerTool({
        .name = "get_weather",
        .description = "Return random weather kind"
    }, [](const json& /*input*/) -> json {
        return { {"kind", random_weather()} };
    });
    std::cout << "[C++] MCP server running at http://127.0.0.1:3000/\n";
    server.start(); // blocks
}
