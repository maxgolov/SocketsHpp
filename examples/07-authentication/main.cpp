// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief HTTP server with authentication example
///
/// This example demonstrates:
/// - Bearer token authentication
/// - API key authentication
/// - Protected and public endpoints

#include <sockets.hpp>
#include <iostream>
#include <string>
#include <map>

using namespace SOCKETSHPP_NS;
using namespace SOCKETSHPP_NS::http::server;

// Mock authentication database
std::map<std::string, std::string> validBearerTokens = {
    {"secret_token_123", "user1"},
    {"admin_token_456", "admin"}
};

std::map<std::string, std::string> validApiKeys = {
    {"api_key_abc", "service1"},
    {"api_key_xyz", "service2"}
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
        HttpServer server("localhost", 8080);

        // Public endpoint - no authentication required
        server.route("/", [](const HttpRequest& req, HttpResponse& res) -> int {
            res.set_header("Content-Type", "text/html");
            res.set_content(
                "<html><body>"
                "<h1>Authentication Example</h1>"
                "<p>Public endpoint - no authentication required</p>"
                "<h2>Try authenticated endpoints:</h2>"
                "<ul>"
                "<li>curl -H \"Authorization: Bearer secret_token_123\" http://localhost:8080/api/user</li>"
                "<li>curl -H \"X-API-Key: api_key_abc\" http://localhost:8080/api/service</li>"
                "</ul>"
                "</body></html>"
            );
            return 200;
        });

        // Protected endpoint - requires authentication
        server.route("/api/user", [](const HttpRequest& req, HttpResponse& res) -> int {
            std::string user = checkAuth(req);
            if (user.empty())
            {
                res.set_status(401);
                res.set_header("Content-Type", "application/json");
                res.set_header("WWW-Authenticate", "Bearer");
                res.set_content("{\"error\": \"Unauthorized\", \"message\": \"Valid Bearer token required\"}");
                return 401;
            }

            res.set_header("Content-Type", "application/json");
            res.set_content("{\"user\": \"" + user + "\", \"endpoint\": \"/api/user\"}");
            return 200;
        });

        // Protected service endpoint - requires API key
        server.route("/api/service", [](const HttpRequest& req, HttpResponse& res) -> int {
            std::string service = checkAuth(req);
            if (service.empty())
            {
                res.set_status(401);
                res.set_header("Content-Type", "application/json");
                res.set_content("{\"error\": \"Unauthorized\", \"message\": \"Valid X-API-Key header required\"}");
                return 401;
            }

            res.set_header("Content-Type", "application/json");
            res.set_content("{\"service\": \"" + service + "\", \"endpoint\": \"/api/service\"}");
            return 200;
        });

        std::cout << "Authentication server listening on http://localhost:8080\n";
        std::cout << "Test commands:\n";
        std::cout << "  curl http://localhost:8080/\n";
        std::cout << "  curl -H \"Authorization: Bearer secret_token_123\" http://localhost:8080/api/user\n";
        std::cout << "  curl -H \"X-API-Key: api_key_abc\" http://localhost:8080/api/service\n";

        server.start();
        
        // Keep server running
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
