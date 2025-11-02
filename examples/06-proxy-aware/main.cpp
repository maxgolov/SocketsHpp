// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/*
 * Proxy-Aware HTTP Server Example
 * 
 * Demonstrates how to use ProxyAwareHelpers to extract real client information
 * when the server is behind a reverse proxy (nginx, Apache, load balancer).
 * 
 * Common deployment scenario:
 *   [Client] -> [nginx/HAProxy] -> [This Server]
 * 
 * The proxy adds headers like X-Forwarded-For, X-Forwarded-Proto, etc.
 * to preserve original client information.
 */

#include <sockets.hpp>
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/server/proxy_aware.h>
#include <iostream>
#include <string>
#include <sstream>

using namespace SOCKETSHPP_NS;
using namespace SOCKETSHPP_NS::http::server;


int main()
{
    try
    {
        // Configure proxy trust settings
        // In production, only trust specific proxy IPs for security
        TrustProxyConfig config;
        
        // Option 1: Trust all proxies (INSECURE - only for testing)
        // config.setMode(TrustProxyConfig::TrustMode::TrustAll);
        
        // Option 2: Trust specific proxy IPs (RECOMMENDED)
        config.setMode(TrustProxyConfig::TrustMode::TrustSpecific);
        config.addTrustedProxy("127.0.0.1");     // localhost nginx
        config.addTrustedProxy("10.0.0.1");      // internal load balancer
        config.addTrustedProxy("172.16.0.1");    // reverse proxy
        
        std::cout << "Proxy-Aware HTTP Server\n";
        std::cout << "======================\n";
        std::cout << "Trust mode: Specific IPs\n";
        std::cout << "Listening on http://localhost:8080\n\n";
        
        // Create HTTP server
        HttpServer server("localhost", 8080);
        
        server.route("/", [&config](const HttpRequest& req, HttpResponse& res) -> int
        {
            // Get real client IP using ProxyAwareHelpers
            std::string clientIP = ProxyAwareHelpers::getClientIP(req.headers, req.client, config);
            std::string protocol = ProxyAwareHelpers::getProtocol(req.headers, req.client, config);
            std::string host = ProxyAwareHelpers::getHost(req.headers, req.client, config, "localhost:8080");
            bool isSecure = ProxyAwareHelpers::isSecure(req.headers, req.client, config);
            
            // Build response showing extracted information
            std::ostringstream responseBody;
            responseBody << "<!DOCTYPE html>\n";
            responseBody << "<html>\n";
            responseBody << "<head><title>Proxy-Aware Server</title></head>\n";
            responseBody << "<body>\n";
            responseBody << "<h1>Proxy-Aware Server Information</h1>\n";
            responseBody << "<h2>Real Client Details</h2>\n";
            responseBody << "<ul>\n";
            responseBody << "<li><strong>Protocol:</strong> " << protocol << "</li>\n";
            responseBody << "<li><strong>Client IP:</strong> " << clientIP << "</li>\n";
            responseBody << "<li><strong>Host:</strong> " << host << "</li>\n";
            responseBody << "<li><strong>Secure:</strong> " << (isSecure ? "Yes (HTTPS)" : "No (HTTP)") << "</li>\n";
            responseBody << "<li><strong>Direct Connection IP:</strong> " << req.client << "</li>\n";
            responseBody << "</ul>\n";
            
            responseBody << "<h2>Received Headers</h2>\n";
            responseBody << "<ul>\n";
            for (const auto& [name, value] : req.headers)
            {
                responseBody << "<li><strong>" << name << ":</strong> " << value << "</li>\n";
            }
            responseBody << "</ul>\n";
            
            responseBody << "<h2>How to Test</h2>\n";
            responseBody << "<p>Configure nginx as a reverse proxy:</p>\n";
            responseBody << "<pre>\n";
            responseBody << "location / {\n";
            responseBody << "    proxy_pass http://localhost:8080;\n";
            responseBody << "    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n";
            responseBody << "    proxy_set_header X-Forwarded-Proto $scheme;\n";
            responseBody << "    proxy_set_header X-Forwarded-Host $host;\n";
            responseBody << "    proxy_set_header X-Real-IP $remote_addr;\n";
            responseBody << "}\n";
            responseBody << "</pre>\n";
            
            responseBody << "<p>Or manually send headers with curl:</p>\n";
            responseBody << "<pre>\n";
            responseBody << "curl -H \"X-Forwarded-For: 203.0.113.42\" \\\n";
            responseBody << "     -H \"X-Forwarded-Proto: https\" \\\n";
            responseBody << "     -H \"X-Forwarded-Host: example.com\" \\\n";
            responseBody << "     http://localhost:8080/\n";
            responseBody << "</pre>\n";
            
            responseBody << "</body>\n";
            responseBody << "</html>\n";
            
            res.set_header("Content-Type", "text/html; charset=utf-8");
            res.set_content(responseBody.str());
            
            // Log to console
            std::cout << "Request from " << clientIP << " (" << protocol << "://" << host << req.uri << ")\n";
            std::cout << "  Secure: " << (isSecure ? "Yes" : "No") << "\n";
            std::cout << "  Direct connection: " << req.client << "\n\n";
            
            return 0;
        });
        
        std::cout << "Test with:\n";
        std::cout << "  curl http://localhost:8080/\n";
        std::cout << "  curl -H \"X-Forwarded-For: 203.0.113.42\" http://localhost:8080/\n\n";
        
        server.start();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}


