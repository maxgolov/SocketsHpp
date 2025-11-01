// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>

#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../../net/common/socket_tools.h"
#include "../common/url_parser.h"

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace client
    {
        using Socket = net::utils::Socket;
        using SocketAddr = net::utils::SocketAddr;
        using SocketParams = net::utils::SocketParams;
        using UrlParser = http::common::UrlParser;

        struct HttpClientRequest
        {
            std::string method = "GET";
            std::string uri;
            std::string protocol = "HTTP/1.1";
            std::map<std::string, std::string> headers;
            std::string body;

            // Add header helper
            void setHeader(const std::string& name, const std::string& value)
            {
                headers[name] = value;
            }

            // Common headers
            void setContentType(const std::string& contentType)
            {
                headers["Content-Type"] = contentType;
            }

            void setUserAgent(const std::string& userAgent)
            {
                headers["User-Agent"] = userAgent;
            }

            void setAccept(const std::string& accept)
            {
                headers["Accept"] = accept;
            }
        };

        struct HttpClientResponse
        {
            int code = 0;
            std::string message;
            std::string protocol;
            std::map<std::string, std::string> headers;
            std::string body;

            // Streaming support
            bool isChunked = false;
            std::function<void(const std::string&)> chunkCallback;  // Called for each chunk
            std::function<void()> onComplete;  // Called when response is complete

            // Helper to get header value
            std::string getHeader(const std::string& name) const
            {
                auto it = headers.find(name);
                return (it != headers.end()) ? it->second : "";
            }

            bool hasHeader(const std::string& name) const
            {
                return headers.find(name) != headers.end();
            }
        };

        class HttpClient
        {
        protected:
            std::string m_userAgent = "SocketsHpp/1.1";
            int m_connectTimeoutMs = 10000;  // 10 seconds
            int m_readTimeoutMs = 30000;     // 30 seconds
            bool m_followRedirects = true;
            int m_maxRedirects = 10;

        public:
            HttpClient() = default;

            void setUserAgent(const std::string& userAgent) { m_userAgent = userAgent; }
            void setConnectTimeout(int ms) { m_connectTimeoutMs = ms; }
            void setReadTimeout(int ms) { m_readTimeoutMs = ms; }
            void setFollowRedirects(bool follow) { m_followRedirects = follow; }
            void setMaxRedirects(int max) { m_maxRedirects = max; }

            // Simple GET request
            bool get(const std::string& url, HttpClientResponse& response)
            {
                HttpClientRequest request;
                request.method = "GET";
                request.uri = url;
                return send(request, response);
            }

            // Simple POST request
            bool post(const std::string& url, const std::string& body, HttpClientResponse& response)
            {
                HttpClientRequest request;
                request.method = "POST";
                request.uri = url;
                request.body = body;
                return send(request, response);
            }

            // Send custom request
            bool send(HttpClientRequest& request, HttpClientResponse& response)
            {
                // Parse URL
                UrlParser url(request.uri);
                if (!url.success_)
                {
                    LOG_ERROR("HttpClient: Failed to parse URL: %s", request.uri.c_str());
                    return false;
                }

                // Set default headers
                if (request.headers.find("Host") == request.headers.end())
                {
                    request.headers["Host"] = url.host_;
                }
                if (request.headers.find("User-Agent") == request.headers.end())
                {
                    request.headers["User-Agent"] = m_userAgent;
                }
                if (request.headers.find("Accept") == request.headers.end())
                {
                    request.headers["Accept"] = "*/*";
                }
                if (!request.body.empty() && request.headers.find("Content-Length") == request.headers.end())
                {
                    request.headers["Content-Length"] = std::to_string(request.body.size());
                }
                if (request.headers.find("Connection") == request.headers.end())
                {
                    request.headers["Connection"] = "close";
                }

                // Connect to server
                SocketAddr addr(url.host_.c_str(), url.port_);
                Socket socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                
                if (socket.invalid())
                {
                    LOG_ERROR("HttpClient: Failed to create socket");
                    return false;
                }
                
                if (!socket.connect(addr))
                {
                    LOG_ERROR("HttpClient: Failed to connect to %s:%d", url.host_.c_str(), url.port_);
                    socket.close();
                    return false;
                }

                // Send request
                std::ostringstream requestStream;
                requestStream << request.method << " " << url.path_ << url.query_ << " " << request.protocol << "\r\n";
                
                for (const auto& header : request.headers)
                {
                    requestStream << header.first << ": " << header.second << "\r\n";
                }
                requestStream << "\r\n";

                std::string requestStr = requestStream.str();
                int sent = socket.send(requestStr.c_str(), static_cast<int>(requestStr.size()));
                if (sent != static_cast<int>(requestStr.size()))
                {
                    LOG_ERROR("HttpClient: Failed to send request headers");
                    return false;
                }

                // Send body if present
                if (!request.body.empty())
                {
                    sent = socket.send(request.body.c_str(), static_cast<int>(request.body.size()));
                    if (sent != static_cast<int>(request.body.size()))
                    {
                        LOG_ERROR("HttpClient: Failed to send request body");
                        return false;
                    }
                }

                // Receive response
                return receiveResponse(socket, response);
            }

        protected:
            bool receiveResponse(Socket& socket, HttpClientResponse& response)
            {
                std::string buffer;
                char tempBuf[4096];
                
                // Read until we have complete headers
                std::string::size_type headerEnd = std::string::npos;
                while (headerEnd == std::string::npos)
                {
                    int received = socket.recv(tempBuf, sizeof(tempBuf));
                    if (received <= 0)
                    {
                        LOG_ERROR("HttpClient: Connection closed while reading headers");
                        return false;
                    }
                    buffer.append(tempBuf, received);
                    headerEnd = buffer.find("\r\n\r\n");
                }

                // Parse status line and headers
                if (!parseHeaders(buffer.substr(0, headerEnd + 4), response))
                {
                    return false;
                }

                // Get remaining data after headers
                std::string bodyBuffer = buffer.substr(headerEnd + 4);

                // Check if response uses chunked encoding
                auto transferEncoding = response.getHeader("Transfer-Encoding");
                if (transferEncoding.find("chunked") != std::string::npos)
                {
                    response.isChunked = true;
                    return receiveChunkedBody(socket, bodyBuffer, response);
                }

                // Check if we have Content-Length
                auto contentLengthStr = response.getHeader("Content-Length");
                if (!contentLengthStr.empty())
                {
                    size_t contentLength = std::stoul(contentLengthStr);
                    return receiveFixedBody(socket, bodyBuffer, contentLength, response);
                }

                // Read until connection closes
                response.body = bodyBuffer;
                while (true)
                {
                    int received = socket.recv(tempBuf, sizeof(tempBuf));
                    if (received <= 0)
                    {
                        break;
                    }
                    response.body.append(tempBuf, received);
                }

                if (response.onComplete)
                {
                    response.onComplete();
                }

                return true;
            }

            bool parseHeaders(const std::string& headerData, HttpClientResponse& response)
            {
                const char* ptr = headerData.c_str();
                const char* begin = ptr;

                // Parse status line: HTTP/1.1 200 OK
                while (*ptr && *ptr != ' ')
                {
                    ptr++;
                }
                if (*ptr != ' ')
                {
                    return false;
                }
                response.protocol.assign(begin, ptr);
                
                while (*ptr == ' ')
                {
                    ptr++;
                }

                // Status code
                begin = ptr;
                while (*ptr && *ptr != ' ' && *ptr != '\r')
                {
                    ptr++;
                }
                std::string codeStr(begin, ptr);
                response.code = std::atoi(codeStr.c_str());

                if (*ptr == ' ')
                {
                    ptr++;
                    begin = ptr;
                    while (*ptr && *ptr != '\r' && *ptr != '\n')
                    {
                        ptr++;
                    }
                    response.message.assign(begin, ptr);
                }

                if (*ptr == '\r')
                {
                    ptr++;
                }
                if (*ptr != '\n')
                {
                    return false;
                }
                ptr++;

                // Parse headers
                response.headers.clear();
                while (*ptr != '\r' && *ptr != '\n')
                {
                    // Name
                    begin = ptr;
                    while (*ptr && *ptr != ':')
                    {
                        ptr++;
                    }
                    if (*ptr != ':')
                    {
                        return false;
                    }
                    std::string name(begin, ptr);
                    ptr++;
                    
                    while (*ptr == ' ')
                    {
                        ptr++;
                    }

                    // Value
                    begin = ptr;
                    while (*ptr && *ptr != '\r' && *ptr != '\n')
                    {
                        ptr++;
                    }
                    response.headers[name] = std::string(begin, ptr);
                    
                    if (*ptr == '\r')
                    {
                        ptr++;
                    }
                    if (*ptr != '\n')
                    {
                        return false;
                    }
                    ptr++;
                }

                return true;
            }

            bool receiveFixedBody(Socket& socket, std::string& bodyBuffer, size_t contentLength, HttpClientResponse& response)
            {
                response.body = bodyBuffer;
                
                while (response.body.size() < contentLength)
                {
                    char tempBuf[4096];
                    int toRead = static_cast<int>(std::min(sizeof(tempBuf), contentLength - response.body.size()));
                    int received = socket.recv(tempBuf, toRead);
                    
                    if (received <= 0)
                    {
                        LOG_ERROR("HttpClient: Connection closed before receiving complete body");
                        return false;
                    }
                    
                    response.body.append(tempBuf, received);
                }

                if (response.onComplete)
                {
                    response.onComplete();
                }

                return true;
            }

            bool receiveChunkedBody(Socket& socket, std::string& buffer, HttpClientResponse& response)
            {
                // Parse chunked encoding: <size in hex>\r\n<data>\r\n
                while (true)
                {
                    // Read chunk size line
                    size_t lineEnd = buffer.find("\r\n");
                    while (lineEnd == std::string::npos)
                    {
                        char tempBuf[4096];
                        int received = socket.recv(tempBuf, sizeof(tempBuf));
                        if (received <= 0)
                        {
                            LOG_ERROR("HttpClient: Connection closed while reading chunk size");
                            return false;
                        }
                        buffer.append(tempBuf, received);
                        lineEnd = buffer.find("\r\n");
                    }

                    // Parse chunk size (hex)
                    std::string sizeStr = buffer.substr(0, lineEnd);
                    size_t chunkSize = std::stoul(sizeStr, nullptr, 16);
                    
                    buffer.erase(0, lineEnd + 2);  // Remove size line

                    if (chunkSize == 0)
                    {
                        // Last chunk - read trailing headers if any
                        while (buffer.find("\r\n") == 0)
                        {
                            buffer.erase(0, 2);
                        }
                        
                        if (response.onComplete)
                        {
                            response.onComplete();
                        }
                        return true;
                    }

                    // Read chunk data
                    while (buffer.size() < chunkSize + 2)  // +2 for trailing \r\n
                    {
                        char tempBuf[4096];
                        int received = socket.recv(tempBuf, sizeof(tempBuf));
                        if (received <= 0)
                        {
                            LOG_ERROR("HttpClient: Connection closed while reading chunk data");
                            return false;
                        }
                        buffer.append(tempBuf, received);
                    }

                    std::string chunkData = buffer.substr(0, chunkSize);
                    buffer.erase(0, chunkSize + 2);  // Remove chunk data and trailing \r\n

                    // Call chunk callback or append to body
                    if (response.chunkCallback)
                    {
                        response.chunkCallback(chunkData);
                    }
                    else
                    {
                        response.body.append(chunkData);
                    }
                }
            }
        };

    }  // namespace client
}  // namespace http
SOCKETSHPP_NS_END
