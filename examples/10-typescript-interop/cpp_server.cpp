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

        // registerTool() provides tools/list and tools/call; the default initialize result
        // advertises the tools capability and serverInfo (cfg.serverName/serverVersion), and
        // MCPServer negotiates the protocol version (the newest both sides support).
        server.registerTool("get_weather", "Get a random weather condition",
                            {{"type", "object"}, {"properties", json::object()}},
                            [](const json&) -> json { return "Weather: " + randomWeather(); });

        server.registerTool("greet", "Greet a person by name",
                            {{"type", "object"},
                             {"properties", {{"name", {{"type", "string"}}}}},
                             {"required", json::array({"name"})}},
                            [](const json& args) -> json {
                                return "Hello, " + args.value("name", std::string("stranger")) +
                                       "! Nice to meet you!";
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
