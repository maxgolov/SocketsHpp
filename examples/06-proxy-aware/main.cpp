// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Proxy-aware HTTP server.
///
/// Behind a reverse proxy (nginx, HAProxy, a load balancer) the TCP peer is the
/// proxy; the original client is described by X-Forwarded-* / Forwarded headers.
/// ProxyAwareHelpers reads those headers, but only when the direct peer is listed
/// in TrustProxyConfig, so clients cannot spoof them.
///
///   [Client] -> [nginx / HAProxy] -> [This server]
///
/// Usage: proxy-aware-server [port]   (default 8080)

#include <sockets.hpp>
#include <SocketsHpp/http/server/proxy_aware.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace SOCKETSHPP_NS::http::server;
using json = nlohmann::json;

namespace
{
    std::atomic<bool> g_running{true};
    void onSignal(int) { g_running = false; }

    // Everything echoed into HTML comes from the request: escape it, or a crafted
    // header or URL becomes script in the page (reflected XSS).
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

    struct ClientInfo
    {
        std::string ip, protocol, host;
        bool secure;
    };

    ClientInfo clientInfo(const HttpRequest& req, const TrustProxyConfig& trust, const std::string& defaultHost)
    {
        return {ProxyAwareHelpers::getClientIP(req.headers, req.client, trust),
                ProxyAwareHelpers::getProtocol(req.headers, req.client, trust),
                ProxyAwareHelpers::getHost(req.headers, req.client, trust, defaultHost),
                ProxyAwareHelpers::isSecure(req.headers, req.client, trust)};
    }
}  // namespace

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        const int port = argc > 1 ? std::stoi(argv[1]) : 8080;
        const std::string defaultHost = "localhost:" + std::to_string(port);

        // Trust forwarded headers only from these peers (addTrustedProxy() selects
        // TrustMode::TrustSpecific). TrustMode::TrustAll would let any client forge them.
        TrustProxyConfig trust;
        trust.addTrustedProxy("127.0.0.1");   // local nginx (and curl in the README)
        trust.addTrustedProxy("10.0.0.1");    // internal load balancer
        trust.addTrustedProxy("172.16.0.1");  // reverse proxy

        HttpServer server("0.0.0.0", port);

        // HTML page with the derived client details and all request headers
        server.route("/", [&trust, &defaultHost](const HttpRequest& req, HttpResponse& res) -> int {
            const ClientInfo info = clientInfo(req, trust, defaultHost);

            std::ostringstream html;
            html << "<!DOCTYPE html>\n<html>\n<head><title>Proxy-Aware Server</title></head>\n<body>\n"
                 << "<h1>Proxy-Aware Server</h1>\n<h2>Client (as reported by trusted proxies)</h2>\n<ul>\n"
                 << "<li><strong>Client IP:</strong> " << htmlEscape(info.ip) << "</li>\n"
                 << "<li><strong>Protocol:</strong> " << htmlEscape(info.protocol) << "</li>\n"
                 << "<li><strong>Host:</strong> " << htmlEscape(info.host) << "</li>\n"
                 << "<li><strong>Secure:</strong> " << (info.secure ? "yes" : "no") << "</li>\n"
                 << "<li><strong>Direct peer:</strong> " << htmlEscape(req.client) << "</li>\n"
                 << "<li><strong>URI:</strong> " << htmlEscape(req.uri) << "</li>\n"
                 << "</ul>\n<h2>Request headers</h2>\n<ul>\n";
            for (const auto& [name, value] : req.headers)
            {
                html << "<li><strong>" << htmlEscape(name) << ":</strong> " << htmlEscape(value) << "</li>\n";
            }
            html << "</ul>\n</body>\n</html>\n";

            res.set_content(html.str(), "text/html; charset=utf-8");
            std::cout << "GET " << req.uri << " from " << info.ip << " via " << req.client << std::endl;
            return 200;
        });

        // The same details as JSON
        server.route("/api/client", [&trust, &defaultHost](const HttpRequest& req, HttpResponse& res) -> int {
            const ClientInfo info = clientInfo(req, trust, defaultHost);
            json body = {
                {"clientIP", info.ip},
                {"protocol", info.protocol},
                {"host", info.host},
                {"secure", info.secure},
                {"directPeer", req.client},
            };
            res.set_content(body.dump(), "application/json");
            return 200;
        });

        server.start();
        std::cout << "Proxy-aware server on http://localhost:" << port << " - try:" << std::endl
                  << "  curl -H \"X-Forwarded-For: 203.0.113.42\" http://localhost:" << port << "/api/client"
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
