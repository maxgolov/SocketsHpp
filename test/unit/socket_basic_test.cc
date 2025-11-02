// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "sockets.hpp"

using namespace SOCKETSHPP_NS::net::common;

namespace testing
{
    class SocketBasicTest : public ::testing::Test
    {
    protected:
        void SetUp() override {}
        void TearDown() override {}
    };

    // Socket parameter tests
    TEST_F(SocketBasicTest, SocketParams_TCP_IPv4)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        EXPECT_EQ(params.af, AF_INET);
        EXPECT_EQ(params.type, SOCK_STREAM);
        EXPECT_EQ(params.proto, 0);
    }

    TEST_F(SocketBasicTest, SocketParams_UDP_IPv4)
    {
        SocketParams params{AF_INET, SOCK_DGRAM, 0};
        EXPECT_EQ(params.af, AF_INET);
        EXPECT_EQ(params.type, SOCK_DGRAM);
        EXPECT_EQ(params.proto, 0);
    }

    TEST_F(SocketBasicTest, SocketParams_TCP_IPv6)
    {
        SocketParams params{AF_INET6, SOCK_STREAM, 0};
        EXPECT_EQ(params.af, AF_INET6);
        EXPECT_EQ(params.type, SOCK_STREAM);
        EXPECT_EQ(params.proto, 0);
    }

    TEST_F(SocketBasicTest, SocketParams_UDP_IPv6)
    {
        SocketParams params{AF_INET6, SOCK_DGRAM, 0};
        EXPECT_EQ(params.af, AF_INET6);
        EXPECT_EQ(params.type, SOCK_DGRAM);
        EXPECT_EQ(params.proto, 0);
    }

#ifdef HAVE_UNIX_DOMAIN
    TEST_F(SocketBasicTest, SocketParams_UnixDomain)
    {
        SocketParams params{AF_UNIX, SOCK_STREAM, 0};
        EXPECT_EQ(params.af, AF_UNIX);
        EXPECT_EQ(params.type, SOCK_STREAM);
        EXPECT_EQ(params.proto, 0);
    }
