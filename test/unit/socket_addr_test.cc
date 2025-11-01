// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "sockets.hpp"

using namespace SOCKETSHPP_NS::net::common;

namespace testing
{
    class SocketAddrTest : public ::testing::Test
    {
    protected:
        void SetUp() override {}
        void TearDown() override {}
    };

    // IPv4 address parsing tests
    TEST_F(SocketAddrTest, IPv4_BasicParsing)
    {
        SocketAddr addr("127.0.0.1:3000");
        std::string result = addr.toString();
        EXPECT_EQ(result, "127.0.0.1:3000");
    }

    TEST_F(SocketAddrTest, IPv4_LocalhostParsing)
    {
        SocketAddr addr("localhost:8080");
        // Note: toString() may resolve to actual IP
        std::string result = addr.toString();
        EXPECT_FALSE(result.empty());
    }

    TEST_F(SocketAddrTest, IPv4_ZeroAddress)
    {
        SocketAddr addr("0.0.0.0:5000");
        std::string result = addr.toString();
        EXPECT_EQ(result, "0.0.0.0:5000");
    }

    TEST_F(SocketAddrTest, IPv4_HighPort)
    {
        SocketAddr addr("192.168.1.100:65535");
        std::string result = addr.toString();
        EXPECT_EQ(result, "192.168.1.100:65535");
    }

    TEST_F(SocketAddrTest, IPv4_LowPort)
    {
        SocketAddr addr("10.0.0.1:1");
        std::string result = addr.toString();
        EXPECT_EQ(result, "10.0.0.1:1");
    }

    TEST_F(SocketAddrTest, IPv4_StandardHttpPort)
    {
        SocketAddr addr("192.168.0.1:80");
        std::string result = addr.toString();
        EXPECT_EQ(result, "192.168.0.1:80");
    }

    TEST_F(SocketAddrTest, IPv4_StandardHttpsPort)
    {
        SocketAddr addr("172.16.0.1:443");
        std::string result = addr.toString();
        EXPECT_EQ(result, "172.16.0.1:443");
    }

    // IPv6 address parsing tests
    TEST_F(SocketAddrTest, IPv6_BasicParsing)
    {
        SocketAddr addr("[::1]:3000");
        std::string result = addr.toString();
        EXPECT_EQ(result, "[::1]:3000");
    }

    TEST_F(SocketAddrTest, IPv6_FullAddress)
    {
        SocketAddr addr("[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:8080");
        std::string result = addr.toString();
        // May be compressed to short form
        EXPECT_FALSE(result.empty());
        EXPECT_TRUE(result.find(']:') != std::string::npos);
    }

    TEST_F(SocketAddrTest, IPv6_LinkLocalAddress)
    {
        SocketAddr addr("[fe80::c018:4a9b:3681:4e41]:3000");
        std::string result = addr.toString();
        EXPECT_EQ(result, "[fe80::c018:4a9b:3681:4e41]:3000");
    }

    TEST_F(SocketAddrTest, IPv6_CompressedAddress)
    {
        SocketAddr addr("[2001:db8::1]:9000");
        std::string result = addr.toString();
        EXPECT_FALSE(result.empty());
        EXPECT_TRUE(result.find(']:') != std::string::npos);
    }

    TEST_F(SocketAddrTest, IPv6_AllZeros)
    {
        SocketAddr addr("[::]:8080");
        std::string result = addr.toString();
        EXPECT_EQ(result, "[::]:8080");
    }

    // Port range tests
    TEST_F(SocketAddrTest, PortRange_Min)
    {
        SocketAddr addr("127.0.0.1:1");
        std::string result = addr.toString();
        EXPECT_TRUE(result.find(":1") != std::string::npos);
    }

    TEST_F(SocketAddrTest, PortRange_Max)
    {
        SocketAddr addr("127.0.0.1:65535");
        std::string result = addr.toString();
        EXPECT_TRUE(result.find(":65535") != std::string::npos);
    }

    TEST_F(SocketAddrTest, PortRange_CommonPorts)
    {
        // Test various common port numbers
        const int common_ports[] = {21, 22, 23, 25, 53, 80, 110, 143, 443, 3306, 5432, 8080, 8888};
        for (int port : common_ports)
        {
            std::string addr_str = "127.0.0.1:" + std::to_string(port);
            SocketAddr addr(addr_str.c_str());
            std::string result = addr.toString();
            EXPECT_EQ(result, addr_str);
        }
    }

    // Round-trip conversion tests
    TEST_F(SocketAddrTest, RoundTrip_IPv4)
    {
        std::string original = "192.168.1.50:12345";
        SocketAddr addr(original.c_str());
        std::string result = addr.toString();
        EXPECT_EQ(original, result);
    }

    TEST_F(SocketAddrTest, RoundTrip_IPv6)
    {
        std::string original = "[fe80::1]:54321";
        SocketAddr addr(original.c_str());
        std::string result = addr.toString();
        EXPECT_EQ(original, result);
    }

    // Edge cases
    TEST_F(SocketAddrTest, EdgeCase_BroadcastAddress)
    {
        SocketAddr addr("255.255.255.255:9999");
        std::string result = addr.toString();
        EXPECT_EQ(result, "255.255.255.255:9999");
    }

    TEST_F(SocketAddrTest, EdgeCase_PrivateNetworkAddresses)
    {
        // Class A private
        {
            SocketAddr addr("10.0.0.1:1000");
            EXPECT_EQ(addr.toString(), "10.0.0.1:1000");
        }
        // Class B private
        {
            SocketAddr addr("172.16.0.1:2000");
            EXPECT_EQ(addr.toString(), "172.16.0.1:2000");
        }
        // Class C private
        {
            SocketAddr addr("192.168.0.1:3000");
            EXPECT_EQ(addr.toString(), "192.168.0.1:3000");
        }
    }

#ifdef HAVE_UNIX_DOMAIN
    // Unix domain socket tests
    TEST_F(SocketAddrTest, UnixDomain_BasicPath)
    {
        SocketAddr addr("/tmp/test.sock", true);
        // Unix domain sockets have different string representation
        EXPECT_TRUE(true);  // Just verify it doesn't crash
    }

    TEST_F(SocketAddrTest, UnixDomain_LongPath)
    {
        std::string long_path = "/tmp/very/long/path/to/socket/file.sock";
        SocketAddr addr(long_path.c_str(), true);
        EXPECT_TRUE(true);  // Verify construction succeeds
    }
#endif

    // Comparison and equality tests
    TEST_F(SocketAddrTest, Equality_SameAddress)
    {
        SocketAddr addr1("127.0.0.1:3000");
        SocketAddr addr2("127.0.0.1:3000");
        EXPECT_EQ(addr1.toString(), addr2.toString());
    }

    TEST_F(SocketAddrTest, Inequality_DifferentPort)
    {
        SocketAddr addr1("127.0.0.1:3000");
        SocketAddr addr2("127.0.0.1:3001");
        EXPECT_NE(addr1.toString(), addr2.toString());
    }

    TEST_F(SocketAddrTest, Inequality_DifferentHost)
    {
        SocketAddr addr1("127.0.0.1:3000");
        SocketAddr addr2("127.0.0.2:3000");
        EXPECT_NE(addr1.toString(), addr2.toString());
    }

}  // namespace testing
