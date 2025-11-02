// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Simple UDP client example - sends a datagram to a UDP server
///
/// This example demonstrates:
/// - Creating a UDP socket
/// - Sending a datagram
///
/// To test, run a UDP server first:
///   netcat: nc -u -l -p 40000

#include <sockets.hpp>
#include <iostream>
#include <string>

using namespace SOCKETSHPP_NS::net::common;

int main()
{
    try
    {
        // Create UDP socket
        SocketParams params{AF_INET, SOCK_DGRAM, 0};
        Socket client(params);

        // Set destination address
        SocketAddr destination("127.0.0.1:40000");
        std::cout << "Sending to " << destination.toString() << std::endl;

        // Connect sets the default destination for send()
        client.connect(destination);

        // Send message
        std::string message = "Hello from SocketsHpp UDP client!";
        auto bytes_sent = client.send(message.c_str(), message.length());

        if (bytes_sent > 0)
        {
            std::cout << "Sent " << bytes_sent << " bytes: " << message << std::endl;
        }
        else
        {
            std::cerr << "Failed to send datagram" << std::endl;
            return 1;
        }

        // Clean up
        client.close();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