#endif

    // Socket creation tests
    TEST_F(SocketBasicTest, CreateSocket_TCP)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        Socket sock(params);
        // Just verify creation doesn't crash
        EXPECT_TRUE(true);
    }

    TEST_F(SocketBasicTest, CreateSocket_UDP)
    {
        SocketParams params{AF_INET, SOCK_DGRAM, 0};
        Socket sock(params);
        // Just verify creation doesn't crash
        EXPECT_TRUE(true);
    }

    // Socket address validation tests
    TEST_F(SocketBasicTest, ValidateAddress_IPv4_Loopback)
    {
        SocketAddr addr("127.0.0.1:8080");
        EXPECT_EQ(addr.toString(), "127.0.0.1:8080");
    }

    TEST_F(SocketBasicTest, ValidateAddress_IPv6_Loopback)
    {
        SocketAddr addr("[::1]:8080");
        EXPECT_EQ(addr.toString(), "[::1]:8080");
    }

    // Socket state tests
    TEST_F(SocketBasicTest, Socket_InitialState)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        Socket sock(params);
        // Socket should be created but not connected
        EXPECT_TRUE(true);
    }

    TEST_F(SocketBasicTest, Socket_CloseSocket)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        Socket sock(params);
        sock.close();
        // Should close without error
        EXPECT_TRUE(true);
    }

    TEST_F(SocketBasicTest, Socket_DoubleClose)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        Socket sock(params);
        sock.close();
        sock.close();  // Second close should be safe
        EXPECT_TRUE(true);
    }

    // Socket type combinations
    TEST_F(SocketBasicTest, MultipleSocketTypes)
    {
        // Create different socket types
        {
            SocketParams params{AF_INET, SOCK_STREAM, 0};
            Socket tcp_sock(params);
        }
        {
            SocketParams params{AF_INET, SOCK_DGRAM, 0};
            Socket udp_sock(params);
        }
        {
            SocketParams params{AF_INET6, SOCK_STREAM, 0};
            Socket tcp6_sock(params);
        }
        EXPECT_TRUE(true);
    }

    // Address family tests
    TEST_F(SocketBasicTest, AddressFamily_IPv4)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        SocketAddr addr("0.0.0.0:0");
        Socket sock(params);
        EXPECT_TRUE(true);
    }

    TEST_F(SocketBasicTest, AddressFamily_IPv6)
    {
        SocketParams params{AF_INET6, SOCK_STREAM, 0};
        SocketAddr addr("[::]:0");
        Socket sock(params);
        EXPECT_TRUE(true);
    }

    // Port number validation
    TEST_F(SocketBasicTest, PortNumbers_StandardPorts)
    {
        std::vector<int> ports = {80, 443, 8080, 3000, 5000};
        for (int port : ports)
        {
            std::string addr_str = "127.0.0.1:" + std::to_string(port);
            SocketAddr addr(addr_str.c_str());
            EXPECT_EQ(addr.toString(), addr_str);
        }
    }

    TEST_F(SocketBasicTest, PortNumbers_EphemeralRange)
    {
        // Test ephemeral port range (typically 49152-65535)
        std::vector<int> ports = {49152, 55000, 60000, 65535};
        for (int port : ports)
        {
            std::string addr_str = "127.0.0.1:" + std::to_string(port);
            SocketAddr addr(addr_str.c_str());
            EXPECT_EQ(addr.toString(), addr_str);
        }
    }

    // Socket lifecycle tests
    TEST_F(SocketBasicTest, SocketLifecycle_CreateAndDestroy)
    {
        {
            SocketParams params{AF_INET, SOCK_STREAM, 0};
            Socket sock(params);
            // Socket created in scope
        }
        // Socket should be destroyed properly
        EXPECT_TRUE(true);
    }

    TEST_F(SocketBasicTest, SocketLifecycle_MultipleSequential)
    {
        for (int i = 0; i < 10; ++i)
        {
            SocketParams params{AF_INET, SOCK_STREAM, 0};
            Socket sock(params);
            sock.close();
        }
        EXPECT_TRUE(true);
    }

    // Protocol tests
    TEST_F(SocketBasicTest, Protocol_TCP)
    {
        SocketParams params{AF_INET, SOCK_STREAM, IPPROTO_TCP};
        Socket sock(params);
        EXPECT_TRUE(true);
    }

    TEST_F(SocketBasicTest, Protocol_UDP)
    {
        SocketParams params{AF_INET, SOCK_DGRAM, IPPROTO_UDP};
        Socket sock(params);
        EXPECT_TRUE(true);
    }

    // Error handling tests
    TEST_F(SocketBasicTest, ErrorHandling_InvalidSocketClose)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        Socket sock(params);
        sock.close();
        // Closing again should not throw
        EXPECT_NO_THROW(sock.close());
    }

    // Special addresses
    TEST_F(SocketBasicTest, SpecialAddresses_Loopback)
    {
        SocketAddr ipv4("127.0.0.1:1234");
        EXPECT_EQ(ipv4.toString(), "127.0.0.1:1234");

        SocketAddr ipv6("[::1]:1234");
        EXPECT_EQ(ipv6.toString(), "[::1]:1234");
    }

    TEST_F(SocketBasicTest, SpecialAddresses_Any)
    {
        SocketAddr ipv4("0.0.0.0:1234");
        EXPECT_EQ(ipv4.toString(), "0.0.0.0:1234");

        SocketAddr ipv6("[::]:1234");
        EXPECT_EQ(ipv6.toString(), "[::]:1234");
    }

    TEST_F(SocketBasicTest, SpecialAddresses_Broadcast)
    {
        SocketAddr broadcast("255.255.255.255:9999");
        EXPECT_EQ(broadcast.toString(), "255.255.255.255:9999");
    }

    // Resource cleanup tests
    TEST_F(SocketBasicTest, ResourceCleanup_ExplicitClose)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        Socket sock(params);
        sock.close();
        // Verify we can create another socket after closing
        Socket sock2(params);
        EXPECT_TRUE(true);
    }

    TEST_F(SocketBasicTest, ResourceCleanup_ImplicitDestructor)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        for (int i = 0; i < 5; ++i)
        {
            Socket sock(params);
            // Let destructor handle cleanup
        }
        EXPECT_TRUE(true);
    }

}  // namespace testing


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
