// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/*
 * Full-Featured HTTP Server Example
 * 
 * Demonstrates combining proxy awareness with authentication.
 * Shows real client IPs from behind proxies while enforcing auth.
 * 
 * Features:
 * - Proxy-aware headers (X-Forwarded-*)
 * - Bearer token authentication
 * - API key authentication
 * - Public and protected endpoints
 */

#include <sockets.hpp>
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/server/proxy_aware.h>
#include <iostream>
#include <string>
#include <map>
#include <sstream>

using namespace SOCKETSHPP_NS;
using namespace SOCKETSHPP_NS::http::server;

// Mock authentication database
std::map<std::string, std::string> validBearerTokens = {
    {"secret_token_123", "user1"},
    {"admin_token_456", "admin"}
};

std::map<std::string, std::string> validApiKeys = {
    {"api_key_abc", "service1"}
};

// Check authentication and return username, or empty string if unauthorized
std::string checkAuth(const HttpRequest& req)
{
    // Check Bearer token authentication
    if (req.has_header("Authorization"))
    {
        std::string auth = req.get_header_value("Authorization");
        if (auth.substr(0, 7) == "Bearer " && auth.length() > 7)
        {
            std::string token = auth.substr(7);
            auto it = validBearerTokens.find(token);
            if (it != validBearerTokens.end())
            {
                return it->second;
            }
        }
    }

    // Check API Key authentication
    if (req.has_header("X-API-Key"))
    {
        std::string apiKey = req.get_header_value("X-API-Key");
        auto it = validApiKeys.find(apiKey);
        if (it != validApiKeys.end())
        {
            return it->second;
        }
    }

    return "";
}


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
        
        std::cout << "Full-Featured HTTP Server\n";
        std::cout << "=========================\n";
        std::cout << "Features: Proxy awareness + Authentication\n";
        std::cout << "Listening on http://localhost:8080\n\n";
        
        // Create HTTP server
        HttpServer server("localhost", 8080);
        
        // Public endpoint with proxy awareness
        server.route("/", [&config](const HttpRequest& req, HttpResponse& res) -> int
        {
            std::string clientIP = ProxyAwareHelpers::getClientIP(req.headers, req.client, config);
            std::string protocol = ProxyAwareHelpers::getProtocol(req.headers, req.client, config);
            std::string host = ProxyAwareHelpers::getHost(req.headers, req.client, config, "localhost:8080");

            std::ostringstream html;
            html << "<html><body>"
                 << "<h1>Full-Featured Server Example</h1>"
                 << "<h2>Request Information (Proxy-Aware)</h2>"
                 << "<ul>"
                 << "<li>Client IP: " << clientIP << "</li>"
                 << "<li>Protocol: " << protocol << "</li>"
                 << "<li>Host: " << host << "</li>"
                 << "<li>URI: " << req.uri << "</li>"
                 << "</ul>"
                 << "<h2>Features Demonstrated</h2>"
                 << "<ul>"
                 << "<li>Proxy awareness (X-Forwarded-* headers)</li>"
                 << "<li>Authentication (Bearer tokens, API keys)</li>"
                 << "<li>Multiple routes and HTTP methods</li>"
                 << "</ul>"
                 << "<h2>Try Protected Endpoints</h2>"
                 << "<pre>"
                 << "curl -H \"Authorization: Bearer secret_token_123\" http://localhost:8080/api/protected\n"
                 << "curl -H \"X-API-Key: api_key_abc\" http://localhost:8080/api/service"
                 << "</pre>"
                 << "</body></html>";

            res.set_header("Content-Type", "text/html");
            res.set_content(html.str());
            return 200;
        });

        // Protected endpoint requiring authentication
        server.route("/api/protected", [&config](const HttpRequest& req, HttpResponse& res) -> int {
            std::string user = checkAuth(req);
            if (user.empty())
            {
                res.set_status(401);
                res.set_header("Content-Type", "application/json");
                res.set_header("WWW-Authenticate", "Bearer");
                res.set_content("{\"error\": \"Unauthorized\", \"message\": \"Valid Bearer token required\"}");
                return 401;
            }

            std::string clientIP = ProxyAwareHelpers::getClientIP(req.headers, req.client, config);
            std::ostringstream json;
            json << "{"
                 << "\"user\": \"" << user << "\","
                 << "\"endpoint\": \"/api/protected\","
                 << "\"clientIP\": \"" << clientIP << "\","
                 << "\"authenticated\": true"
                 << "}";

            res.set_header("Content-Type", "application/json");
            res.set_content(json.str());
            return 200;
        });

        // Service endpoint requiring API key
        server.route("/api/service", [](const HttpRequest& req, HttpResponse& res) -> int {
            std::string service = checkAuth(req);
            if (service.empty())
            {
                res.set_status(401);
                res.set_header("Content-Type", "application/json");
                res.set_content("{\"error\": \"Unauthorized\", \"message\": \"Valid X-API-Key header required\"}");
                return 401;
            }

            std::ostringstream json;
            json << "{"
                 << "\"service\": \"" << service << "\","
                 << "\"endpoint\": \"/api/service\","
                 << "\"authenticated\": true"
                 << "}";

            res.set_header("Content-Type", "application/json");
            res.set_content(json.str());
            return 200;
        });

        std::cout << "Test commands:\n";
        std::cout << "  curl http://localhost:8080/\n";
        std::cout << "  curl -H \"Authorization: Bearer secret_token_123\" http://localhost:8080/api/protected\n";
        std::cout << "  curl -H \"X-API-Key: api_key_abc\" http://localhost:8080/api/service\n";
        
        server.start();
        
        // Keep server running
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}


