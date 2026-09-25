// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// Functional tests for the net::tcp::TcpServer convenience wrapper.

#include <gtest/gtest.h>

#include <SocketsHpp/net/tcp/tcp.h>

#include <string>

using SOCKETSHPP_NS::net::tcp::TcpServer;
using SOCKETSHPP_NS::net::common::Socket;

namespace
{
    void setRecvTimeout(Socket& sock, int ms)
    {
#ifdef _WIN32
        DWORD tv = static_cast<DWORD>(ms);
        ::setsockopt(sock.m_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        ::setsockopt(sock.m_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    }

    std::string roundTrip(TcpServer& server, const std::string& msg, size_t expected)
    {
        Socket client(AF_INET, SOCK_STREAM, 0);
        setRecvTimeout(client, 5000);
        std::string out;
        if (client.connect(server.address()) &&
            client.send(msg.data(), msg.size()) == static_cast<int>(msg.size()))
        {
            char buf[1024];
            while (out.size() < expected)
            {
                int n = client.recv(buf, sizeof(buf));
                if (n <= 0)
                    break;
                out.append(buf, static_cast<size_t>(n));
            }
        }
        client.close();
        return out;
    }

    TEST(TcpServerTest, DefaultEchoOnEphemeralPort)
    {
        TcpServer server;
        ASSERT_GT(server.port(), 0);
        server.Start();
        EXPECT_EQ(roundTrip(server, "hello", 5), "hello");
        server.Stop();
    }

    TEST(TcpServerTest, MessageHandlerResponse)
    {
        TcpServer server(0, "127.0.0.1");
        server.onMessage([](const std::string& msg, TcpServer::Connection&) { return "echo: " + msg; });
        server.Start();
        EXPECT_EQ(roundTrip(server, "abc", 9), "echo: abc");
        server.Stop();
    }

    TEST(TcpServerTest, HandlerCanCloseConnection)
    {
        TcpServer server;
        server.onMessage([](const std::string& msg, TcpServer::Connection& conn) {
            conn.state.insert(TcpServer::Connection::Closing);
            return "bye " + msg;
        });
        server.Start();
        // Read until EOF: the server closes after responding.
        EXPECT_EQ(roundTrip(server, "x", 1000), "bye x");
        server.Stop();
    }

    TEST(TcpServerTest, BindFailureThrows)
    {
        TcpServer first;
        EXPECT_THROW(TcpServer second(first.port()), std::runtime_error);
    }

    TEST(TcpServerTest, InvalidHostThrows)
    {
        // Malformed IPv6 literal: rejected without a DNS lookup.
        EXPECT_THROW(TcpServer server(0, "[not-ipv6]"), std::invalid_argument);
    }
}  // namespace
