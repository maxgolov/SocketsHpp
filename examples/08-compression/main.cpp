// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief HTTP response compression with CompressionMiddleware.
///
/// This example demonstrates:
/// - Registering codecs in CompressionRegistry (here the toy "rle" codec from
///   compression_simple.h; register zlib/brotli/zstd-based codecs for real clients)
/// - CompressionMiddleware::compressResponse(): Accept-Encoding negotiation, minimum
///   size and content-type checks
/// - Setting Content-Encoding and Vary on the response
///
/// Usage: compression-server [port]   (default 8080)

#include <sockets.hpp>
#include <SocketsHpp/http/server/compression.h>
#include <SocketsHpp/http/server/compression_simple.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace SOCKETSHPP_NS::http::server;

namespace
{
    std::atomic<bool> g_running{true};
    void onSignal(int) { g_running = false; }

    // Compress body if the client accepts a registered encoding, then send it.
    int sendCompressed(CompressionMiddleware& compression, const HttpRequest& req, HttpResponse& res,
                       std::string body, const std::string& contentType)
    {
        std::string encoding;
        if (compression.compressResponse(req.get_header_value("Accept-Encoding"), contentType, body, encoding))
        {
            res.set_header("Content-Encoding", encoding);
        }
        res.set_header("Vary", "Accept-Encoding");  // caches must key on Accept-Encoding
        res.set_content(body, contentType);
        return 200;
    }
}  // namespace

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        const int port = argc > 1 ? std::stoi(argv[1]) : 8080;

        // Register codecs before start(): the registry is not synchronized.
        // "rle" is a toy run-length codec for experiments; browsers cannot decode it.
        compression::registerSimpleCompression();
        // A real codec: CompressionRegistry::instance().registerStrategy(
        //     std::make_shared<CompressionStrategy>("gzip", myCompress, myDecompress));

        CompressionMiddleware compression;  // compresses text/JSON bodies of 1 KB or more
        compression.setMinSize(256);

        HttpServer server("0.0.0.0", port);

        // A large, repetitive text body (run-length encoding only helps with runs)
        server.route("/", [&compression](const HttpRequest& req, HttpResponse& res) -> int {
            if (req.uri.substr(0, req.uri.find('?')) != "/")
            {
                res.set_content("Not found\n", "text/plain");
                return 404;
            }
            std::ostringstream text;
            text << "Compression demo: a bar chart\n";
            for (int i = 1; i <= 40; ++i)
            {
                text << std::string(i * 2, '#') << std::string(80 - i * 2, ' ') << "|\n";
            }
            return sendCompressed(compression, req, res, text.str(), "text/plain");
        });

        // Below the minimum size: always sent as is
        server.route("/small", [&compression](const HttpRequest& req, HttpResponse& res) -> int {
            return sendCompressed(compression, req, res, "Too small to be worth compressing.\n", "text/plain");
        });

        server.start();
        std::cout << "Compression server on http://localhost:" << port << " - try:" << std::endl
                  << "  curl -si -H \"Accept-Encoding: rle\" http://localhost:" << port << "/" << std::endl;

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
