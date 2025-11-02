// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Simple TCP client example - sends data to a TCP server
///
/// This example demonstrates:
/// - Creating a TCP socket
/// - Connecting to a server
/// - Sending data
///
/// To test, run a TCP server first:
///   netcat: nc -l -p 40000
///   or use the tcp-server example from this library

#include <sockets.hpp>
#include <iostream>
#include <vector>

using namespace SOCKETSHPP_NS::net::common;

int main()
{
    try
    {
        // Create TCP socket
        SocketParams params{AF_INET, SOCK_STREAM, 0};
        Socket client(params);

        // Connect to server at localhost:40000
        std::cout << "Connecting to 127.0.0.1:40000..." << std::endl;
        client.connect(SocketAddr{"127.0.0.1:40000"});
        std::cout << "Connected!" << std::endl;

        // Create sample data (1MB buffer with repeated pattern)
        std::vector<uint8_t> buffer;
        buffer.reserve(1024 * 1024);
        for (int i = 0; i < 1024 * 1024; i++)
        {
            buffer.push_back(static_cast<uint8_t>(i % 256));
        }

        // Send data
        std::cout << "Sending " << buffer.size() << " bytes..." << std::endl;
        auto bytes_sent = client.send(buffer.data(), buffer.size());

        if (bytes_sent > 0)
        {
            std::cout << "Successfully sent " << bytes_sent << " bytes" << std::endl;
        }
        else
        {
            std::cerr << "Failed to send data" << std::endl;
            return 1;
        }

        // Clean shutdown
        client.close();
        std::cout << "Connection closed" << std::endl;

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
