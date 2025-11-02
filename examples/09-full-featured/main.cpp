// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/*
 * Full-Featured HTTP Server Example
 * 
 * Demonstrates all enterprise features working together:
 * - Proxy awareness (X-Forwarded headers)
 * - Authentication (Bearer tokens, API keys, Basic auth)
 * - Compression (automatic content negotiation)
 * 
 * This shows a production-ready server configuration.
 */

#include <sockets.hpp>
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/server/proxy_aware.h>
#include <SocketsHpp/http/server/authentication.h>
#include <SocketsHpp/http/server/compression.h>
#include <SocketsHpp/http/server/compression_simple.h>
#ifdef _WIN32
#include <SocketsHpp/http/server/compression_windows.h>
#endif
#include <iostream>
#include <string>
#include <map>

using namespace SOCKETSHPP_NS;
using namespace SOCKETSHPP_NS::http::server;
using namespace SOCKETSHPP_NS::http::server::compression;

// HTTP request/response structures
struct HttpRequest
{
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string remoteAddr;
    
    bool has_header(const std::string& name) const
    {
        auto it = headers.find(name);
        if (it != headers.end()) return true;
        
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

struct HttpResponse
{
    int status = 200;
    std::string statusText = "OK";
    std::map<std::string, std::string> headers;
    std::string body;
    
    void set_header(const std::string& name, const std::string& value)
    {
        headers[name] = value;
    }
    
    void set_content(const std::string& content)
    {
        body = content;
    }
    
    std::string toString() const
    {
        std::ostringstream response;
        response << "HTTP/1.1 " << status << " " << statusText << "\r\n";
        
        for (const auto& [name, value] : headers)
        {
            response << name << ": " << value << "\r\n";
        }
        
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << body;
        
        return response.str();
    }
};

// Mock databases
std::map<std::string, std::string> validTokens = {
    {"secret_token_123", "user1"},
    {"admin_token_456", "admin"},
};

std::map<std::string, std::string> validApiKeys = {
    {"api_key_abc", "service1"},
};

std::map<std::string, std::string> validUsers = {
    {"alice", "password123"},
};

// Auth callbacks
AuthResult validateBearerToken(const std::string& token)
{
    auto it = validTokens.find(token);
    return it != validTokens.end() ? AuthResult::success(it->second) : AuthResult::failure("Invalid token");
}

AuthResult validateApiKey(const std::string& apiKey)
{
    auto it = validApiKeys.find(apiKey);
    return it != validApiKeys.end() ? AuthResult::success(it->second) : AuthResult::failure("Invalid API key");
}

AuthResult validateBasicAuth(const std::string& username, const std::string& password)
{
    auto it = validUsers.find(username);
    return (it != validUsers.end() && it->second == password) ? AuthResult::success(username) : AuthResult::failure("Invalid credentials");
}

// Request parser
HttpRequest parseRequest(const std::string& requestData, const std::string& remoteAddr)
{
    HttpRequest req;
    req.remoteAddr = remoteAddr;
    std::istringstream stream(requestData);
    std::string line;
    
    if (std::getline(stream, line))
    {
        std::istringstream lineStream(line);
        lineStream >> req.method >> req.path;
    }
    
    while (std::getline(stream, line) && !line.empty() && line != "\r")
    {
        if (line.back() == '\r') line.pop_back();
        
        auto colonPos = line.find(':');
        if (colonPos != std::string::npos)
        {
            std::string name = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
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
        // Configure proxy trust
        TrustProxyConfig proxyConfig;
        proxyConfig.setTrustMode(TrustMode::TrustSpecific);
        proxyConfig.addTrustedProxy("127.0.0.1");
        
        // Configure authentication
        AuthenticationMiddleware<HttpRequest, HttpResponse> authMiddleware;
        authMiddleware.addStrategy(std::make_shared<BearerTokenAuth<HttpRequest>>(validateBearerToken));
        authMiddleware.addStrategy(std::make_shared<ApiKeyAuth<HttpRequest>>(validateApiKey, "X-API-Key"));
        authMiddleware.addStrategy(std::make_shared<BasicAuth<HttpRequest>>(validateBasicAuth));
        
        authMiddleware.setAuthenticatedCallback([](HttpRequest& req, const AuthResult& authResult)
        {
            std::cout << "  ✓ Authenticated: " << authResult.userId << "\n";
        });
        
        // Configure compression
        registerSimpleCompression();
#ifdef _WIN32
        registerWindowsCompression();
#endif
        
        CompressionMiddleware compressionMiddleware;
        compressionMiddleware.setMinSize(500);
        compressionMiddleware.setLevel(6);
        
        std::cout << "Full-Featured HTTP Server\n";
        std::cout << "==========================\n";
        std::cout << "Features:\n";
        std::cout << "  • Proxy awareness (X-Forwarded headers)\n";
        std::cout << "  • Multi-strategy authentication\n";
        std::cout << "  • Automatic compression\n";
        std::cout << "\nListening on http://localhost:8080\n\n";
        
        tcp::SocketServer<> server(8080);
        
        server.onConnect([&](tcp::SocketConnection<>& connection)
        {
            connection.onMessage([&](const std::string& message)
            {
                HttpRequest req = parseRequest(message, connection.remoteAddress());
                HttpResponse res;
                
                // Extract real client info (proxy-aware)
                std::string clientIP = ProxyAwareHelpers::getClientIP(req, proxyConfig);
                std::string protocol = ProxyAwareHelpers::getProtocol(req, proxyConfig);
                bool isSecure = ProxyAwareHelpers::isSecure(req, proxyConfig);
                
                std::cout << req.method << " " << req.path << " from " << clientIP 
                          << " (" << (isSecure ? "HTTPS" : "HTTP") << ")\n";
                
                // Public endpoints
                if (req.path == "/" || req.path == "/public")
                {
                    std::ostringstream html;
                    html << "<!DOCTYPE html>\n<html>\n<head><title>Full-Featured Server</title></head>\n<body>\n";
                    html << "<h1>Full-Featured Server</h1>\n";
                    html << "<h2>Your Request</h2>\n";
                    html << "<ul>\n";
                    html << "<li>Real IP: " << clientIP << "</li>\n";
                    html << "<li>Protocol: " << protocol << "</li>\n";
                    html << "<li>Secure: " << (isSecure ? "Yes" : "No") << "</li>\n";
                    html << "</ul>\n";
                    html << "<h2>Try These Endpoints</h2>\n";
                    html << "<ul>\n";
                    html << "<li><code>GET /public</code> - This page (no auth)</li>\n";
                    html << "<li><code>GET /api/user</code> - Requires authentication</li>\n";
                    html << "<li><code>GET /api/data</code> - Large JSON (compressed)</li>\n";
                    html << "</ul>\n";
                    
                    // Pad to make compressible
                    for (int i = 0; i < 20; i++)
                    {
                        html << "<p>Lorem ipsum dolor sit amet. " << i << "</p>\n";
                    }
                    
                    html << "</body>\n</html>\n";
                    res.set_content(html.str());
                    res.set_header("Content-Type", "text/html");
                }
                else if (req.path.find("/api/") == 0)
                {
                    // Protected endpoints - require authentication
                    AuthResult authResult = authMiddleware.authenticate(req, res);
                    
                    if (!authResult)
                    {
                        res.status = 401;
                        res.statusText = "Unauthorized";
                        res.set_content("{\"error\": \"" + authResult.errorMessage + "\"}");
                        res.set_header("Content-Type", "application/json");
                        std::cout << "  ✗ Authentication failed\n";
                    }
                    else
                    {
                        // Handle API routes
                        if (req.path == "/api/user")
                        {
                            std::ostringstream json;
                            json << "{\n  \"userId\": \"" << authResult.userId << "\",\n";
                            json << "  \"clientIP\": \"" << clientIP << "\",\n";
                            json << "  \"protocol\": \"" << protocol << "\",\n";
                            json << "  \"secure\": " << (isSecure ? "true" : "false") << "\n}\n";
                            
                            res.set_content(json.str());
                            res.set_header("Content-Type", "application/json");
                        }
                        else if (req.path == "/api/data")
                        {
                            // Large JSON response (will be compressed)
                            std::ostringstream json;
                            json << "{\n  \"userId\": \"" << authResult.userId << "\",\n  \"data\": [\n";
                            for (int i = 0; i < 100; i++)
                            {
                                json << "    {\"id\": " << i << ", \"value\": \"Item " << i << "\"}";
                                if (i < 99) json << ",";
                                json << "\n";
                            }
                            json << "  ]\n}\n";
                            
                            res.set_content(json.str());
                            res.set_header("Content-Type", "application/json");
                        }
                        else
                        {
                            res.status = 404;
                            res.statusText = "Not Found";
                            res.set_content("{\"error\": \"Not found\"}");
                            res.set_header("Content-Type", "application/json");
                        }
                    }
                }
                else
                {
                    res.status = 404;
                    res.statusText = "Not Found";
                    res.set_content("404 Not Found");
                    res.set_header("Content-Type", "text/plain");
                }
                
                // Apply compression
                size_t originalSize = res.body.size();
                std::string encoding;
                bool compressed = compressionMiddleware.compressResponse(
                    req.get_header_value("Accept-Encoding"),
                    res.headers["Content-Type"],
                    res.body,
                    encoding
                );
                
                if (compressed)
                {
                    res.set_header("Content-Encoding", encoding);
                    res.set_header("Vary", "Accept-Encoding");
                    float ratio = (1.0f - (float)res.body.size() / originalSize) * 100.0f;
                    std::cout << "  ⚡ Compressed: " << originalSize << " → " << res.body.size() 
                              << " bytes (" << ratio << "% reduction)\n";
                }
                
                connection.send(res.toString());
                connection.close();
                std::cout << "\n";
            });
        });
        
        std::cout << "Test commands:\n\n";
        std::cout << "Public:\n";
        std::cout << "  curl http://localhost:8080/public\n\n";
        std::cout << "Authenticated + Compressed:\n";
        std::cout << "  curl -H \"Authorization: Bearer secret_token_123\" -H \"Accept-Encoding: rle\" http://localhost:8080/api/data\n\n";
        std::cout << "With proxy headers:\n";
        std::cout << "  curl -H \"X-Forwarded-For: 203.0.113.42\" -H \"X-Forwarded-Proto: https\" http://localhost:8080/public\n\n";
        
        server.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
