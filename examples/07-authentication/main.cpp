// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/*
 * Authenticated API Server Example
 * 
 * Demonstrates using the authentication framework with multiple strategies:
 * - Bearer tokens (OAuth 2.0 style)
 * - API keys (custom X-API-Key header)
 * - HTTP Basic authentication
 * 
 * Real-world usage would integrate with a database or OAuth provider.
 */

#include <sockets.hpp>
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/server/authentication.h>
#include <iostream>
#include <string>
#include <map>

using namespace SOCKETSHPP_NS;
using namespace SOCKETSHPP_NS::http::server;

// Simple HTTP request/response structures
struct HttpRequest
{
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
    
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

// Mock user database
std::map<std::string, std::string> validTokens = {
    {"secret_token_123", "user1"},
    {"admin_token_456", "admin"},
};

std::map<std::string, std::string> validApiKeys = {
    {"api_key_abc", "service1"},
    {"api_key_xyz", "service2"},
};

std::map<std::string, std::string> validUsers = {
    {"alice", "password123"},
    {"bob", "securepass"},
};

// Token validator callback
AuthResult validateBearerToken(const std::string& token)
{
    auto it = validTokens.find(token);
    if (it != validTokens.end())
    {
        // Return success with user ID
        return AuthResult::success(it->second);
    }
    return AuthResult::failure("Invalid token");
}

// API key validator callback
AuthResult validateApiKey(const std::string& apiKey)
{
    auto it = validApiKeys.find(apiKey);
    if (it != validApiKeys.end())
    {
        return AuthResult::success(it->second);
    }
    return AuthResult::failure("Invalid API key");
}

// Basic auth validator callback
AuthResult validateBasicAuth(const std::string& username, const std::string& password)
{
    auto it = validUsers.find(username);
    if (it != validUsers.end() && it->second == password)
    {
        return AuthResult::success(username);
    }
    return AuthResult::failure("Invalid credentials");
}

// Simple request parser
HttpRequest parseRequest(const std::string& requestData)
{
    HttpRequest req;
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
    
    // Read body (simplified)
    std::string bodyContent;
    while (std::getline(stream, line))
    {
        bodyContent += line + "\n";
    }
    req.body = bodyContent;
    
    return req;
}

int main()
{
    try
    {
        std::cout << "Authenticated API Server\n";
        std::cout << "========================\n";
        std::cout << "Listening on http://localhost:8080\n\n";
        
        // Create authentication middleware with multiple strategies
        AuthenticationMiddleware<HttpRequest, HttpResponse> authMiddleware;
        
        // Add Bearer token authentication
        auto bearerAuth = std::make_shared<BearerTokenAuth<HttpRequest>>(validateBearerToken);
        authMiddleware.addStrategy(bearerAuth);
        
        // Add API key authentication
        auto apiKeyAuth = std::make_shared<ApiKeyAuth<HttpRequest>>(validateApiKey, "X-API-Key");
        authMiddleware.addStrategy(apiKeyAuth);
        
        // Add Basic authentication
        auto basicAuth = std::make_shared<BasicAuth<HttpRequest>>(validateBasicAuth);
        authMiddleware.addStrategy(basicAuth);
        
        // Set authentication callback
        authMiddleware.setAuthenticatedCallback([](HttpRequest& req, const AuthResult& authResult)
        {
            std::cout << "Authenticated user: " << authResult.userId << "\n";
        });
        
        tcp::SocketServer<> server(8080);
        
        server.onConnect([&authMiddleware](tcp::SocketConnection<>& connection)
        {
            connection.onMessage([&authMiddleware, &connection](const std::string& message)
            {
                HttpRequest req = parseRequest(message);
                HttpResponse res;
                
                std::cout << req.method << " " << req.path << "\n";
                
                // Public endpoint (no auth required)
                if (req.path == "/" || req.path == "/public")
                {
                    res.set_content("Public endpoint - no authentication required");
                    connection.send(res.toString());
                    connection.close();
                    return;
                }
                
                // Protected endpoint - requires authentication
                AuthResult authResult = authMiddleware.authenticate(req, res);
                
                if (!authResult)
                {
                    // Authentication failed
                    res.status = 401;
                    res.statusText = "Unauthorized";
                    res.set_content("{\"error\": \"" + authResult.errorMessage + "\"}");
                    res.set_header("Content-Type", "application/json");
                    
                    std::cout << "  Authentication failed: " << authResult.errorMessage << "\n";
                    
                    connection.send(res.toString());
                    connection.close();
                    return;
                }
                
                // Authentication successful - handle protected routes
                if (req.path == "/api/user")
                {
                    std::ostringstream json;
                    json << "{\n";
                    json << "  \"userId\": \"" << authResult.userId << "\",\n";
                    json << "  \"message\": \"Welcome to the protected API!\",\n";
                    json << "  \"claims\": {\n";
                    
                    bool first = true;
                    for (const auto& [key, value] : authResult.claims)
                    {
                        if (!first) json << ",\n";
                        json << "    \"" << key << "\": \"" << value << "\"";
                        first = false;
                    }
                    
                    json << "\n  }\n";
                    json << "}\n";
                    
                    res.set_content(json.str());
                    res.set_header("Content-Type", "application/json");
                }
                else if (req.path == "/api/admin")
                {
                    // Admin-only endpoint
                    if (authResult.userId != "admin")
                    {
                        res.status = 403;
                        res.statusText = "Forbidden";
                        res.set_content("{\"error\": \"Admin access required\"}");
                        res.set_header("Content-Type", "application/json");
                    }
                    else
                    {
                        res.set_content("{\"message\": \"Admin panel access granted\"}");
                        res.set_header("Content-Type", "application/json");
                    }
                }
                else
                {
                    res.status = 404;
                    res.statusText = "Not Found";
                    res.set_content("{\"error\": \"Endpoint not found\"}");
                    res.set_header("Content-Type", "application/json");
                }
                
                connection.send(res.toString());
                connection.close();
            });
        });
        
        std::cout << "Test with:\n\n";
        std::cout << "Public endpoint:\n";
        std::cout << "  curl http://localhost:8080/public\n\n";
        
        std::cout << "Bearer token (OAuth style):\n";
        std::cout << "  curl -H \"Authorization: Bearer secret_token_123\" http://localhost:8080/api/user\n\n";
        
        std::cout << "API key:\n";
        std::cout << "  curl -H \"X-API-Key: api_key_abc\" http://localhost:8080/api/user\n\n";
        
        std::cout << "Basic authentication:\n";
        std::cout << "  curl -u alice:password123 http://localhost:8080/api/user\n\n";
        
        std::cout << "Admin endpoint:\n";
        std::cout << "  curl -H \"Authorization: Bearer admin_token_456\" http://localhost:8080/api/admin\n\n";
        
        std::cout << "Unauthorized (should fail):\n";
        std::cout << "  curl http://localhost:8080/api/user\n\n";
        
        server.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
