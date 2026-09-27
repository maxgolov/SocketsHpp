// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// MCPServer Unix-domain listening and configurable HTTP worker concurrency,
// driven with HttpClient over TCP and Unix-domain sockets.

#include <gtest/gtest.h>

#include <SocketsHpp/http/client/http_client.h>
#include <SocketsHpp/mcp/common/mcp_config.h>
#include <SocketsHpp/mcp/server/mcp_server.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

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

class McpWorkerPoolTest : public ::testing::TestWithParam<std::tuple<TransportType, bool, size_t>>
{
};

TEST_P(McpWorkerPoolTest, ConfiguredConcurrency)
{
    ServerConfig cfg;
    cfg.transport = std::get<0>(GetParam());
    const bool useUnix = std::get<1>(GetParam());
    cfg.workerThreads = std::get<2>(GetParam());
    cfg.port = 0;
    cfg.session.enabled = false;
    if (useUnix)
    {
#ifdef HAVE_UNIX_DOMAIN
        cfg.unixSocketPath = socketPath();
#else
        GTEST_SKIP() << "AF_UNIX is unavailable";
#endif
    }

    size_t workers = cfg.workerThreads;
    if (workers == 0)
    {
        workers = std::thread::hardware_concurrency();
        if (workers == 0) workers = 4;
    }
    const size_t expected = std::min<size_t>(workers, 6);
    const bool checkLimit = workers <= 6;  // Bound automatic-mode fanout on large hosts.
    const size_t requests = expected + (checkLimit ? 1 : 0);
    std::mutex mutex;
    std::condition_variable cv;
    size_t entered = 0;
    bool released = false;
    std::atomic<size_t> completed{0};
    MCPServer server(cfg);
    server.registerMethod("probe", [&](const json&) {
        std::unique_lock<std::mutex> lock(mutex);
        ++entered;
        cv.notify_all();
        // Bound cleanup even if the test fails before releasing the handlers.
        cv.wait_for(lock, std::chrono::seconds(15), [&] { return released; });
        return json{{"ok", true}};
    });
    try
    {
        server.listen();
    }
    catch (const std::runtime_error& e)
    {
#ifdef _WIN32
        if (useUnix) GTEST_SKIP() << "AF_UNIX not usable here: " << e.what();
#endif
        throw;
    }
    const std::string url = useUnix ? "http://localhost/mcp" :
        "http://127.0.0.1:" + std::to_string(server.port()) + "/mcp";
    std::vector<std::thread> clients;
    for (size_t i = 0; i < requests; ++i)
    {
        clients.emplace_back([&, i] {
            HttpClient client;
            if (useUnix) client.setUnixSocketPath(cfg.unixSocketPath);
            HttpClientRequest request;
            request.method = "POST";
            request.uri = url;
            request.setHeader("Content-Type", "application/json");
            request.setHeader("Accept", "application/json");
            request.body = json{{"jsonrpc", "2.0"}, {"id", i}, {"method", "probe"}}.dump();
            HttpClientResponse response;
            if (client.send(request, response) && response.code == 200)
            {
                const auto body = json::parse(response.body, nullptr, false);
                if (body.is_object() && body.contains("result") &&
                    body["result"] == json{{"ok", true}})
                    ++completed;
            }
        });
    }
    bool reached = false;
    bool exceeded = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        reached = cv.wait_for(lock, std::chrono::seconds(5), [&] { return entered >= expected; });
        if (checkLimit)
            exceeded = cv.wait_for(lock, std::chrono::milliseconds(250), [&] { return entered > expected; });
        released = true;
    }
    cv.notify_all();
    for (auto& client : clients) client.join();
    server.stop();
    EXPECT_TRUE(reached) << "configured workers did not enter together";
    EXPECT_FALSE(exceeded) << "pool exceeded the configured worker count";
    EXPECT_EQ(completed.load(), requests);
}

INSTANTIATE_TEST_SUITE_P(TransportsAndListeners, McpWorkerPoolTest,
    ::testing::Combine(::testing::Values(TransportType::HTTP, TransportType::HTTP_STREAMABLE),
                       ::testing::Bool(), ::testing::Values(size_t{0}, size_t{1}, size_t{6})));
