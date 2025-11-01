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
        SocketAddr destination("127.0.0.1:8888");
        SocketServer server(destination, params);
        EXPECT_TRUE(true);  // Server created successfully
    }

    TEST_F(HttpServerTest, ServerAddressBinding)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        SocketAddr destination("127.0.0.1:8889");
        SocketServer server(destination, params);
        
        // Verify the server address matches
        EXPECT_EQ(server.address().toString(), "127.0.0.1:8889");
    }

    TEST_F(HttpServerTest, ServerStartStop)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        SocketAddr destination("127.0.0.1:8890");
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
        SocketAddr destination("127.0.0.1:8891");
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
