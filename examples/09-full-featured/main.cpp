// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Full-featured HTTP server: a small notes API.
///
/// Combines the server features shown separately in examples 03-08:
/// - Worker thread pool (handlers run concurrently, so shared state is locked)
/// - CORS (enableCors / setCorsOrigin / setCorsHeaders) with automatic preflight answers
/// - Authentication middleware (bearer token or API key)
/// - Proxy awareness (real client IP / scheme behind a trusted reverse proxy)
/// - Response compression (CompressionMiddleware, toy "rle" codec)
/// - GET and POST routes with JSON bodies (nlohmann::json)
/// - Graceful shutdown on Ctrl+C / SIGTERM
///
/// Usage: full-featured-server [port]   (default 8080)

#include <sockets.hpp>
#include <SocketsHpp/http/server/authentication.h>
#include <SocketsHpp/http/server/compression.h>
#include <SocketsHpp/http/server/compression_simple.h>
#include <SocketsHpp/http/server/proxy_aware.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace SOCKETSHPP_NS::http::server;
using json = nlohmann::json;

namespace
{
    std::atomic<bool> g_running{true};
    void onSignal(int) { g_running = false; }

    std::string htmlEscape(const std::string& s)
    {
        std::string out;
        for (char c : s)
        {
            switch (c)
            {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
            }
        }
        return out;
    }

    // Demo credentials (a real server keeps hashed secrets, compared in constant time)
    const std::map<std::string, std::string> kTokens = {{"secret_token_123", "user1"}, {"admin_token_456", "admin"}};
    const std::map<std::string, std::string> kApiKeys = {{"api_key_abc", "service1"}};

    AuthResult lookup(const std::map<std::string, std::string>& table, const std::string& credential)
    {
        auto it = table.find(credential);
        return it == table.end() ? AuthResult::failure("Unknown credential") : AuthResult::success(it->second);
    }

    // Notes shared by all worker threads
    std::mutex g_notesMutex;
    json g_notes = json::array();
    constexpr size_t kMaxNotes = 100;
}  // namespace

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        const int port = argc > 1 ? std::stoi(argv[1]) : 8080;

        // Forwarded headers are honoured only from these peers
        TrustProxyConfig trust;
        trust.addTrustedProxy("127.0.0.1");

        // Bearer token or X-API-Key, tried in this order
        AuthenticationMiddleware<HttpRequest, HttpResponse> auth;
        auth.addStrategy(std::make_shared<BearerTokenAuth<HttpRequest>>(
            [](const std::string& token) { return lookup(kTokens, token); }));
        auth.addStrategy(std::make_shared<ApiKeyAuth<HttpRequest>>(
            "X-API-Key", [](const std::string& key) { return lookup(kApiKeys, key); }));

        // Register codecs before start(); "rle" only shrinks bodies with long runs.
        compression::registerSimpleCompression();
        CompressionMiddleware compression;
        compression.setMinSize(256);

        // Send body with status, compressed if the client accepts a registered codec
        auto reply = [&compression](const HttpRequest& req, HttpResponse& res, int status, std::string body,
                                    const std::string& contentType) {
            std::string encoding;
            if (compression.compressResponse(req.get_header_value("Accept-Encoding"), contentType, body, encoding))
            {
                res.set_header("Content-Encoding", encoding);
            }
            res.set_header("Vary", "Accept-Encoding");
            res.set_content(body, contentType);
            return status;
        };

        HttpServer server("0.0.0.0", port);
        server.enableThreadPool(4);
        server.enableCors();
        server.setCorsOrigin("http://localhost:3000");  // the web app allowed to call this API
        server.setCorsHeaders("Content-Type, Authorization, X-API-Key", "Content-Encoding");

        // Public page. Every handler declines OPTIONS (returns 0 without touching the
        // response) so the server answers CORS preflights itself with 204.
        server.route("/", [&](const HttpRequest& req, HttpResponse& res) -> int {
            if (req.method == "OPTIONS")
            {
                return 0;
            }
            if (req.uri.substr(0, req.uri.find('?')) != "/")
            {
                return reply(req, res, 404, json{{"error", "Not found"}}.dump(), "application/json");
            }
            const std::string clientIP = ProxyAwareHelpers::getClientIP(req.headers, req.client, trust);
            const std::string protocol = ProxyAwareHelpers::getProtocol(req.headers, req.client, trust);
            return reply(req, res, 200,
                         "<!DOCTYPE html><html><body><h1>Notes API</h1><p>Your address: " + htmlEscape(clientIP) +
                             " (" + htmlEscape(protocol) + ")</p><p>GET /api/notes, POST /api/notes "
                             "(authenticated, see the README).</p></body></html>",
                         "text/html; charset=utf-8");
        });

        server.route("/api/notes", [&](const HttpRequest& req, HttpResponse& res) -> int {
            if (req.method == "OPTIONS")
            {
                return 0;
            }
            if (req.method != "GET" && req.method != "POST")
            {
                res.set_header("Allow", "GET, POST, OPTIONS");
                return reply(req, res, 405, json{{"error", "Method not allowed"}}.dump(), "application/json");
            }
            AuthResult user;
            if (!auth.authenticate(req, res, &user))
            {
                return 401;  // the middleware filled in the 401 response
            }

            if (req.method == "GET")
            {
                std::lock_guard<std::mutex> lock(g_notesMutex);
                return reply(req, res, 200, json{{"notes", g_notes}}.dump(), "application/json");
            }

            // POST {"text": "..."}
            json body = json::parse(req.content, nullptr, /*allow_exceptions=*/false);
            if (!body.is_object() || !body.contains("text") || !body["text"].is_string() ||
                body["text"].get<std::string>().empty())
            {
                return reply(req, res, 400, json{{"error", "expected {\"text\": \"...\"}"}}.dump(),
                             "application/json");
            }
            json note = {
                {"text", body["text"]},
                {"author", user.userId},
                {"clientIP", ProxyAwareHelpers::getClientIP(req.headers, req.client, trust)},
            };
            {
                std::lock_guard<std::mutex> lock(g_notesMutex);
                if (g_notes.size() >= kMaxNotes)
                {
                    return reply(req, res, 507, json{{"error", "note limit reached"}}.dump(), "application/json");
                }
                note["id"] = g_notes.size() + 1;
                g_notes.push_back(note);
            }
            std::cout << "note " << note["id"] << " from " << user.userId << std::endl;
            return reply(req, res, 201, note.dump(), "application/json");
        });

        server.start();
        std::cout << "Notes API on http://localhost:" << port << " - press Ctrl+C to stop" << std::endl;

        while (g_running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        // stop() stops accepting requests; the destructor then waits for running handlers.
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
