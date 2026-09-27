// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief UDP echo round trip in one process.
///
/// Starts a SocketServer bound to a UDP socket on an ephemeral 127.0.0.1 port whose
/// onRequest handler sends every datagram back, then sends a few datagrams from a
/// client Socket and checks each reply.

#include <SocketsHpp/net/common/socket_server.h>

#include <iostream>
#include <string>

using SOCKETSHPP_NS::net::common::Socket;
using SOCKETSHPP_NS::net::common::SocketAddr;
using SOCKETSHPP_NS::net::common::SocketParams;
using SOCKETSHPP_NS::net::common::SocketServer;

// UDP gives no delivery guarantee: never wait for a reply forever.
static void setRecvTimeout(Socket& sock, int ms)
{
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
#else
    timeval tv{ms / 1000, (ms % 1000) * 1000};
#endif
    ::setsockopt(sock.m_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
}

int main()
{
    try
    {
        const SocketParams udp{AF_INET, SOCK_DGRAM, 0};

        // Port 0 picks a free port; address() reports the one actually bound.
        SocketServer server(SocketAddr("127.0.0.1:0"), udp);
        if (!server.is_bound)
        {
            std::cerr << "bind failed" << std::endl;
            return 1;
        }
        // Called on the reactor thread for each datagram: reply with the same bytes.
        server.onRequest = [](SocketServer::Connection& conn) {
            conn.response_buffer = conn.request_buffer;
            conn.state.insert(SocketServer::Connection::Responding);
        };
        server.Start();
        std::cout << "Echo server listening on udp://" << server.address().toString() << std::endl;

        Socket client(udp);
        setRecvTimeout(client, 2000);
        // On a UDP socket connect() only sets the default peer for send()/recv().
        if (!client.connect(server.address()))
        {
            std::cerr << "connect() failed, error " << client.error() << std::endl;
            client.close();
            return 1;
        }

        int failures = 0;
        for (const std::string message : {"Hello", "from the", "SocketsHpp UDP client!"})
        {
            char reply[1500];
            int n = -1;
            if (client.send(message.data(), message.size()) == static_cast<int>(message.size()))
            {
                n = client.recv(reply, sizeof(reply));
            }
            if (n >= 0 && std::string(reply, static_cast<size_t>(n)) == message)
            {
                std::cout << "echoed: " << message << std::endl;
            }
            else
            {
                std::cerr << "no matching reply for \"" << message << "\" (error " << client.error() << ")"
                          << std::endl;
                ++failures;
            }
        }

        client.close();
        server.Stop();
        std::cout << (failures == 0 ? "OK: all datagrams echoed" : "FAILED") << std::endl;
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
