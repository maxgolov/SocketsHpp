// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <sockets.hpp>
#include <SocketsHpp/http/server/http_server.h>
#include <iostream>

using namespace SOCKETSHPP_NS;
using namespace SOCKETSHPP_NS::http::server;

int main()
{
    try
    {
        std::cout << "HTTP Server with Compression Example (Simplified)\n";
        std::cout << "==================================================\n";
        std::cout << "NOTE: Compression middleware integration with HttpServer\n";
        std::cout << "is planned for future releases. This example shows basic HTTP.\n\n";
        std::cout << "Listening on http://localhost:8080\n\n";
        
        HttpServer server("localhost", 8080);
        
        server.route("/", [](const HttpRequest& req, HttpResponse& res) -> int
        {
            std::ostringstream html;
            html << "<!DOCTYPE html>\n<html>\n<head><title>Compression Demo</title></head>\n<body>\n";
            html << "<h1>HTTP Compression Demonstration</h1>\n";
            html << "<p>Compression middleware integration coming soon!</p>\n";
            html << "<h2>Large Response Test</h2>\n";
            
            for (int i = 0; i < 50; i++)
            {
                html << "<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit. " << i << "</p>\n";
            }
            
            html << "</body>\n</html>\n";
            
            res.set_header("Content-Type", "text/html");
            res.set_content(html.str());
            return 0;
        });
        
        std::cout << "Test with:\n";
        std::cout << "  curl http://localhost:8080/\n\n";
        
        server.start();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
