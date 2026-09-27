// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief HTTP server protected with the library's authentication helpers.
///
/// This example demonstrates:
/// - AuthenticationMiddleware trying several strategies in order
/// - BearerTokenAuth, BasicAuth and ApiKeyAuth with validator callbacks
/// - 401 responses with WWW-Authenticate challenges (filled in by the middleware)
/// - Claims on the AuthResult and a 403 for an insufficient role
///
/// Usage: authenticated-api [port]   (default 8080)

#include <sockets.hpp>
#include <SocketsHpp/http/server/authentication.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

using namespace SOCKETSHPP_NS::http::server;
using json = nlohmann::json;
using Auth = AuthenticationMiddleware<HttpRequest, HttpResponse>;

namespace
{
    std::atomic<bool> g_running{true};
    void onSignal(int) { g_running = false; }

    // Demo credential store. A real server would keep hashed secrets and compare
    // them in constant time.
    struct Account
    {
        std::string user, role;
    };
    const std::map<std::string, Account> kBearerTokens = {
        {"secret_token_123", {"user1", "user"}},
        {"admin_token_456", {"admin", "admin"}},
    };
    const std::map<std::string, std::string> kPasswords = {{"alice", "wonderland"}};
    const std::map<std::string, std::string> kApiKeys = {{"api_key_abc", "service1"}, {"api_key_xyz", "service2"}};

    AuthResult checkToken(const std::string& token)
    {
        auto it = kBearerTokens.find(token);
        if (it == kBearerTokens.end())
        {
            return AuthResult::failure("Unknown bearer token");
        }
        return AuthResult::success(it->second.user, {{"role", it->second.role}});
    }

    AuthResult checkPassword(const std::string& user, const std::string& password)
    {
        auto it = kPasswords.find(user);
        if (it == kPasswords.end() || it->second != password)
        {
            return AuthResult::failure("Wrong user name or password");
        }
        return AuthResult::success(user, {{"role", "user"}});
    }

    AuthResult checkApiKey(const std::string& key)
    {
        auto it = kApiKeys.find(key);
        return it == kApiKeys.end() ? AuthResult::failure("Unknown API key") : AuthResult::success(it->second);
    }
}  // namespace

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        const int port = argc > 1 ? std::stoi(argv[1]) : 8080;

        // Users: a bearer token or HTTP Basic credentials (tried in this order)
        Auth userAuth;
        userAuth.addStrategy(std::make_shared<BearerTokenAuth<HttpRequest>>(checkToken));
        userAuth.addStrategy(std::make_shared<BasicAuth<HttpRequest>>(checkPassword));

        // Services: an X-API-Key header
        Auth serviceAuth;
        serviceAuth.addStrategy(std::make_shared<ApiKeyAuth<HttpRequest>>("X-API-Key", checkApiKey));

        HttpServer server("0.0.0.0", port);

        // Public. "/" also receives every unknown path, so answer those with 404.
        server.route("/", [](const HttpRequest& req, HttpResponse& res) -> int {
            if (req.uri.substr(0, req.uri.find('?')) != "/")
            {
                res.set_content(json{{"error", "Not found"}}.dump(), "application/json");
                return 404;
            }
            res.set_content("<!DOCTYPE html><html><body><h1>Authentication Example</h1>"
                            "<p>Public page. Protected routes: /api/user, /api/admin, /api/service "
                            "(see the README for curl commands).</p></body></html>",
                            "text/html; charset=utf-8");
            return 200;
        });

        // Any authenticated user. On failure authenticate() has already filled in the
        // 401 response (status, WWW-Authenticate challenges, JSON body).
        server.route("/api/user", [&userAuth](const HttpRequest& req, HttpResponse& res) -> int {
            AuthResult auth;
            if (!userAuth.authenticate(req, res, &auth))
            {
                return 401;
            }
            json body = {{"user", auth.userId}, {"role", auth.claims["role"]}};
            res.set_content(body.dump(), "application/json");
            return 200;
        });

        // Authenticated users with the "admin" role: 401 without credentials, 403 with
        // valid credentials but the wrong role.
        server.route("/api/admin", [&userAuth](const HttpRequest& req, HttpResponse& res) -> int {
            AuthResult auth;
            if (!userAuth.authenticate(req, res, &auth))
            {
                return 401;
            }
            if (auth.claims["role"] != "admin")
            {
                res.set_content(json{{"error", "Forbidden"}, {"message", "admin role required"}}.dump(),
                                "application/json");
                return 403;
            }
            res.set_content(json{{"user", auth.userId}, {"admin", true}}.dump(), "application/json");
            return 200;
        });

        server.route("/api/service", [&serviceAuth](const HttpRequest& req, HttpResponse& res) -> int {
            AuthResult auth;
            if (!serviceAuth.authenticate(req, res, &auth))
            {
                return 401;
            }
            res.set_content(json{{"service", auth.userId}}.dump(), "application/json");
            return 200;
        });

        server.start();
        std::cout << "Authentication example on http://localhost:" << port << " - press Ctrl+C to stop"
                  << std::endl;

        while (g_running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "Shutting down..." << std::endl;
        server.stop();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
