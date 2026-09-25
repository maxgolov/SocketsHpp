// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// Functional tests for net::common::SocketServer lifecycle, TCP/UDP echo on
// ephemeral ports, peer-reset handling and stream read classification.

#include <gtest/gtest.h>

#include "sockets.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using namespace SOCKETSHPP_NS::net::common;

namespace
{
    using Clock = std::chrono::steady_clock;

    /// Poll `predicate` until it returns true or `timeout` expires.
    bool waitFor(const std::function<bool()>& predicate,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
    {
        auto deadline = Clock::now() + timeout;
        while (Clock::now() < deadline)
        {
            if (predicate())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return predicate();
    }

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

    void installEcho(SocketServer& server)
    {
        server.onRequest = [](SocketServer::Connection& conn) {
            conn.response_buffer.clear();
            conn.request_buffer.swap(conn.response_buffer);
            conn.state.insert(SocketServer::Connection::Responding);
        };
    }

    size_t connectionCount(SocketServer& server)
    {
        std::lock_guard<std::recursive_mutex> lock(server.connections_mutex);
        return server.connections.size();
    }

    /// Receive exactly `size` bytes (or fewer on EOF / timeout).
    std::string recvExactly(Socket& sock, size_t size)
    {
        std::string out;
        char buf[4096];
        while (out.size() < size)
        {
            size_t want = std::min(sizeof(buf), size - out.size());
            int n = sock.recv(buf, want);
            if (n <= 0)
                break;
            out.append(buf, static_cast<size_t>(n));
        }
        return out;
    }

    // ------------------------------------------------------------------ TCP

    TEST(SocketServerTest, TcpEchoOnEphemeralPort)
    {
        SocketServer server(SocketAddr("127.0.0.1:0"), SocketParams{AF_INET, SOCK_STREAM, 0});
        ASSERT_TRUE(server.is_bound);
        ASSERT_GT(server.address().port(), 0);
        installEcho(server);
        server.Start();

        for (int i = 0; i < 4; i++)
        {
            Socket client(server.server_socket_params);
            setRecvTimeout(client, 5000);
            ASSERT_TRUE(client.connect(server.address()));
            const std::string msg = "Hello #" + std::to_string(i);
            ASSERT_EQ(client.send(msg.data(), msg.size()), static_cast<int>(msg.size()));
            EXPECT_EQ(recvExactly(client, msg.size()), msg);
            client.close();
        }
        server.Stop();
    }

    TEST(SocketServerTest, TcpClientDisconnectRemovesConnection)
    {
        SocketServer server(SocketAddr("127.0.0.1:0"), SocketParams{AF_INET, SOCK_STREAM, 0});
        ASSERT_TRUE(server.is_bound);
        installEcho(server);
        server.Start();

        Socket client(server.server_socket_params);
        setRecvTimeout(client, 5000);
        ASSERT_TRUE(client.connect(server.address()));
        const std::string msg = "ping";
        ASSERT_EQ(client.send(msg.data(), msg.size()), static_cast<int>(msg.size()));
        ASSERT_EQ(recvExactly(client, msg.size()), msg);
        EXPECT_EQ(connectionCount(server), 1u);

        client.close();
        EXPECT_TRUE(waitFor([&] { return connectionCount(server) == 0; }));
        server.Stop();
    }

    TEST(SocketServerTest, StopClosesAcceptedClients)
    {
        SocketServer server(SocketAddr("127.0.0.1:0"), SocketParams{AF_INET, SOCK_STREAM, 0});
        ASSERT_TRUE(server.is_bound);
        installEcho(server);
        server.Start();

        Socket client(server.server_socket_params);
        setRecvTimeout(client, 5000);
        ASSERT_TRUE(client.connect(server.address()));
        const std::string msg = "ping";
        ASSERT_EQ(client.send(msg.data(), msg.size()), static_cast<int>(msg.size()));
        ASSERT_EQ(recvExactly(client, msg.size()), msg);

        server.Stop();
        EXPECT_EQ(connectionCount(server), 0u);
        EXPECT_TRUE(server.server_socket.invalid());

        // The server closed its end: the client observes EOF (or a reset),
        // not a receive timeout.
        char c;
        int n = client.recv(&c, 1);
        EXPECT_LE(n, 0);
        if (n < 0)
        {
            EXPECT_FALSE(Socket::isWouldBlock(client.error())) << "server did not close client fd";
        }
        client.close();

        // Stop() is idempotent.
        server.Stop();
    }

    TEST(SocketServerTest, TcpDestroyStartedServerWithoutStop)
    {
        SocketAddr address;
        {
            auto server = std::make_unique<SocketServer>(
                SocketAddr("127.0.0.1:0"), SocketParams{AF_INET, SOCK_STREAM, 0});
            ASSERT_TRUE(server->is_bound);
            installEcho(*server);
            server->Start();
            address = server->address();

            Socket client(server->server_socket_params);
            setRecvTimeout(client, 5000);
            ASSERT_TRUE(client.connect(address));
            const std::string msg = "ping";
            ASSERT_EQ(client.send(msg.data(), msg.size()), static_cast<int>(msg.size()));
            ASSERT_EQ(recvExactly(client, msg.size()), msg);
            client.close();
            // Destroyed here without Stop(): must not std::terminate.
        }
        // The listener is closed: connecting now fails.
        Socket probe(AF_INET, SOCK_STREAM, 0);
        EXPECT_FALSE(probe.connect(address));
        probe.close();
    }

    TEST(SocketServerTest, DestroyUnstartedServer)
    {
        SocketServer server(SocketAddr("127.0.0.1:0"), SocketParams{AF_INET, SOCK_STREAM, 0});
        EXPECT_TRUE(server.is_bound);
    }

    // A client that resets the connection while the server is still sending a
    // large response must cause the server to drop the connection instead of
    // re-arming Writable forever.
    TEST(SocketServerTest, TcpPeerResetDuringResponseClosesConnection)
    {
        SocketServer server(SocketAddr("127.0.0.1:0"), SocketParams{AF_INET, SOCK_STREAM, 0});
        ASSERT_TRUE(server.is_bound);
        const std::string bigResponse(16 * 1024 * 1024, 'z');
        server.onRequest = [&](SocketServer::Connection& conn) {
            conn.response_buffer = bigResponse;
            conn.state.insert(SocketServer::Connection::Responding);
        };
        server.Start();

        Socket client(server.server_socket_params);
        setRecvTimeout(client, 5000);
        ASSERT_TRUE(client.connect(server.address()));
        const std::string msg = "GET";
        ASSERT_EQ(client.send(msg.data(), msg.size()), static_cast<int>(msg.size()));
        // Read some of the response so we know the server is mid-response.
        ASSERT_EQ(recvExactly(client, 1024).size(), 1024u);
        ASSERT_EQ(connectionCount(server), 1u);

        struct linger lg;
        lg.l_onoff = 1;
        lg.l_linger = 0;
        ::setsockopt(client.m_sock, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lg), sizeof(lg));
        client.close();  // sends RST

        EXPECT_TRUE(waitFor([&] { return connectionCount(server) == 0; }))
            << "connection was not closed after peer reset";
        server.Stop();
    }

    // ReadStreamBuffer must only close on EOF or a hard error, not when a
    // non-blocking read has nothing to return yet.
    TEST(SocketServerTest, ReadStreamBufferClassifiesReads)
    {
        // Not started: used only to call ReadStreamBuffer directly.
        SocketServer server(SocketAddr("127.0.0.1:0"), SocketParams{AF_INET, SOCK_STREAM, 0});
        ASSERT_TRUE(server.is_bound);

        Socket client(AF_INET, SOCK_STREAM, 0);
        ASSERT_TRUE(client.connect(server.address()));
        Socket accepted;
        SocketAddr peer;
        ASSERT_TRUE(server.server_socket.accept(accepted, peer));
        accepted.setNonBlocking();

        SocketServer::Connection conn;
        conn.socket = accepted;
        conn.state = {SocketServer::Connection::Idle};

        // Nothing sent yet: would-block, connection stays open.
        server.ReadStreamBuffer(conn);
        EXPECT_TRUE(conn.request_buffer.empty());
        EXPECT_EQ(conn.state.count(SocketServer::Connection::Closing), 0u);
        EXPECT_EQ(conn.state.count(SocketServer::Connection::Receiving), 0u);

        // Data available.
        const std::string msg = "hello";
        ASSERT_EQ(client.send(msg.data(), msg.size()), static_cast<int>(msg.size()));
        ASSERT_TRUE(waitFor([&] {
            server.ReadStreamBuffer(conn);
            return !conn.request_buffer.empty();
        }));
        EXPECT_EQ(conn.request_buffer, msg);
        EXPECT_EQ(conn.state.count(SocketServer::Connection::Receiving), 1u);
        EXPECT_EQ(conn.state.count(SocketServer::Connection::Closing), 0u);

        // Peer closed: EOF -> Closing.
        client.close();
        EXPECT_TRUE(waitFor([&] {
            server.ReadStreamBuffer(conn);
            return conn.state.count(SocketServer::Connection::Closing) == 1;
        }));
        accepted.close();
    }

    // ------------------------------------------------------------------ UDP

    TEST(SocketServerTest, UdpEchoOnEphemeralPort)
    {
        SocketParams params{AF_INET, SOCK_DGRAM, 0};
        SocketServer server(SocketAddr("127.0.0.1:0"), params);
        ASSERT_TRUE(server.is_bound);
        ASSERT_GT(server.address().port(), 0);
        installEcho(server);
        server.Start();

        Socket client(params);
        setRecvTimeout(client, 5000);
        ASSERT_TRUE(client.connect(server.address()));
        for (int i = 0; i < 20; i++)
        {
            const std::string msg = "datagram #" + std::to_string(i);
            ASSERT_EQ(client.send(msg.data(), msg.size()), static_cast<int>(msg.size()));
            char buf[256];
            int n = client.recv(buf, sizeof(buf));
            ASSERT_EQ(n, static_cast<int>(msg.size()));
            EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), msg);
        }
        client.close();

