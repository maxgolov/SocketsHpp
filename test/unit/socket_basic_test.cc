// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "sockets.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace SOCKETSHPP_NS::net::common;

namespace
{
    /// Returns true if the host is able to create sockets of the given family.
    /// IPv6 is frequently disabled in containers and CI sandboxes.
    bool familySupported(int af)
    {
        try
        {
            ScopedSocket probe(af, SOCK_STREAM, 0);
            return !probe.get().invalid();
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

#define SKIP_IF_NO_IPV6()                                                   \
    do                                                                      \
    {                                                                       \
        if (!familySupported(AF_INET6))                                     \
            GTEST_SKIP() << "IPv6 sockets are not supported on this host"; \
    } while (0)

    class SocketBasicTest : public ::testing::Test
    {
    };

    // Socket parameter tests
    TEST_F(SocketBasicTest, SocketParams_TCP_IPv4)
    {
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        EXPECT_EQ(params.af, AF_INET);
        EXPECT_EQ(params.type, SOCK_STREAM);
        EXPECT_EQ(params.proto, 0);
        EXPECT_STREQ(params.scheme(), "tcp");
    }

    TEST_F(SocketBasicTest, SocketParams_UDP_IPv4)
    {
        SocketParams params{AF_INET, SOCK_DGRAM, 0};
        EXPECT_EQ(params.type, SOCK_DGRAM);
        EXPECT_STREQ(params.scheme(), "udp");
    }

    TEST_F(SocketBasicTest, SocketParams_IPv6Schemes)
    {
        SocketParams tcp6{AF_INET6, SOCK_STREAM, 0};
        SocketParams udp6{AF_INET6, SOCK_DGRAM, 0};
        EXPECT_STREQ(tcp6.scheme(), "tcp");
        EXPECT_STREQ(udp6.scheme(), "udp");
    }

    TEST_F(SocketBasicTest, SocketParams_UnknownScheme)
    {
        SocketParams raw{AF_INET, SOCK_RAW, 0};
        EXPECT_STREQ(raw.scheme(), "unknown");
    }

#ifdef HAVE_UNIX_DOMAIN
    TEST_F(SocketBasicTest, SocketParams_UnixDomain)
    {
        SocketParams params{AF_UNIX, SOCK_STREAM, 0};
        EXPECT_STREQ(params.scheme(), "unix");
    }
#endif

    // Socket creation and lifecycle
    TEST_F(SocketBasicTest, CreateSocket_TCP)
    {
        Socket sock(SocketParams{AF_INET, SOCK_STREAM, 0});
        EXPECT_FALSE(sock.invalid());
        sock.close();
        EXPECT_TRUE(sock.invalid());
    }

    TEST_F(SocketBasicTest, CreateSocket_UDP)
    {
        Socket sock(SocketParams{AF_INET, SOCK_DGRAM, 0});
        EXPECT_FALSE(sock.invalid());
        sock.close();
        EXPECT_TRUE(sock.invalid());
    }

    TEST_F(SocketBasicTest, CreateSocket_ExplicitProtocols)
    {
        Socket tcp(SocketParams{AF_INET, SOCK_STREAM, IPPROTO_TCP});
        Socket udp(SocketParams{AF_INET, SOCK_DGRAM, IPPROTO_UDP});
        EXPECT_FALSE(tcp.invalid());
        EXPECT_FALSE(udp.invalid());
        tcp.close();
        udp.close();
    }

    TEST_F(SocketBasicTest, CreateSocket_IPv6)
    {
        SKIP_IF_NO_IPV6();
        Socket sock(SocketParams{AF_INET6, SOCK_STREAM, 0});
        EXPECT_FALSE(sock.invalid());
        sock.close();
    }

    TEST_F(SocketBasicTest, CreateSocket_InvalidFamilyThrows)
    {
        EXPECT_THROW(Socket(-1, SOCK_STREAM, 0), std::runtime_error);
    }

    TEST_F(SocketBasicTest, DefaultSocketIsInvalid)
    {
        Socket sock;
        EXPECT_TRUE(sock.invalid());
    }

    TEST_F(SocketBasicTest, DoubleCloseIsSafe)
    {
        Socket sock(SocketParams{AF_INET, SOCK_STREAM, 0});
        sock.close();
        EXPECT_NO_THROW(sock.close());
        EXPECT_TRUE(sock.invalid());
    }

    TEST_F(SocketBasicTest, MoveTransfersHandle)
    {
        Socket a(SocketParams{AF_INET, SOCK_STREAM, 0});
        auto handle = a.m_sock;
        Socket b(std::move(a));
        EXPECT_TRUE(a.invalid());
        EXPECT_EQ(b.m_sock, handle);

        Socket c;
        c = std::move(b);
        EXPECT_TRUE(b.invalid());
        EXPECT_EQ(c.m_sock, handle);
        c.close();
    }

    TEST_F(SocketBasicTest, ManySequentialSocketsDoNotLeak)
    {
        for (int i = 0; i < 256; ++i)
        {
            ScopedSocket sock(AF_INET, SOCK_STREAM, 0);
            ASSERT_FALSE(sock.get().invalid());
        }
    }

    // ScopedSocket RAII semantics
    TEST_F(SocketBasicTest, ScopedSocket_ReleaseTransfersOwnership)
    {
        Socket released;
        {
            ScopedSocket scoped(AF_INET, SOCK_STREAM, 0);
            released = scoped.release();
        }
        // The socket must still be usable after the ScopedSocket is gone.
        ASSERT_FALSE(released.invalid());
        EXPECT_TRUE(released.setReuseAddr());
        released.close();
    }

    TEST_F(SocketBasicTest, ScopedSocket_MoveAssignment)
    {
        ScopedSocket a(AF_INET, SOCK_STREAM, 0);
        ScopedSocket b(AF_INET, SOCK_STREAM, 0);
        auto handle = a.get().m_sock;
        b = std::move(a);
        EXPECT_EQ(b.get().m_sock, handle);
    }

    // Socket options
    TEST_F(SocketBasicTest, SocketOptions)
    {
        ScopedSocket sock(AF_INET, SOCK_STREAM, 0);
        EXPECT_TRUE(sock.get().setReuseAddr());
        EXPECT_TRUE(sock.get().setNoDelay());
        EXPECT_NO_THROW(sock.get().setNonBlocking());
    }

    // Bind / listen / connect / accept round-trip over loopback
    TEST_F(SocketBasicTest, TcpLoopbackRoundTrip)
    {
        ScopedSocket listener(AF_INET, SOCK_STREAM, 0);
        ASSERT_TRUE(listener.get().setReuseAddr());
        ASSERT_EQ(listener.get().bind(SocketAddr("127.0.0.1:0")), 0);
        ASSERT_TRUE(listener.get().listen(1));

        SocketAddr bound;
        ASSERT_TRUE(listener.get().getsockname(bound));
        ASSERT_GT(bound.port(), 0);

        ScopedSocket client(AF_INET, SOCK_STREAM, 0);
        ASSERT_TRUE(client.get().connect(bound));

        Socket accepted;
        SocketAddr peer;
        ASSERT_TRUE(listener.get().accept(accepted, peer));
        ScopedSocket server(std::move(accepted));
        EXPECT_EQ(peer.toString().rfind("127.0.0.1:", 0), 0u);

        const std::string msg = "ping";
        ASSERT_EQ(client.get().send(msg.data(), msg.size()), static_cast<int>(msg.size()));

        char buf[16] = {};
        int n = server.get().recv(buf, sizeof(buf));
        ASSERT_EQ(n, static_cast<int>(msg.size()));
        EXPECT_EQ(std::string(buf, n), msg);

        // Graceful shutdown is observed by the peer as EOF.
        EXPECT_TRUE(client.get().shutdown(Socket::ShutdownSend));
        EXPECT_EQ(server.get().recv(buf, sizeof(buf)), 0);
    }

    TEST_F(SocketBasicTest, UdpLoopbackRoundTrip)
    {
        ScopedSocket receiver(AF_INET, SOCK_DGRAM, 0);
        ASSERT_EQ(receiver.get().bind(SocketAddr("127.0.0.1:0")), 0);
        SocketAddr bound;
        ASSERT_TRUE(receiver.get().getsockname(bound));

        ScopedSocket sender(AF_INET, SOCK_DGRAM, 0);
        const std::string msg = "datagram";
        ASSERT_EQ(sender.get().sendto(msg.data(), msg.size(), 0, bound), static_cast<int>(msg.size()));

        char buf[32] = {};
        SocketAddr from;
        int n = receiver.get().recvfrom(buf, sizeof(buf), 0, from);
        ASSERT_EQ(n, static_cast<int>(msg.size()));
        EXPECT_EQ(std::string(buf, n), msg);
        EXPECT_EQ(from.toString().rfind("127.0.0.1:", 0), 0u);
    }

    TEST_F(SocketBasicTest, TcpLoopbackRoundTrip_IPv6)
    {
        SKIP_IF_NO_IPV6();
        ScopedSocket listener(AF_INET6, SOCK_STREAM, 0);
        if (listener.get().bind(SocketAddr("[::1]:0")) != 0)
            GTEST_SKIP() << "IPv6 loopback is not configured on this host";
        ASSERT_TRUE(listener.get().listen(1));

        SocketAddr bound;
        ASSERT_TRUE(listener.get().getsockname(bound));
        ASSERT_GT(bound.port(), 0);

        ScopedSocket client(AF_INET6, SOCK_STREAM, 0);
        ASSERT_TRUE(client.get().connect(bound));

        Socket accepted;
        SocketAddr peer;
        ASSERT_TRUE(listener.get().accept(accepted, peer));
        ScopedSocket server(std::move(accepted));
        EXPECT_EQ(peer.toString().rfind("[::1]:", 0), 0u);
    }

    TEST_F(SocketBasicTest, SendOnInvalidArgsReturnsZero)
    {
        ScopedSocket sock(AF_INET, SOCK_STREAM, 0);
        EXPECT_EQ(sock.get().send(nullptr, 10), 0);
        char c = 'x';
        EXPECT_EQ(sock.get().send(&c, 0), 0);
    }

    // Addresses
    TEST_F(SocketBasicTest, SpecialAddresses)
    {
        EXPECT_EQ(SocketAddr("127.0.0.1:1234").toString(), "127.0.0.1:1234");
        EXPECT_EQ(SocketAddr("[::1]:1234").toString(), "[::1]:1234");
        EXPECT_EQ(SocketAddr("0.0.0.0:1234").toString(), "0.0.0.0:1234");
        EXPECT_EQ(SocketAddr("[::]:1234").toString(), "[::]:1234");
        EXPECT_EQ(SocketAddr("255.255.255.255:9999").toString(), "255.255.255.255:9999");
    }

    TEST_F(SocketBasicTest, PortNumbers)
    {
        for (int port : {1, 80, 443, 8080, 49152, 65535})
        {
            std::string addr_str = "127.0.0.1:" + std::to_string(port);
            SocketAddr addr(addr_str.c_str());
            EXPECT_EQ(addr.toString(), addr_str);
            EXPECT_EQ(addr.port(), port);
        }
    }

}  // namespace
