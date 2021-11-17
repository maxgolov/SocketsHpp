// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
// Uncomment this line for additional debugging:
#define HAVE_CONSOLE_LOG

#include <cstdio>
#include <cstdlib>

#include "sockets.hpp"

using namespace SOCKETSHPP_NS::net::common;
using namespace std;

/// <summary>
/// Simple example that shows how to send large buffer over TCP.
/// TCP socket server must be running at 127.0.0.1:40000.
/// You can use netcat to receive the buffer as follows:
/// # nc -L -p 40000 > capture.bin
/// </summary>
/// <param name="argc">Unused</param>
/// <param name="argv">Unused</param>
/// <returns></returns>
int main(int argc, const char* argv[])
{
    SocketParams params{ AF_INET, SOCK_STREAM, 0 };
    Socket client(params);
    client.connect(SocketAddr{ "127.0.0.1:40000" });
    // Create large buffer with repeated sequence
    std::vector<uint8_t> buffer;
    buffer.reserve(1024 * 1024);
    for (int i = 0; i < 1024 * 1024; i++)
    {
        buffer.push_back(static_cast<uint8_t>(i));
    }

    // Send 1024K buffer. Print how many bytes we sent.
    // If connection times out, the count will be -1.
    auto total_bytes_sent = client.send(buffer.data(), buffer.size());
    printf("total_bytes_sent=%d\n", total_bytes_sent);

    // Close connection
    client.close();
    return 0;
}
