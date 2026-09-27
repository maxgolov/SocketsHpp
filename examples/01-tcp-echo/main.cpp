// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief TCP echo round trip in one process.
///
/// Starts the library's TcpServer (which echoes what it receives when no message
/// handler is set) on an ephemeral 127.0.0.1 port, connects a client Socket, sends
/// 1 MiB, reads the echo back and checks that it matches.

#include <SocketsHpp/net/tcp/tcp.h>

#include <iostream>
#include <string>
#include <thread>

using SOCKETSHPP_NS::net::common::Socket;
using SOCKETSHPP_NS::net::common::SocketParams;
using SOCKETSHPP_NS::net::tcp::TcpServer;

int main()
{
    try
    {
        // Port 0 picks a free port; the server is listening once the constructor returns.
        TcpServer server(0, "127.0.0.1");
        server.Start();
        std::cout << "Echo server listening on " << server.address().toString() << std::endl;

        Socket client(SocketParams{AF_INET, SOCK_STREAM, 0});
        if (!client.connect(server.address()))
        {
            std::cerr << "connect() failed, error " << client.error() << std::endl;
            client.close();
            return 1;
        }

        std::string payload(1024 * 1024, '\0');
        for (size_t i = 0; i < payload.size(); ++i)
        {
            payload[i] = static_cast<char>(i % 251);
        }

        // Send on a second thread while this one reads the echo. Sending everything
        // before reading could deadlock: once both sides' socket buffers are full, the
        // server waits for us to read and we wait for the server to read.
        size_t sent = 0;
        int sendError = 0;
        std::thread writer([&] { sent = client.writeall(payload, &sendError); });

        std::string received;
        char buffer[16 * 1024];
        while (received.size() < payload.size())
        {
            int n = client.recv(buffer, sizeof(buffer));
            if (n <= 0)
            {
                break;  // 0: server closed the connection, -1: error
            }
            received.append(buffer, static_cast<size_t>(n));
        }
        if (received.size() < payload.size())
        {
            client.shutdown(Socket::ShutdownBoth);  // unblock the writer if it is stuck
        }
        writer.join();
        client.close();
        server.Stop();

        std::cout << "Sent " << sent << " bytes, received " << received.size() << " bytes" << std::endl;
        if (sendError != 0 || sent != payload.size() || received != payload)
        {
            std::cerr << "FAILED: echo does not match (send error " << sendError << ")" << std::endl;
            return 1;
        }
        std::cout << "OK: echo matches" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
