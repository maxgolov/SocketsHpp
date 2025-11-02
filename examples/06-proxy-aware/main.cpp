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

using namespace SOCKETSHPP_NS;
using namespace SOCKETSHPP_NS::net::common;

// Simple HTTP request structure
struct HttpRequest
{
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    
    bool has_header(const std::string& name) const
    {
        auto it = headers.find(name);
        if (it != headers.end()) return true;
        
        // Try lowercase
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (const auto& [key, value] : headers)
        {
            std::string keyLower = key;
            std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
            if (keyLower == lower) return true;
        }
        return false;
    }
    
    std::string get_header_value(const std::string& name) const
    {
        auto it = headers.find(name);
        if (it != headers.end()) return it->second;
        
        // Try lowercase
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (const auto& [key, value] : headers)
        {
            std::string keyLower = key;
            std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
            if (keyLower == lower) return value;
        }
        return "";
    }
};

// Simple request parser (minimal example)
HttpRequest parseRequest(const std::string& requestData)
{
    HttpRequest req;
    
    std::istringstream stream(requestData);
    std::string line;
    
    // Parse request line
    if (std::getline(stream, line))
    {
        std::istringstream lineStream(line);
        lineStream >> req.method >> req.path;
    }
    
    // Parse headers
    while (std::getline(stream, line) && !line.empty() && line != "\r")
    {
        if (line.back() == '\r') line.pop_back();
        
        auto colonPos = line.find(':');
        if (colonPos != std::string::npos)
        {
            std::string name = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            
            // Trim whitespace
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            req.headers[name] = value;
        }
    }
    
    return req;
}

int main()
{
    try
    {
        // Configure proxy trust settings
        // In production, only trust specific proxy IPs for security
        SOCKETSHPP_NS::http::server::TrustProxyConfig config;
        
        // Option 1: Trust all proxies (INSECURE - only for testing)
        // config.setMode(SOCKETSHPP_NS::http::server::TrustProxyConfig::TrustMode::TrustAll);
        
        // Option 2: Trust specific proxy IPs (RECOMMENDED)
        config.setMode(SOCKETSHPP_NS::http::server::TrustProxyConfig::TrustMode::TrustSpecific);
        config.addTrustedProxy("127.0.0.1");     // localhost nginx
        config.addTrustedProxy("10.0.0.1");      // internal load balancer
        config.addTrustedProxy("172.16.0.1");    // reverse proxy
        
        std::cout << "Proxy-Aware HTTP Server\n";
        std::cout << "======================\n";
        std::cout << "Trust mode: Specific IPs\n";
        std::cout << "Listening on http://localhost:8080\n\n";
        
        // Create TCP server
        SocketServer<> server(8080);
        
        server.onConnect([&config](SocketConnection<>& connection)
        {
            std::cout << "Client connected from: " << connection.remoteAddress() << "\n";
            
            connection.onMessage([&config, &connection](const std::string& message)
            {
                // Parse HTTP request
                HttpRequest req = parseRequest(message);
                
                std::string remoteAddr = connection.remoteAddress();
                
                // Extract real client information using ProxyAwareHelpers
                std::string protocol = SOCKETSHPP_NS::http::server::ProxyAwareHelpers::getProtocol(
                    req.headers, remoteAddr, config);
                std::string clientIP = SOCKETSHPP_NS::http::server::ProxyAwareHelpers::getClientIP(
                    req.headers, remoteAddr, config);
                std::string host = SOCKETSHPP_NS::http::server::ProxyAwareHelpers::getHost(
                    req.headers, config);
                bool isSecure = SOCKETSHPP_NS::http::server::ProxyAwareHelpers::isSecure(
                    req.headers, remoteAddr, config);
                
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
                responseBody << "<li><strong>Direct Connection IP:</strong> " << connection.remoteAddress() << "</li>\n";
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
                
                std::string body = responseBody.str();
                
                // Send HTTP response
                std::ostringstream response;
                response << "HTTP/1.1 200 OK\r\n";
                response << "Content-Type: text/html; charset=utf-8\r\n";
                response << "Content-Length: " << body.length() << "\r\n";
                response << "Connection: close\r\n";
                response << "\r\n";
                response << body;
                
                connection.send(response.str());
                connection.close();
                
                // Log to console
                std::cout << "Request from " << clientIP << " (" << protocol << "://" << host << req.path << ")\n";
                std::cout << "  Secure: " << (isSecure ? "Yes" : "No") << "\n";
                std::cout << "  Direct connection: " << connection.remoteAddress() << "\n\n";
            });
            
            connection.onDisconnect([]()
            {
                std::cout << "Client disconnected\n";
            });
        });
        
        std::cout << "Test with:\n";
        std::cout << "  curl http://localhost:8080/\n";
        std::cout << "  curl -H \"X-Forwarded-For: 203.0.113.42\" http://localhost:8080/\n\n";
        
        server.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

