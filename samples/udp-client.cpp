// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
// Uncomment this line for additional debugging:
#define HAVE_CONSOLE_LOG

#include <cstdio>
#include <cstdlib>

#include "sockets.hpp"

using namespace SOCKETSHPP_NS::net::common;
using namespace std;

int main(int argc, const char* argv[])
{
    SocketParams params{ AF_INET, SOCK_DGRAM, 0 };
    SocketAddr destination("127.0.0.1:40000");
    Socket client(params);
    client.connect(destination);

    string hello = "Hello!";
    auto total_bytes_sent = client.send(hello.c_str(), hello.length());

    client.close();
    return 0;
}
