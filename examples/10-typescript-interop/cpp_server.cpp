// C++ MCP server (SocketsHpp MCPServer, Streamable HTTP transport).
// The TypeScript client (ts/client.ts, official MCP TypeScript SDK) connects to it.
//
//   ./cpp_server [port]        (default port 3000, endpoint /mcp)

#include <sockets.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <random>
#include <string>
#include <thread>

using namespace SocketsHpp::mcp;
using namespace SocketsHpp::mcp::server;
using SocketsHpp::http::common::JsonRpcError;
using json = nlohmann::json;

namespace
{
    std::atomic<bool> g_running{true};

    std::string randomWeather()
    {
        static const char* kinds[] = {"sunny", "cloudy", "rainy", "stormy", "snowy", "windy"};
        static std::mt19937 rng{std::random_device{}()};
        return kinds[std::uniform_int_distribution<int>(0, 5)(rng)];
    }

    json textResult(const std::string& text)
    {
        return {{"content", json::array({{{"type", "text"}, {"text", text}}})}};
    }
}  // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, [](int) { g_running = false; });
    std::signal(SIGTERM, [](int) { g_running = false; });

    try
    {
        ServerConfig cfg;
        cfg.transport = TransportType::HTTP_STREAMABLE;
        cfg.host = "127.0.0.1";
        cfg.port = argc > 1 ? std::stoi(argv[1]) : 3000;
        cfg.endpoint = "/mcp";
        cfg.serverName = "cpp-mcp-server";
        cfg.serverVersion = "1.0.0";

        MCPServer server(cfg);

        // initialize: advertise the tools capability. The protocol version is negotiated
        // by MCPServer (it answers with a version both sides support).
        server.registerMethod("initialize", [](const json&) -> json {
            return {
                {"capabilities", {{"tools", json::object()}}},
                {"serverInfo", {{"name", "cpp-mcp-server"}, {"version", "1.0.0"}}},
            };
        });

        server.registerMethod("tools/list", [](const json&) -> json {
            return {{"tools", json::array({
                {{"name", "get_weather"},
                 {"description", "Get a random weather condition"},
                 {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}},
                {{"name", "greet"},
                 {"description", "Greet a person by name"},
                 {"inputSchema", {{"type", "object"},
                                  {"properties", {{"name", {{"type", "string"}}}}},
                                  {"required", json::array({"name"})}}}},
            })}};
        });

        server.registerMethod("tools/call", [](const json& params) -> json {
            const std::string name = params.value("name", std::string());
            const json args = params.value("arguments", json::object());
            if (name == "get_weather")
                return textResult("Weather: " + randomWeather());
            if (name == "greet")
                return textResult("Hello, " + args.value("name", std::string("stranger")) + "! Nice to meet you!");
            throw JsonRpcError::invalidParams("Unknown tool: " + name);
        });

        server.listen();  // non-blocking
        std::cout << "[C++ server] MCP server (SocketsHpp) listening at http://127.0.0.1:" << server.port()
                  << "/mcp" << std::endl;

        while (g_running)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server.stop();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[C++ server] Error: " << e.what() << std::endl;
        return 1;
    }
}
