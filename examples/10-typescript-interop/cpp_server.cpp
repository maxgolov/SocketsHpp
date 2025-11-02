// C++ JSON-RPC server using SocketsHpp HTTP server
// Demonstrates cross-language interop: TypeScript client calling C++ server

#include <sockets.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <random>
#include <thread>
#include <chrono>

using namespace SocketsHpp::http::server;
using json = nlohmann::json;

std::string random_weather()
{
    static const char* kinds[] = {"sunny", "cloudy", "rainy", "stormy", "snowy", "windy"};
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_int_distribution<int> dist(0, 5);
    return kinds[dist(rng)];
}

int main()
{
    try
    {
        HttpServer server("127.0.0.1", 3000);

        // Route for /mcp endpoint - handle JSON-RPC requests (MCP protocol)
        server.route("/mcp", [](const HttpRequest& req, HttpResponse& resp) -> int {
            // CORS headers
            resp.set_header("Access-Control-Allow-Origin", "*");
            resp.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            resp.set_header("Access-Control-Allow-Headers", "Content-Type");

            if (req.method == "OPTIONS")
            {
                resp.set_status(204);
                return 204;
            }

            if (req.method != "POST")
            {
                resp.set_status(405);
                resp.set_content("Method Not Allowed");
                return 405;
            }

            try
            {
                // Parse JSON-RPC request
                json rpcReq = json::parse(req.content);
                std::string method = rpcReq.value("method", std::string());
                json params = rpcReq.value("params", json::object());
                json id = rpcReq.contains("id") ? rpcReq["id"] : json(nullptr);

                json result;

                // Handle MCP protocol methods
                if (method == "initialize")
                {
                    std::cout << "[C++] Initialize called" << std::endl;
                    result = json{
                        {"protocolVersion", "2024-11-05"},
                        {"capabilities", {{"tools", json::object()}}},
                        {"serverInfo", {{"name", "cpp-server"}, {"version", "1.0.0"}}}
                    };
                }
                else if (method == "tools/list")
                {
                    std::cout << "[C++] Listing tools" << std::endl;
                    result = json{
                        {"tools", json::array({{
                            {"name", "get_weather"},
                            {"description", "Get random weather condition"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", json::object()}
                            }}
                        }})}
                    };
                }
                else if (method == "tools/call")
                {
                    std::string toolName = params.value("name", std::string());
                    std::cout << "[C++] Calling tool: " << toolName << std::endl;

                    if (toolName == "get_weather")
                    {
                        result = json{
                            {"content", json::array({{
                                {"type", "text"},
                                {"text", "Weather: " + random_weather()}
                            }})}
                        };
                    }
                    else
                    {
                        throw std::runtime_error("Unknown tool: " + toolName);
                    }
                }
                else
                {
                    throw std::runtime_error("Unknown method: " + method);
                }

                // Send JSON-RPC success response
                json rpcResp = {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"result", result}
                };
                resp.set_header("Content-Type", "application/json");
                resp.set_content(rpcResp.dump());
                return 200;
            }
            catch (const std::exception& e)
            {
                // Send JSON-RPC error response
                json rpcResp = {
                    {"jsonrpc", "2.0"},
                    {"id", json(nullptr)},
                    {"error", {
                        {"code", -32603},
                        {"message", std::string(e.what())}
                    }}
                };
                resp.set_header("Content-Type", "application/json");
                resp.set_content(rpcResp.dump());
                return 500;
            }
        });

        std::cout << "[C++] MCP server running at http://127.0.0.1:3000/mcp" << std::endl;
        std::cout << "[C++] Press Ctrl+C to stop" << std::endl;
        server.start();

        // Keep server running
        std::cout << "[C++] Server started. Waiting for connections..." << std::endl;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
}
