// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/*
 * HTTP Server with Compression Example
 * 
 * Demonstrates the compression framework with:
 * - Accept-Encoding negotiation
 * - Content-Type based compression
 * - Platform-specific compression (Windows API on Windows, SimpleRLE for testing)
 * - Minimum size threshold
 */

#include <sockets.hpp>
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/server/compression.h>
#include <SocketsHpp/http/server/compression_simple.h>
#ifdef _WIN32
#include <SocketsHpp/http/server/compression_windows.h>
#endif
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

using namespace SOCKETSHPP_NS;
using namespace SOCKETSHPP_NS::net::common;


// Simple HTTP request/response structures
struct HttpRequest
{
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    
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
    
    return req;
}

// Generate sample content
std::string generateLargeText()
{
    std::ostringstream content;
    content << "<!DOCTYPE html>\n";
    content << "<html>\n";
    content << "<head><title>Compression Demo</title></head>\n";
    content << "<body>\n";
    content << "<h1>HTTP Compression Demonstration</h1>\n";
    content << "<p>This response demonstrates automatic compression based on Accept-Encoding.</p>\n";
    content << "<h2>Lorem Ipsum (Repeated for Compression)</h2>\n";
    
    // Repeat lorem ipsum to create compressible content
    for (int i = 0; i < 50; i++)
    {
        content << "<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
        content << "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ";
        content << "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris. ";
        content << "This is repetition number " << i << ".</p>\n";
    }
    
    content << "</body>\n";
    content << "</html>\n";
    
    return content.str();
}

int main()
{
    try
    {
        // Register compression strategies
        SOCKETSHPP_NS::http::server::compression::registerSimpleCompression(); // RLE and Identity for testing
        
#ifdef _WIN32
        // Register Windows compression (MSZIP, XPRESS, LZMS)
        SOCKETSHPP_NS::http::server::compression::registerWindowsCompression();
        std::cout << "Windows Compression API registered (MSZIP, XPRESS, LZMS)\n";
#endif
        
        // Create compression middleware
        SOCKETSHPP_NS::http::server::compression::CompressionMiddleware middleware;
        middleware.setMinSize(500); // Only compress responses > 500 bytes
        middleware.setLevel(6);      // Compression level (1-9)
        
        // Show registered compression algorithms
        auto encodings = SOCKETSHPP_NS::http::server::compression::SOCKETSHPP_NS::http::server::compression::CompressionRegistry::instance().supportedEncodings();
        std::cout << "Compression Server\n";
        std::cout << "==================\n";
        std::cout << "Supported encodings: ";
        for (size_t i = 0; i < encodings.size(); i++)
        {
            std::cout << encodings[i];
            if (i < encodings.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
        std::cout << "Minimum size: " << 500 << " bytes\n";
        std::cout << "Listening on http://localhost:8080\n\n";
        
        tcp::SocketServer<> server(8080);
        
        server.onConnect([&middleware](tcp::SocketConnection<>& connection)
        {
            connection.onMessage([&middleware, &connection](const std::string& message)
            {
                HttpRequest req = parseRequest(message);
                
                std::cout << req.method << " " << req.path << "\n";
                
                std::string contentType;
                std::string body;
                
                // Route handling
                if (req.path == "/" || req.path == "/index.html")
                {
                    body = generateLargeText();
                    contentType = "text/html";
                }
                else if (req.path == "/api/data")
                {
                    // JSON response
                    std::ostringstream json;
                    json << "{\n";
                    json << "  \"message\": \"This is a JSON API response\",\n";
                    json << "  \"data\": [\n";
                    for (int i = 0; i < 100; i++)
                    {
                        json << "    {\"id\": " << i << ", \"value\": \"Item " << i << "\"}";
                        if (i < 99) json << ",";
                        json << "\n";
                    }
                    json << "  ]\n";
                    json << "}\n";
                    
                    body = json.str();
                    contentType = "application/json";
                }
                else if (req.path == "/small")
                {
                    body = "Small response (not compressed due to size threshold)";
                    contentType = "text/plain";
                }
                else
                {
                    body = "404 Not Found";
                    contentType = "text/plain";
                }
                
                size_t originalSize = body.size();
                std::string encoding;
                
                // Try to compress the response
                std::string acceptEncoding = req.get_header_value("Accept-Encoding");
                bool compressed = middleware.compressResponse(
                    acceptEncoding,
                    contentType,
                    body,
                    encoding
                );
                
                size_t compressedSize = body.size();
                
                // Build HTTP response
                std::ostringstream response;
                response << "HTTP/1.1 200 OK\r\n";
                response << "Content-Type: " << contentType << "\r\n";
                response << "Content-Length: " << body.length() << "\r\n";
                
                if (compressed && !encoding.empty())
                {
                    response << "Content-Encoding: " << encoding << "\r\n";
                    response << "Vary: Accept-Encoding\r\n";
                }
                
                response << "Connection: close\r\n";
                response << "\r\n";
                response << body;
                
                connection.send(response.str());
                connection.close();
                
                // Log compression statistics
                std::cout << "  Original size: " << originalSize << " bytes\n";
                if (compressed)
                {
                    float ratio = (1.0f - (float)compressedSize / originalSize) * 100.0f;
                    std::cout << "  Compressed (" << encoding << "): " << compressedSize 
                              << " bytes (" << ratio << "% reduction)\n";
                }
                else
                {
                    std::cout << "  Not compressed (";
                    if (originalSize < 500)
                        std::cout << "too small";
                    else if (acceptEncoding.empty())
                        std::cout << "client doesn't support compression";
                    else
                        std::cout << "no matching encoding";
                    std::cout << ")\n";
                }
                std::cout << "\n";
            });
        });
        
        std::cout << "Test with:\n\n";
        
        std::cout << "Without compression:\n";
        std::cout << "  curl http://localhost:8080/\n\n";
        
        std::cout << "With compression (RLE):\n";
        std::cout << "  curl -H \"Accept-Encoding: rle\" http://localhost:8080/ --output -\n\n";
        
#ifdef _WIN32
        std::cout << "With Windows compression (MSZIP):\n";
        std::cout << "  curl -H \"Accept-Encoding: mszip\" http://localhost:8080/ --output -\n\n";
        
        std::cout << "With Windows compression (XPRESS):\n";
        std::cout << "  curl -H \"Accept-Encoding: xpress\" http://localhost:8080/ --output -\n\n";
#endif
        
        std::cout << "JSON API:\n";
        std::cout << "  curl -H \"Accept-Encoding: rle\" http://localhost:8080/api/data\n\n";
        
        std::cout << "Small response (not compressed):\n";
        std::cout << "  curl http://localhost:8080/small\n\n";
        
        server.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}


