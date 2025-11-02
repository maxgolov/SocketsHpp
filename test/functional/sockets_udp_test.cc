// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// Uncomment this line for additional debugging:
#define HAVE_CONSOLE_LOG

#include <cstdio>
#include <cstdlib>
#include <list>
#include <map>
#include <set>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "sockets.hpp"

#include "./utils.h"

using namespace SOCKETSHPP_NS::net::common;
using namespace std;

namespace testing
{

    TEST(SocketTests, BasicUdpTest)
    {
        SocketParams params{ AF_INET, SOCK_DGRAM, 0 };
        SocketAddr destination("127.0.0.1:40000");
        Socket client(params);
        client.connect(destination);

        std::string hello = "Hello!";
        auto total_bytes_sent = client.send(hello.c_str(), hello.length());
        EXPECT_EQ(total_bytes_sent, hello.size());

        client.close();
    }

}  // namespace testing

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