        // Datagram "connections" are not accumulated.
        EXPECT_EQ(connectionCount(server), 0u);

        auto started = Clock::now();
        server.Stop();
        EXPECT_LT(Clock::now() - started, std::chrono::seconds(3));
        EXPECT_TRUE(server.server_socket.invalid());
        server.Stop();  // idempotent
    }

    // A handler that marks a datagram "connection" as Closing must not close
    // the server socket.
    TEST(SocketServerTest, UdpClosingRequestDoesNotCloseServerSocket)
    {
        SocketParams params{AF_INET, SOCK_DGRAM, 0};
        SocketServer server(SocketAddr("127.0.0.1:0"), params);
        ASSERT_TRUE(server.is_bound);
        server.onRequest = [](SocketServer::Connection& conn) {
            conn.response_buffer.clear();
            conn.request_buffer.swap(conn.response_buffer);
            conn.state.insert(SocketServer::Connection::Responding);
            conn.state.insert(SocketServer::Connection::Closing);
        };
        server.Start();

        Socket client(params);
        setRecvTimeout(client, 5000);
        ASSERT_TRUE(client.connect(server.address()));
        for (int i = 0; i < 3; i++)
        {
            const std::string msg = "x" + std::to_string(i);
            ASSERT_EQ(client.send(msg.data(), msg.size()), static_cast<int>(msg.size()));
            char buf[64];
            int n = client.recv(buf, sizeof(buf));
            ASSERT_EQ(n, static_cast<int>(msg.size())) << "server socket stopped answering";
        }
        client.close();
        server.Stop();
    }

    TEST(SocketServerTest, UdpDestroyStartedServerWithoutStop)
    {
        SocketParams params{AF_INET, SOCK_DGRAM, 0};
        auto server = std::make_unique<SocketServer>(SocketAddr("127.0.0.1:0"), params);
        ASSERT_TRUE(server->is_bound);
        installEcho(*server);
        server->Start();
        server.reset();  // must not std::terminate
        SUCCEED();
    }

#ifndef _WIN32
    // An idle UDP server must block in poll() rather than spin on a
    // non-blocking recvfrom(). clock() measures process CPU time on POSIX.
    TEST(SocketServerTest, UdpIdleServerDoesNotSpin)
    {
        SocketParams params{AF_INET, SOCK_DGRAM, 0};
        SocketServer server(SocketAddr("127.0.0.1:0"), params);
        ASSERT_TRUE(server.is_bound);
        server.Start();

        std::clock_t cpuStart = std::clock();
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        double cpuSeconds = static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
        server.Stop();
        // A spinning receive loop burns ~0.6 s of CPU here.
        EXPECT_LT(cpuSeconds, 0.2);
    }
#endif

}  // namespace
