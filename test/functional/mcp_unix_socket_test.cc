// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// MCPServer listening on a Unix domain socket (ServerConfig::unixSocketPath),
// driven with HttpClient::setUnixSocketPath().

#include <gtest/gtest.h>

#include <SocketsHpp/http/client/http_client.h>
#include <SocketsHpp/mcp/common/mcp_config.h>
#include <SocketsHpp/mcp/server/mcp_server.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

using namespace SocketsHpp::mcp;
using namespace SocketsHpp::mcp::server;
using SocketsHpp::http::client::HttpClient;
using SocketsHpp::http::client::HttpClientRequest;
using SocketsHpp::http::client::HttpClientResponse;
using json = nlohmann::json;
namespace fs = std::filesystem;

#ifdef HAVE_UNIX_DOMAIN
namespace
{
    std::string socketPath()
    {
#  ifdef _WIN32
        const long pid = static_cast<long>(::GetCurrentProcessId());
#  else
        const long pid = static_cast<long>(::getpid());
#  endif
        fs::path p = fs::temp_directory_path() / ("shpp-mcp-" + std::to_string(pid) + ".sock");
        std::error_code ec;
        fs::remove(p, ec);
        return p.string();
    }
}  // namespace

TEST(McpUnixSocketTest, ListensOnUnixSocketOnly)
{
    ServerConfig cfg;
    cfg.transport = TransportType::HTTP_STREAMABLE;
    cfg.unixSocketPath = socketPath();
    cfg.host = "0.0.0.0";  // ignored (and not refused) when unixSocketPath is set
    cfg.port = 0;
    MCPServer server(cfg);
    try
    {
        server.listen();
    }
    catch (const std::runtime_error& e)
    {
#  ifdef _WIN32
        GTEST_SKIP() << "AF_UNIX not usable here: " << e.what();
#  else
        FAIL() << e.what();
#  endif
    }
    EXPECT_EQ(server.port(), -1);
    EXPECT_TRUE(fs::exists(cfg.unixSocketPath));

    HttpClient client;
    client.setUnixSocketPath(cfg.unixSocketPath);
    HttpClientResponse health;
    ASSERT_TRUE(client.get("http://localhost/health", health));
    EXPECT_EQ(health.code, 200);

    HttpClientRequest req;
    req.method = "POST";
    req.uri = "http://localhost/mcp";
    req.setHeader("Content-Type", "application/json");
    req.setHeader("Accept", "application/json");
    req.body = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})";
    HttpClientResponse init;
    ASSERT_TRUE(client.send(req, init));
    EXPECT_EQ(init.code, 200);
    json body = json::parse(init.body, nullptr, false);
    ASSERT_FALSE(body.is_discarded()) << init.body;
    EXPECT_EQ(body["id"], 1);
    EXPECT_TRUE(body.contains("result"));

    server.stop();
    EXPECT_FALSE(fs::exists(cfg.unixSocketPath));
}
#endif

TEST(McpUnixSocketTest, TcpStillGuardedWithoutUnixSocket)
{
    ServerConfig cfg;
    cfg.transport = TransportType::HTTP_STREAMABLE;
    cfg.host = "0.0.0.0";
    cfg.port = 0;
    MCPServer server(cfg);
    EXPECT_THROW(server.listen(), std::runtime_error);  // loopback guard
}
