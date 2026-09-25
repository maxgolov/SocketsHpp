// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// Functional test for HTTP server components

#include <gtest/gtest.h>
#include "sockets.hpp"
#include "../common/test_utils.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace SOCKETSHPP_NS::net::common;
using namespace SOCKETSHPP_NS::http::server;

namespace testing
{
    class HttpServerTest : public ::testing::Test
    {
    protected:
        void SetUp() override {}
        void TearDown() override {}
    };

    TEST_F(HttpServerTest, BasicServerCreation)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        SocketAddr destination("127.0.0.1:0");
        SocketServer server(destination, params);
        EXPECT_TRUE(true);  // Server created successfully
    }

    TEST_F(HttpServerTest, ServerAddressBinding)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        SocketAddr destination("127.0.0.1:0");
        SocketServer server(destination, params);
        
        // Verify the server address matches
        // Binding to port 0 picks an ephemeral port; address() reports it.
        EXPECT_GT(server.address().port(), 0);
        EXPECT_EQ(server.address().toString(), "127.0.0.1:" + std::to_string(server.address().port()));
    }

    TEST_F(HttpServerTest, ServerStartStop)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        SocketAddr destination("127.0.0.1:0");
        SocketServer server(destination, params);
        
        server.Start();
        // Server should be running
        server.Stop();
        // Server should be stopped
        EXPECT_TRUE(true);
    }

    TEST_F(HttpServerTest, MultipleStartStop)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        SocketAddr destination("127.0.0.1:0");
        SocketServer server(destination, params);
        
        // Start and stop multiple times
        for (int i = 0; i < 3; ++i)
        {
            server.Start();
            server.Stop();
        }
        EXPECT_TRUE(true);
    }

    // --- Portable end-to-end tests (run on Windows too) -------------------------
    // These exercise the thread-pool dispatch path, whose send handoff relies on
    // platform-specific writable notifications (FD_WRITE on Windows).

    static std::string MakePayload(size_t size)
    {
        std::string payload(size, '\0');
        for (size_t i = 0; i < size; ++i)
            payload[i] = static_cast<char>('a' + (i * 7919) % 26);
        return payload;
    }

    class HttpServerEndToEndTest : public ::testing::TestWithParam<bool>
    {
    };

    TEST_P(HttpServerEndToEndTest, SmallAndLargeResponses)
    {
        const bool useThreadPool = GetParam();
        HttpServer server("127.0.0.1", 0);
        if (useThreadPool)
            server.enableThreadPool(2);
        const std::string big = MakePayload(4 * 1024 * 1024);
        server.route("/small", [](const HttpRequest&, HttpResponse& res) {
            res.set_content("hello");
            return 200;
        });
        server.route("/big", [&big](const HttpRequest&, HttpResponse& res) {
            res.set_content(big, "application/octet-stream");
            return 200;
        });
        server.start();
        const std::string base = "http://127.0.0.1:" + std::to_string(server.getListeningPort());

        SOCKETSHPP_NS::http::client::HttpClient client;
        for (int i = 0; i < 20; ++i)
        {
            SOCKETSHPP_NS::http::client::HttpClientResponse res;
            ASSERT_TRUE(client.get(base + "/small", res)) << "request " << i;
            EXPECT_EQ(res.code, 200);
            EXPECT_EQ(res.body, "hello");
        }
        SOCKETSHPP_NS::http::client::HttpClientResponse res;
        ASSERT_TRUE(client.get(base + "/big", res));
        EXPECT_EQ(res.code, 200);
        ASSERT_EQ(res.body.size(), big.size());
        EXPECT_TRUE(res.body == big);
        server.stop();
    }

    TEST_P(HttpServerEndToEndTest, ConcurrentClients)
    {
        const bool useThreadPool = GetParam();
        HttpServer server("127.0.0.1", 0);
        if (useThreadPool)
            server.enableThreadPool(4);
        server.route("/id/", [](const HttpRequest& req, HttpResponse& res) {
            res.set_content(req.uri);
            return 200;
        });
        server.start();
        const std::string base = "http://127.0.0.1:" + std::to_string(server.getListeningPort());

        std::atomic<int> ok{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t)
        {
            threads.emplace_back([&, t]() {
                SOCKETSHPP_NS::http::client::HttpClient client;
                for (int i = 0; i < 10; ++i)
                {
                    const std::string path = "/id/" + std::to_string(t) + "-" + std::to_string(i);
                    SOCKETSHPP_NS::http::client::HttpClientResponse res;
                    if (client.get(base + path, res) && res.code == 200 && res.body == path)
                        ++ok;
                }
            });
        }
        for (auto& th : threads)
            th.join();
        EXPECT_EQ(ok.load(), 80);
        server.stop();
    }

    // A stateful (mutable) stream callback must keep its state across chunks in
    // both dispatch modes; the stream must end when it returns "".
    TEST_P(HttpServerEndToEndTest, StatefulStreamCallback)
    {
        const bool useThreadPool = GetParam();
        HttpServer server("127.0.0.1", 0);
        if (useThreadPool)
            server.enableThreadPool(2);
        server.route("/events", [](const HttpRequest&, HttpResponse& res) {
            res.set_header("Content-Type", "text/event-stream");
            int n = 0;
            res.send_chunk_stream([n]() mutable -> std::string {
                if (n == 3)
                    return "";
                ++n;
                return SSEEvent::message("e" + std::to_string(n), std::to_string(n)).format();
            });
            return 200;
        });
        server.start();

        SOCKETSHPP_NS::http::client::HttpClient client;
        client.setReadTimeout(5000);
        SOCKETSHPP_NS::http::client::HttpClientResponse res;
        ASSERT_TRUE(client.get("http://127.0.0.1:" + std::to_string(server.getListeningPort()) + "/events", res));
        EXPECT_EQ(res.code, 200);
        EXPECT_EQ(res.body, "id: 1\ndata: e1\n\nid: 2\ndata: e2\n\nid: 3\ndata: e3\n\n");
        server.stop();
    }

    // Handlers that set a body (or a status) but return 0 have handled the request,
    // and the most specific route wins regardless of registration order.
    TEST_P(HttpServerEndToEndTest, BodyWithoutStatusIsOkAndLongestPrefixWins)
    {
        const bool useThreadPool = GetParam();
        HttpServer server("127.0.0.1", 0);
        if (useThreadPool)
            server.enableThreadPool(2);
        server.route("/", [](const HttpRequest&, HttpResponse& res) {
            res.set_content("root");
            return 0;
        });
        server.route("/api", [](const HttpRequest&, HttpResponse& res) {
            res.set_content("api");
            return 0;
        });
        server.route("/declines", [](const HttpRequest&, HttpResponse&) { return 0; });
        server.start();
        const std::string base = "http://127.0.0.1:" + std::to_string(server.getListeningPort());

        SOCKETSHPP_NS::http::client::HttpClient client;
        SOCKETSHPP_NS::http::client::HttpClientResponse res;
        ASSERT_TRUE(client.get(base + "/api/items", res));
        EXPECT_EQ(res.code, 200);
        EXPECT_EQ(res.body, "api");
        ASSERT_TRUE(client.get(base + "/other", res));
        EXPECT_EQ(res.code, 200);
        EXPECT_EQ(res.body, "root");
        ASSERT_TRUE(client.get(base + "/declines", res));  // falls through to "/"
        EXPECT_EQ(res.body, "root");
        server.stop();
    }

    INSTANTIATE_TEST_SUITE_P(Dispatch, HttpServerEndToEndTest, ::testing::Values(false, true),
                             [](const ::testing::TestParamInfo<bool>& info) {
                                 return info.param ? std::string("ThreadPool") : std::string("Reactor");
                             });

}  // namespace testing


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
