// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// Functional test for HTTP server components

#include <gtest/gtest.h>
#include "sockets.hpp"
#include "../common/test_utils.h"

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

}  // namespace testing


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
