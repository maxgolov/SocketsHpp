// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/net/common/socket_tools.h>
#include <SocketsHpp/http/common/http_constants.h>

#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <BS_thread_pool.hpp>

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace server
    {

        using Reactor = net::utils::Reactor;
        using Socket = net::utils::Socket;
        using SocketAddr = net::utils::SocketAddr;
        using SocketParams = net::utils::SocketParams;

        // Import commonly used constants into server namespace for convenience
        using constants::CONTENT_TYPE;
        using constants::CONTENT_TYPE_TEXT;
        using constants::CONTENT_TYPE_BINARY;
        using constants::CONTENT_TYPE_SSE;
        using constants::CONTENT_TYPE_JSON;
        using constants::MCP_SESSION_ID;
        using constants::LAST_EVENT_ID;
        using constants::ACCESS_CONTROL_ALLOW_ORIGIN;
        using constants::ACCESS_CONTROL_ALLOW_METHODS;
        using constants::ACCESS_CONTROL_ALLOW_HEADERS;
        using constants::ACCESS_CONTROL_EXPOSE_HEADERS;
        using constants::ACCESS_CONTROL_MAX_AGE;

        // Session management with event history for Last-Event-ID support
        class SessionManager
        {
        private:
            struct SessionData
            {
                std::chrono::steady_clock::time_point lastAccess;
                std::vector<std::pair<std::string, std::string>> eventHistory; // <eventId, eventData>
                size_t maxHistorySize = 1000;
            };
            
            std::map<std::string, SessionData> m_sessions;
            std::mutex m_mutex;
            std::chrono::seconds m_sessionTimeout{3600}; // 1 hour default
            std::chrono::milliseconds m_historyDuration{300000}; // 5 minutes default
            size_t m_maxHistorySize = 1000;
            bool m_resumabilityEnabled = false;
            
        public:
            void enableResumability(bool enabled, std::chrono::milliseconds historyDuration = std::chrono::milliseconds(300000), size_t maxHistorySize = 1000)
            {
                m_resumabilityEnabled = enabled;
                m_historyDuration = historyDuration;
                m_maxHistorySize = maxHistorySize;
            }
            
            std::string createSession()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                
                // Generate session ID (simple implementation - production should use crypto-random)
                auto now = std::chrono::steady_clock::now();
                auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();
                
                std::ostringstream ss;
                ss << "session-" << timestamp << "-" << (rand() % 100000);
                std::string sessionId = ss.str();
                
                SessionData data;
                data.lastAccess = now;
                data.maxHistorySize = m_maxHistorySize;
                m_sessions[sessionId] = data;
                return sessionId;
            }
            
            bool validateSession(const std::string& sessionId)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_sessions.find(sessionId);
                if (it == m_sessions.end())
                {
                    return false;
                }
                
                // Check if session has expired
                auto now = std::chrono::steady_clock::now();
                if (now - it->second.lastAccess > m_sessionTimeout)
                {
                    m_sessions.erase(it);
                    return false;
                }
                
                // Update last access time
                it->second.lastAccess = now;
                return true;
            }
            
            bool terminateSession(const std::string& sessionId)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_sessions.erase(sessionId) > 0;
            }
            
            void setSessionTimeout(std::chrono::seconds timeout)
            {
                m_sessionTimeout = timeout;
            }
            
            /// @brief Add event to session history for Last-Event-ID support
            /// @param sessionId Session identifier
            /// @param eventId Event identifier
            /// @param eventData Event data (SSE formatted)
            void addEvent(const std::string& sessionId, const std::string& eventId, const std::string& eventData)
            {
                if (!m_resumabilityEnabled)
                {
                    return;
                }
                
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_sessions.find(sessionId);
                if (it == m_sessions.end())
                {
                    return;
                }
                
                auto& history = it->second.eventHistory;
                history.emplace_back(eventId, eventData);
                
                // Limit history size
                if (history.size() > it->second.maxHistorySize)
                {
                    history.erase(history.begin(), history.begin() + (history.size() - it->second.maxHistorySize));
                }
            }
            
            /// @brief Get events since a specific event ID
            /// @param sessionId Session identifier
            /// @param lastEventId Last event ID received by client
            /// @return Vector of events that occurred after lastEventId
            std::vector<std::string> getEventsSince(const std::string& sessionId, const std::string& lastEventId)
            {
                std::vector<std::string> events;
                
                if (!m_resumabilityEnabled)
                {
                    return events;
                }
                
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_sessions.find(sessionId);
                if (it == m_sessions.end())
                {
                    return events;
                }
                
                const auto& history = it->second.eventHistory;
                
                // If no lastEventId, return all recent events
                if (lastEventId.empty())
                {
                    for (const auto& event : history)
                    {
                        events.push_back(event.second);
                    }
                    return events;
                }
                
                // Find the position of lastEventId and return events after it
                bool found = false;
                for (const auto& event : history)
                {
                    if (found)
                    {
                        events.push_back(event.second);
                    }
                    else if (event.first == lastEventId)
                    {
                        found = true;
                    }
                }
                
                return events;
            }
            
            void cleanupExpiredSessions()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto now = std::chrono::steady_clock::now();
                
                for (auto it = m_sessions.begin(); it != m_sessions.end();)
                {
                    if (now - it->second.lastAccess > m_sessionTimeout)
                    {
                        it = m_sessions.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        };
        
        // CORS configuration
        struct CorsConfig
        {
            bool enabled = false;
            std::string allowOrigin = constants::CORS_ALLOW_ORIGIN_ALL;
            std::string allowMethods = constants::CORS_DEFAULT_METHODS;
            std::string allowHeaders = constants::CORS_DEFAULT_ALLOW_HEADERS;
            std::string exposeHeaders = constants::CORS_DEFAULT_EXPOSE_HEADERS;
            std::string maxAge = constants::CORS_DEFAULT_MAX_AGE;
        };

        // SSE (Server-Sent Events) helper class
        class SSEEvent
        {
        public:
            std::string event;
            std::string data;
            std::string id;
            int retry = -1;  // -1 means not set

            std::string format() const
            {
                std::ostringstream oss;
                if (!id.empty())
                {
                    oss << "id: " << id << "\n";
                }
                if (!event.empty())
                {
                    oss << "event: " << event << "\n";
                }
                if (retry >= 0)
                {
                    oss << "retry: " << retry << "\n";
                }
                // Split data by newlines for proper SSE formatting
                if (!data.empty())
                {
                    size_t start = 0;
                    size_t end = data.find('\n');
                    while (end != std::string::npos)
                    {
                        oss << "data: " << data.substr(start, end - start) << "\n";
                        start = end + 1;
                        end = data.find('\n', start);
                    }
                    oss << "data: " << data.substr(start) << "\n";
                }
                oss << "\n";  // Empty line terminates event
                return oss.str();
            }

            static SSEEvent message(const std::string& data, const std::string& id = "")
            {
                SSEEvent evt;
                evt.data = data;
                evt.id = id;
                return evt;
            }

            static SSEEvent custom(const std::string& event, const std::string& data, const std::string& id = "")
            {
                SSEEvent evt;
                evt.event = event;
                evt.data = data;
                evt.id = id;
                return evt;
            }
        };

        struct HttpRequest
        {
            std::string client;
            std::string method;
            std::string uri;
            std::string protocol;
            std::map<std::string, std::string> headers;
            std::string content;
            
            /// @brief Parse query parameters from URI
            /// @return Map of query parameter key-value pairs
            /// @note Performs URL decoding of values
            std::map<std::string, std::string> parse_query() const
            {
                std::map<std::string, std::string> params;
                
                // Find query string start
                size_t qpos = uri.find('?');
                if (qpos == std::string::npos)
                {
                    return params;
                }
                
                std::string query = uri.substr(qpos + 1);
                size_t start = 0;
                
                while (start < query.length())
                {
                    // Find key=value pair
                    size_t eq = query.find('=', start);
                    if (eq == std::string::npos)
                    {
                        break;
                    }
                    
                    size_t amp = query.find('&', eq);
                    size_t end = (amp == std::string::npos) ? query.length() : amp;
                    
                    std::string key = query.substr(start, eq - start);
                    std::string value = query.substr(eq + 1, end - eq - 1);
                    
                    // URL decode the value
                    std::string decoded;
                    decoded.reserve(value.length());
                    
                    for (size_t i = 0; i < value.length(); ++i)
                    {
                        if (value[i] == '%' && i + 2 < value.length())
                        {
                            // Decode %XX
                            char hex[3] = {value[i + 1], value[i + 2], 0};
                            char* end_ptr;
                            long ch = strtol(hex, &end_ptr, 16);
                            if (end_ptr == hex + 2)
                            {
                                decoded += static_cast<char>(ch);
                                i += 2;
                            }
                            else
                            {
                                decoded += value[i];
                            }
                        }
                        else if (value[i] == '+')
                        {
                            decoded += ' ';
                        }
                        else
                        {
                            decoded += value[i];
                        }
                    }
                    
                    params[key] = decoded;
                    start = (amp == std::string::npos) ? query.length() : amp + 1;
                }
                
                return params;
            }
            
            /// @brief Get accepted MIME types from Accept header
            /// @return Vector of MIME types sorted by quality factor
            std::vector<std::string> get_accepted_types() const
            {
                std::vector<std::string> types;
                
                auto it = headers.find("Accept");
                if (it == headers.end())
                {
                    return types;
                }
                
                std::string accept = it->second;
                size_t start = 0;
                
                while (start < accept.length())
                {
                    // Find next comma
                    size_t comma = accept.find(',', start);
                    size_t end = (comma == std::string::npos) ? accept.length() : comma;
                    
                    std::string type = accept.substr(start, end - start);
                    
                    // Trim whitespace
                    size_t first = type.find_first_not_of(" \t");
                    size_t last = type.find_last_not_of(" \t");
                    
                    if (first != std::string::npos && last != std::string::npos)
                    {
                        type = type.substr(first, last - first + 1);
                        
                        // Remove quality factor if present (e.g., ";q=0.8")
                        size_t semi = type.find(';');
                        if (semi != std::string::npos)
                        {
                            type = type.substr(0, semi);
                        }
                        
                        types.push_back(type);
                    }
                    
                    start = (comma == std::string::npos) ? accept.length() : comma + 1;
                }
                
                return types;
            }
            
            /// @brief Check if request accepts a specific MIME type
            /// @param mime_type The MIME type to check (e.g., "application/json")
            /// @return true if the type is accepted or no Accept header present
            bool accepts(const std::string& mime_type) const
            {
                auto it = headers.find("Accept");
                if (it == headers.end())
                {
                    return true;  // No Accept header = accept all
                }
                
                std::string accept = it->second;
                
                // Check for */* (accept all)
                if (accept.find("*/*") != std::string::npos)
                {
                    return true;
                }
                
                // Check for exact match
                if (accept.find(mime_type) != std::string::npos)
                {
                    return true;
                }
                
                // Check for wildcard match (e.g., "application/*")
                size_t slash = mime_type.find('/');
                if (slash != std::string::npos)
                {
                    std::string wildcard = mime_type.substr(0, slash + 1) + "*";
                    if (accept.find(wildcard) != std::string::npos)
                    {
                        return true;
                    }
                }
                
                return false;
            }
        };

        struct HttpResponse
        {
            int code;
            std::string message;
            std::map<std::string, std::string> headers;
            std::string body;
            
            // Streaming support
            bool streaming = false;
            bool useChunkedEncoding = false;
            std::function<std::string()> streamCallback;  // Returns chunk data, empty string = end
            std::function<void()> onStreamEnd;            // Called when stream completes
        };

        using CallbackFunction = std::function<int(HttpRequest const& request, HttpResponse& response)>;

        class HttpRequestCallback
        {
        protected:
            CallbackFunction callback = nullptr;

        public:
            HttpRequestCallback() {};

            HttpRequestCallback& operator=(HttpRequestCallback other)
            {
                callback = other.callback;
                return *this;
            };

            HttpRequestCallback(CallbackFunction func) : callback(func) {};

            HttpRequestCallback& operator=(CallbackFunction func)
            {
                callback = func;
                return (*this);
            }

            virtual int onHttpRequest(HttpRequest const& request, HttpResponse& response)
            {
                if (callback != nullptr)
                {
                    return callback(request, response);
                }
                return 0;
            };
        };

        // Simple HTTP server
        // Goals:
        //   - Support enough of HTTP to be used as a mock
        //   - Be flexible to allow creating various test scenarios
        // Out of scope:
        //   - Performance
        //   - Full support of RFC 7230-7237
        class HttpServer : private Reactor::SocketCallback
        {
        protected:
            struct Connection
            {
                Socket socket;
                std::string receiveBuffer;
                std::string sendBuffer;
                enum
                {
                    Idle,
                    ReceivingHeaders,
                    Sending100Continue,
                    ReceivingBody,
                    Processing,
                    ProcessingAsync,  // New: processing in thread pool
                    SendingHeaders,
                    SendingBody,
                    StreamingChunked,  // New: sending chunked data
                    Closing
                } state;
                size_t contentLength;
                bool keepalive;
                HttpRequest request;
                HttpResponse response;
                
                // Streaming state
                bool streamingActive = false;
                size_t chunksSent = 0;
            };

            std::string m_serverHost;
            bool allowKeepalive{ true };
            Reactor m_reactor;
            std::list<Socket> m_listeningSockets;

            class HttpRequestHandler : public std::pair<std::string, HttpRequestCallback*>
            {
            public:
                HttpRequestHandler(std::string key, HttpRequestCallback* value)
                {
                    first = key;
                    second = value;
                };

                HttpRequestHandler() : std::pair<std::string, HttpRequestCallback*>()
                {
                    first = "";
                    second = nullptr;
                };

                HttpRequestHandler& operator=(std::pair<std::string, HttpRequestCallback*> other)
                {
                    first = other.first;
                    second = other.second;
                    return (*this);
                };

                HttpRequestHandler& operator=(HttpRequestCallback& cb)
                {
                    second = &cb;
                    return (*this);
                };

                HttpRequestHandler& operator=(HttpRequestCallback* cb)
                {
                    second = cb;
                    return (*this);
                };
            };

            std::list<HttpRequestHandler> m_handlers;

            std::map<Socket, Connection> m_connections;
            std::mutex m_connectionsMutex;  // Protects m_connections
            std::optional<BS::thread_pool<>> m_threadPool;  // Optional thread pool for async request processing
            size_t m_maxRequestHeadersSize, m_maxRequestContentSize;
            
            SessionManager m_sessionManager;
            CorsConfig m_corsConfig;

        public:
            void setKeepalive(bool keepAlive) { allowKeepalive = keepAlive; }

            HttpServer()
                : m_serverHost("unnamed"),
                allowKeepalive(true),
                m_reactor(*this),
                m_maxRequestHeadersSize(8192),
                m_maxRequestContentSize(2 * 1024 * 1024) {};

            HttpServer(std::string serverHost, int port = 30000) : HttpServer()
            {
                std::ostringstream os;
                os << serverHost << ":" << port;
                setServerName(os.str());
                addListeningPort(port);
            };

            ~HttpServer()
            {
                for (auto& sock : m_listeningSockets)
                {
                    sock.close();
                }
            }

            void setRequestLimits(size_t maxRequestHeadersSize, size_t maxRequestContentSize)
            {
                m_maxRequestHeadersSize = maxRequestHeadersSize;
                m_maxRequestContentSize = maxRequestContentSize;
            }

            void setServerName(std::string const& name) { m_serverHost = name; }
            
            // Thread pool configuration
            void enableThreadPool(size_t numThreads = 0)
            {
                if (numThreads == 0)
                {
                    numThreads = std::thread::hardware_concurrency();
                    if (numThreads == 0) numThreads = 4;  // Fallback
                }
                m_threadPool.emplace(numThreads);
                LOG_INFO("HttpServer: Thread pool enabled with %zu threads", numThreads);
            }
            
            void disableThreadPool()
            {
                m_threadPool.reset();
                LOG_INFO("HttpServer: Thread pool disabled");
            }
            
            bool isThreadPoolEnabled() const
            {
                return m_threadPool.has_value();
            }
            
            // CORS configuration
            void enableCors(bool enabled = true) { m_corsConfig.enabled = enabled; }
            
            void setCorsOrigin(const std::string& origin) { m_corsConfig.allowOrigin = origin; }
            
            void setCorsHeaders(const std::string& allowHeaders, const std::string& exposeHeaders = "")
            {
                m_corsConfig.allowHeaders = allowHeaders;
                if (!exposeHeaders.empty())
                {
                    m_corsConfig.exposeHeaders = exposeHeaders;
                }
            }
            
            // Session management
            void setSessionTimeout(std::chrono::seconds timeout)
            {
                m_sessionManager.setSessionTimeout(timeout);
            }
            
            std::string createSession()
            {
                return m_sessionManager.createSession();
            }
            
            bool validateSession(const std::string& sessionId)
            {
                return m_sessionManager.validateSession(sessionId);
            }
            
            bool terminateSession(const std::string& sessionId)
            {
                return m_sessionManager.terminateSession(sessionId);
            }

            int addListeningPort(int port)
            {
                Socket socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                socket.setNonBlocking();
                socket.setReuseAddr();

                SocketAddr addr(static_cast<u_long>(0), port);
                socket.bind(addr);
                socket.getsockname(addr);

                socket.listen(10);
                m_listeningSockets.push_back(socket);
                m_reactor.addSocket(socket, Reactor::Acceptable);
                LOG_INFO("HttpServer: Listening on %s", addr.toString().c_str());

                return addr.port();
            }

            HttpRequestHandler& addHandler(const std::string& root, HttpRequestCallback& handler)
            {
                // No thread-safety here!
                m_handlers.push_back({ root, &handler });
                LOG_INFO("HttpServer: Added handler for %s", root.c_str());
                return m_handlers.back();
            }

            HttpRequestHandler& operator[](const std::string& root)
            {
                // No thread-safety here!
                m_handlers.push_back({ root, nullptr });
                LOG_INFO("HttpServer: Added handler for %s", root.c_str());
                return m_handlers.back();
            }

            HttpServer& operator+=(std::pair<const std::string&, HttpRequestCallback&> other)
            {
                LOG_INFO("HttpServer: Added handler for %s", other.first.c_str());
                m_handlers.push_back(HttpRequestHandler(other.first, &other.second));
                return (*this);
            };

            void start() { m_reactor.start(); }

            void stop() { m_reactor.stop(); }

        protected:
            virtual void onSocketAcceptable(Socket socket) override
            {
                LOG_TRACE("HttpServer: accepting socket fd=0x%llx", socket.m_sock);
                assert(std::find(m_listeningSockets.begin(), m_listeningSockets.end(), socket) !=
                    m_listeningSockets.end());

                Socket csocket;
                SocketAddr caddr;
                if (socket.accept(csocket, caddr))
                {
                    csocket.setNonBlocking();
                    {
                        std::lock_guard<std::mutex> lock(m_connectionsMutex);
                        Connection& conn = m_connections[csocket];
                        conn.socket = csocket;
                        conn.state = Connection::Idle;
                        conn.request.client = caddr.toString();
                    }
                    m_reactor.addSocket(csocket, Reactor::Readable | Reactor::Closed);
                    LOG_TRACE("HttpServer: [%s] accepted", caddr.toString().c_str());
                }
            }

            virtual void onSocketReadable(Socket socket) override
            {
                LOG_TRACE("HttpServer: reading socket fd=0x%llx", socket.m_sock);
                assert(std::find(m_listeningSockets.begin(), m_listeningSockets.end(), socket) ==
                    m_listeningSockets.end());

                std::lock_guard<std::mutex> lock(m_connectionsMutex);
                auto connIt = m_connections.find(socket);
                if (connIt == m_connections.end())
                {
                    return;
                }
                Connection& conn = connIt->second;

                char buffer[2048] = { 0 };
                int received = socket.recv(buffer, sizeof(buffer));
                LOG_TRACE("HttpServer: [%s] received %d", conn.request.client.c_str(), received);
                if (received <= 0)
                {
                    handleConnectionClosed(conn);
                    return;
                }
                conn.receiveBuffer.append(buffer, buffer + received);

                handleConnection(conn);
            }

            virtual void onSocketWritable(Socket socket) override
            {
                LOG_TRACE("HttpServer: writing socket fd=0x%llx", socket.m_sock);

                assert(std::find(m_listeningSockets.begin(), m_listeningSockets.end(), socket) ==
                    m_listeningSockets.end());

                std::lock_guard<std::mutex> lock(m_connectionsMutex);
                auto connIt = m_connections.find(socket);
                if (connIt == m_connections.end())
                {
                    return;
                }
                Connection& conn = connIt->second;

                if (!sendMore(conn))
                {
                    handleConnection(conn);
                }
            }

            virtual void onSocketClosed(Socket socket) override
            {
                LOG_TRACE("HttpServer: closing socket fd=0x%llx", socket.m_sock);
                assert(std::find(m_listeningSockets.begin(), m_listeningSockets.end(), socket) ==
                    m_listeningSockets.end());

                std::lock_guard<std::mutex> lock(m_connectionsMutex);
                auto connIt = m_connections.find(socket);
                if (connIt == m_connections.end())
                {
                    return;
                }
                Connection& conn = connIt->second;

                handleConnectionClosed(conn);
            }

            bool sendMore(Connection& conn)
            {
                if (conn.sendBuffer.empty())
                {
                    return false;
                }

                int sent = conn.socket.send(conn.sendBuffer.data(), static_cast<int>(conn.sendBuffer.size()));
                LOG_TRACE("HttpServer: [%s] sent %d", conn.request.client.c_str(), sent);
                if (sent < 0 && conn.socket.error() != Socket::ErrorWouldBlock)
                {
                    return true;
                }
                conn.sendBuffer.erase(0, sent);

                if (!conn.sendBuffer.empty())
                {
                    m_reactor.addSocket(conn.socket,
                        Reactor::Writable | Reactor::Closed);
                    return true;
                }

                return false;
            }

        protected:
            void handleConnectionClosed(Connection& conn)
            {
                LOG_TRACE("HttpServer: [%s] closed", conn.request.client.c_str());
                if (conn.state != Connection::Idle && conn.state != Connection::Closing)
                {
                    LOG_WARN("HttpServer: [%s] connection closed unexpectedly", conn.request.client.c_str());
                }
                m_reactor.removeSocket(conn.socket);
                auto connIt = m_connections.find(conn.socket);
                conn.socket.close();
                m_connections.erase(connIt);
            }

            void handleConnection(Connection& conn)
            {
                for (;;)
                {
                    if (conn.state == Connection::Idle)
                    {
                        conn.response.code = 0;
                        conn.state = Connection::ReceivingHeaders;
                        LOG_TRACE("HttpServer: [%s] receiving headers", conn.request.client.c_str());
                    }

                    if (conn.state == Connection::ReceivingHeaders)
                    {
                        bool lfOnly = false;
                        size_t ofs = conn.receiveBuffer.find("\r\n\r\n");
                        if (ofs == std::string::npos)
                        {
                            lfOnly = true;
                            ofs = conn.receiveBuffer.find("\n\n");
                        }
                        size_t headersLen = (ofs != std::string::npos) ? ofs : conn.receiveBuffer.length();
                        if (headersLen > m_maxRequestHeadersSize)
                        {
                            LOG_WARN("HttpServer: [%s] headers too long - %u", conn.request.client.c_str(),
                                static_cast<unsigned>(headersLen));
                            conn.response.code = 431;  // Request Header Fields Too Large
                            conn.keepalive = false;
                            conn.state = Connection::Processing;
                            continue;
                        }
                        if (ofs == std::string::npos)
                        {
                            return;
                        }

                        if (!parseHeaders(conn))
                        {
                            LOG_WARN("HttpServer: [%s] invalid headers", conn.request.client.c_str());
                            conn.response.code = 400;  // Bad Request
                            conn.keepalive = false;
                            conn.state = Connection::Processing;
                            continue;
                        }
                        LOG_INFO("HttpServer: [%s] %s %s %s", conn.request.client.c_str(),
                            conn.request.method.c_str(), conn.request.uri.c_str(),
                            conn.request.protocol.c_str());
                        conn.receiveBuffer.erase(0, ofs + (lfOnly ? 2 : 4));

                        conn.keepalive = (conn.request.protocol == constants::HTTP_1_1);
                        auto const connection = conn.request.headers.find(constants::CONNECTION);
                        if (connection != conn.request.headers.end())
                        {
                            if (equalsLowercased(connection->second, constants::CONNECTION_KEEP_ALIVE))
                            {
                                conn.keepalive = true;
                            }
                            else if (equalsLowercased(connection->second, constants::CONNECTION_CLOSE))
                            {
                                conn.keepalive = false;
                            }
                        }

                        auto const contentLength = conn.request.headers.find(constants::CONTENT_LENGTH);
                        if (contentLength != conn.request.headers.end())
                        {
                            conn.contentLength = atoi(contentLength->second.c_str());
                        }
                        else
                        {
                            conn.contentLength = 0;
                        }
                        if (conn.contentLength > m_maxRequestContentSize)
                        {
                            LOG_WARN("HttpServer: [%s] content too long - %u", conn.request.client.c_str(),
                                static_cast<unsigned>(conn.contentLength));
                            conn.response.code = 413;  // Payload Too Large
                            conn.keepalive = false;
                            conn.state = Connection::Processing;
                            continue;
                        }

                        auto const expect = conn.request.headers.find(constants::EXPECT);
                        if (expect != conn.request.headers.end() && conn.request.protocol == constants::HTTP_1_1)
                        {
                            if (!equalsLowercased(expect->second, constants::EXPECT_100_CONTINUE))
                            {
                                LOG_WARN("HttpServer: [%s] unknown expectation - %s", conn.request.client.c_str(),
                                    expect->second.c_str());
                                conn.response.code = 417;  // Expectation Failed
                                conn.keepalive = false;
                                conn.state = Connection::Processing;
                                continue;
                            }
                            conn.sendBuffer = "HTTP/1.1 100 Continue\r\n\r\n";
                            conn.state = Connection::Sending100Continue;
                            LOG_TRACE("HttpServer: [%s] sending \"100 Continue\"", conn.request.client.c_str());
                            continue;
                        }

                        conn.state = Connection::ReceivingBody;
                        LOG_TRACE("HttpServer: [%s] receiving body", conn.request.client.c_str());
                    }

                    if (conn.state == Connection::Sending100Continue)
                    {
                        if (sendMore(conn))
                        {
                            return;
                        }

                        conn.state = Connection::ReceivingBody;
                        LOG_TRACE("HttpServer: [%s] receiving body", conn.request.client.c_str());
                    }

                    if (conn.state == Connection::ReceivingBody)
                    {
                        if (conn.receiveBuffer.length() < conn.contentLength)
                        {
                            return;
                        }

                        if (conn.receiveBuffer.length() == conn.contentLength)
                        {
                            conn.request.content = std::move(conn.receiveBuffer);
                            conn.receiveBuffer.clear();
                        }
                        else
                        {
                            conn.request.content.assign(conn.receiveBuffer, 0, conn.contentLength);
                            conn.receiveBuffer.erase(0, conn.contentLength);
                        }

                        conn.state = Connection::Processing;
                        LOG_TRACE("HttpServer: [%s] processing request", conn.request.client.c_str());
                    }

                    if (conn.state == Connection::Processing)
                    {
                        // If thread pool is enabled, offload to worker thread
                        if (m_threadPool.has_value())
                        {
                            // Capture socket for async processing
                            Socket sock = conn.socket;
                            conn.state = Connection::ProcessingAsync;
                            LOG_TRACE("HttpServer: [%s] offloading to thread pool", conn.request.client.c_str());
                            
                            // Submit task to thread pool
                            m_threadPool->detach_task([this, sock]() {
                                // Process request in worker thread
                                {
                                    std::lock_guard<std::mutex> lock(m_connectionsMutex);
                                    auto it = m_connections.find(sock);
                                    if (it == m_connections.end())
                                    {
                                        return;  // Connection closed while queued
                                    }
                                    
                                    Connection& asyncConn = it->second;
                                    if (asyncConn.state != Connection::ProcessingAsync)
                                    {
                                        return;  // State changed, skip
                                    }
                                    
                                    processRequest(asyncConn);
                                    
                                    // Build response headers
                                    std::ostringstream os;
                                    os << asyncConn.request.protocol << ' ' << asyncConn.response.code << ' ' 
                                       << asyncConn.response.message << "\r\n";
                                    for (auto const& header : asyncConn.response.headers)
                                    {
                                        os << header.first << ": " << header.second << "\r\n";
                                    }
                                    os << "\r\n";
                                    
                                    asyncConn.sendBuffer = os.str();
                                    asyncConn.state = Connection::SendingHeaders;
                                }
                                
                                // Signal reactor that socket is ready to write
                                // This will trigger onSocketWritable on the reactor thread
                                m_reactor.addSocket(sock, Reactor::Writable | Reactor::Closed);
                            });
                            
                            // Return immediately - reactor will be notified when processing completes
                            return;
                        }
                        else
                        {
                            // Synchronous processing (original behavior)
                            processRequest(conn);
                        }

                        std::ostringstream os;
                        os << conn.request.protocol << ' ' << conn.response.code << ' ' << conn.response.message
                            << "\r\n";
                        for (auto const& header : conn.response.headers)
                        {
                            os << header.first << ": " << header.second << "\r\n";
                        }
                        os << "\r\n";

                        conn.sendBuffer = os.str();
                        conn.state = Connection::SendingHeaders;
                        LOG_TRACE("HttpServer: [%s] sending headers", conn.request.client.c_str());
                    }
                    
                    if (conn.state == Connection::ProcessingAsync)
                    {
                        // Still processing in thread pool, wait
                        return;
                    }

                    if (conn.state == Connection::SendingHeaders)
                    {
                        if (sendMore(conn))
                        {
                            return;
                        }

                        // Check if we're streaming
                        if (conn.streamingActive)
                        {
                            conn.state = Connection::StreamingChunked;
                            LOG_TRACE("HttpServer: [%s] starting chunked stream", conn.request.client.c_str());
                        }
                        else
                        {
                            conn.sendBuffer = std::move(conn.response.body);
                            conn.state = Connection::SendingBody;
                            LOG_TRACE("HttpServer: [%s] sending body", conn.request.client.c_str());
                        }
                    }

                    if (conn.state == Connection::SendingBody)
                    {
                        if (sendMore(conn))
                        {
                            return;
                        }

                        conn.keepalive &= allowKeepalive;

                        if (conn.keepalive)
                        {
                            m_reactor.addSocket(conn.socket,
                                Reactor::Readable | Reactor::Closed);
                            conn.state = Connection::Idle;
                            LOG_TRACE("HttpServer: [%s] idle (keep-alive)", conn.request.client.c_str());
                            if (conn.receiveBuffer.empty())
                            {
                                return;
                            }
                        }
                        else
                        {
                            conn.socket.shutdown(Socket::ShutdownSend);
                            m_reactor.addSocket(conn.socket, Reactor::Closed);
                            conn.state = Connection::Closing;
                            LOG_TRACE("HttpServer: [%s] closing", conn.request.client.c_str());
                        }
                    }
                    
                    if (conn.state == Connection::StreamingChunked)
                    {
                        // Get next chunk from callback
                        if (conn.response.streamCallback)
                        {
                            std::string chunkData = conn.response.streamCallback();
                            
                            if (chunkData.empty())
                            {
                                // End of stream - send final chunk
                                conn.sendBuffer = "0\r\n\r\n";  // Terminal chunk
                                LOG_TRACE("HttpServer: [%s] sending terminal chunk (sent %zu chunks)",
                                    conn.request.client.c_str(), conn.chunksSent);
                                    
                                if (sendMore(conn))
                                {
                                    return;
                                }
                                
                                // Call end callback if provided
                                if (conn.response.onStreamEnd)
                                {
                                    conn.response.onStreamEnd();
                                }
                                
                                conn.streamingActive = false;
                                conn.keepalive &= allowKeepalive;
                                
                                if (conn.keepalive)
                                {
                                    m_reactor.addSocket(conn.socket, Reactor::Readable | Reactor::Closed);
                                    conn.state = Connection::Idle;
                                    LOG_TRACE("HttpServer: [%s] stream ended, idle (keep-alive)", conn.request.client.c_str());
                                }
                                else
                                {
                                    conn.socket.shutdown(Socket::ShutdownSend);
                                    m_reactor.addSocket(conn.socket, Reactor::Closed);
                                    conn.state = Connection::Closing;
                                    LOG_TRACE("HttpServer: [%s] stream ended, closing", conn.request.client.c_str());
                                }
                            }
                            else
                            {
                                // Format as chunked data: <size in hex>\r\n<data>\r\n
                                std::ostringstream chunkHeader;
                                chunkHeader << std::hex << chunkData.size() << "\r\n";
                                conn.sendBuffer = chunkHeader.str() + chunkData + "\r\n";
                                conn.chunksSent++;
                                
                                LOG_TRACE("HttpServer: [%s] sending chunk #%zu (%zu bytes)",
                                    conn.request.client.c_str(), conn.chunksSent, chunkData.size());
                                
                                if (sendMore(conn))
                                {
                                    return;
                                }
                                
                                // Stay in streaming state - reactor will call us again when writable
                                m_reactor.addSocket(conn.socket, Reactor::Writable | Reactor::Closed);
                            }
                        }
                        else
                        {
                            // No callback - end stream
                            conn.sendBuffer = "0\r\n\r\n";
                            if (sendMore(conn))
                            {
                                return;
                            }
                            conn.streamingActive = false;
                            conn.state = Connection::Closing;
                        }
                    }

                    if (conn.state == Connection::Closing)
                    {
                        return;
                    }
                }
            }

            bool parseHeaders(Connection& conn)
            {
                // Method
                char const* begin = conn.receiveBuffer.c_str();
                char const* ptr = begin;
                while (*ptr && *ptr != ' ' && *ptr != '\r' && *ptr != '\n')
                {
                    ptr++;
                }
                if (*ptr != ' ')
                {
                    return false;
                }
                conn.request.method.assign(begin, ptr);
                while (*ptr == ' ')
                {
                    ptr++;
                }

                // URI
                begin = ptr;
                while (*ptr && *ptr != ' ' && *ptr != '\r' && *ptr != '\n')
                {
                    ptr++;
                }
                if (*ptr != ' ')
                {
                    return false;
                }
                conn.request.uri.assign(begin, ptr);
                while (*ptr == ' ')
                {
                    ptr++;
                }

                // Protocol
                begin = ptr;
                while (*ptr && *ptr != ' ' && *ptr != '\r' && *ptr != '\n')
                {
                    ptr++;
                }
                if (*ptr != '\r' && *ptr != '\n')
                {
                    return false;
                }
                conn.request.protocol.assign(begin, ptr);
                if (*ptr == '\r')
                {
                    ptr++;
                }
                if (*ptr != '\n')
                {
                    return false;
                }
                ptr++;

                // Headers
                conn.request.headers.clear();
                while (*ptr != '\r' && *ptr != '\n')
                {
                    // Name
                    begin = ptr;
                    while (*ptr && *ptr != ':' && *ptr != ' ' && *ptr != '\r' && *ptr != '\n')
                    {
                        ptr++;
                    }
                    if (*ptr != ':')
                    {
                        return false;
                    }
                    std::string name = normalizeHeaderName(begin, ptr);
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
                    conn.request.headers[name] = std::string(begin, ptr);
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

                if (*ptr == '\r')
                {
                    ptr++;
                }
                if (*ptr != '\n')
                {
                    return false;
                }
                ptr++;

                return true;
            }

            static bool equalsLowercased(std::string const& str, char const* mask)
            {
                char const* ptr = str.c_str();
                while (*ptr && *mask && ::tolower(*ptr) == *mask)
                {
                    ptr++;
                    mask++;
                }
                return !*ptr && !*mask;
            }

            static std::string normalizeHeaderName(char const* begin, char const* end)
            {
                std::string result(begin, end);
                bool first = true;
                for (char& ch : result)
                {
                    if (first)
                    {
                        ch = static_cast<char>(::toupper(ch));
                        first = false;
                    }
                    else if (ch == '-')
                    {
                        first = true;
                    }
                    else
                    {
                        ch = static_cast<char>(::tolower(ch));
                    }
                }
                return result;
            }

            void processRequest(Connection& conn)
            {
                conn.response.message.clear();
                conn.response.headers.clear();
                conn.response.body.clear();
                conn.response.streaming = false;
                conn.response.useChunkedEncoding = false;
                conn.response.streamCallback = nullptr;

                // Store original method for HEAD handling
                std::string originalMethod = conn.request.method;
                bool isHeadRequest = (conn.request.method == "HEAD");
                
                // Treat HEAD as GET for processing, but skip body in response
                if (isHeadRequest)
                {
                    conn.request.method = "GET";
                }
                
                // Handle OPTIONS method for CORS preflight
                if (originalMethod == "OPTIONS")
                {
                    if (m_corsConfig.enabled)
                    {
                        conn.response.code = 204;  // No Content
                        conn.response.message = "No Content";
                    }
                    else
                    {
                        conn.response.code = 405;  // Method Not Allowed
                        conn.response.message = "Method Not Allowed";
                    }
                }
                // Handle DELETE method for session termination
                else if (originalMethod == "DELETE")
                {
                    // Check for session ID in headers
                    auto sessionIt = conn.request.headers.find(MCP_SESSION_ID);
                    if (sessionIt != conn.request.headers.end())
                    {
                        if (m_sessionManager.terminateSession(sessionIt->second))
                        {
                            conn.response.code = 200;  // OK
                            conn.response.message = "Session terminated";
                        }
                        else
                        {
                            conn.response.code = 404;  // Not Found
                            conn.response.message = "Session not found";
                        }
                    }
                    else
                    {
                        conn.response.code = 400;  // Bad Request
                        conn.response.message = "Missing session ID";
                    }
                }
                // Handle PUT method - route to handlers like POST
                else if (originalMethod == "PUT" && conn.response.code == 0)
                {
                    conn.response.code = 404;  // Not Found
                    for (auto& handler : m_handlers)
                    {
                        if (conn.request.uri.length() >= handler.first.length() &&
                            strncmp(conn.request.uri.c_str(), handler.first.c_str(), handler.first.length()) == 0)
                        {
                            LOG_TRACE("HttpServer: [%s] using handler for %s (PUT)", conn.request.client.c_str(),
                                handler.first.c_str());
                            int result = handler.second->onHttpRequest(conn.request, conn.response);
                            if (result != 0)
                            {
                                conn.response.code = result;
                                break;
                            }
                        }
                    }
                    
                    // Restore original method for response
                    conn.request.method = originalMethod;
                }
                else if (conn.response.code == 0)
                {
                    conn.response.code = 404;  // Not Found
                    for (auto& handler : m_handlers)
                    {
                        if (conn.request.uri.length() >= handler.first.length() &&
                            strncmp(conn.request.uri.c_str(), handler.first.c_str(), handler.first.length()) == 0)
                        {
                            LOG_TRACE("HttpServer: [%s] using handler for %s", conn.request.client.c_str(),
                                handler.first.c_str());
                            // auto callback = handler.second; // Bazel gets mad at this unused
                            // var, uncomment when using
                            int result = handler.second->onHttpRequest(conn.request, conn.response);
                            if (result != 0)
                            {
                                conn.response.code = result;
                                break;
                            }
                        }
                    }

                    if (conn.response.code == -1)
                    {
                        LOG_TRACE("HttpServer: [%s] closing by request", conn.request.client.c_str());
                        handleConnectionClosed(conn);
                    }
                }

                if (conn.response.message.empty())
                {
                    conn.response.message = getDefaultResponseMessage(conn.response.code);
                }

                conn.response.headers[constants::HOST] = m_serverHost;
                conn.response.headers[constants::CONNECTION] = (conn.keepalive ? constants::CONNECTION_KEEP_ALIVE : constants::CONNECTION_CLOSE);
                conn.response.headers[constants::DATE] = formatTimestamp(time(nullptr));
                
                // Add CORS headers if enabled
                if (m_corsConfig.enabled)
                {
                    conn.response.headers[ACCESS_CONTROL_ALLOW_ORIGIN] = m_corsConfig.allowOrigin;
                    conn.response.headers[ACCESS_CONTROL_ALLOW_METHODS] = m_corsConfig.allowMethods;
                    conn.response.headers[ACCESS_CONTROL_ALLOW_HEADERS] = m_corsConfig.allowHeaders;
                    conn.response.headers[ACCESS_CONTROL_EXPOSE_HEADERS] = m_corsConfig.exposeHeaders;
                    
                    if (conn.request.method == "OPTIONS")
                    {
                        conn.response.headers[ACCESS_CONTROL_MAX_AGE] = m_corsConfig.maxAge;
                    }
                }
                
                // Handle streaming vs regular response
                if (conn.response.streaming && conn.response.streamCallback)
                {
                    conn.streamingActive = true;
                    conn.chunksSent = 0;
                    
                    // Use chunked encoding for streaming
                    if (conn.response.useChunkedEncoding || conn.request.protocol == "HTTP/1.1")
                    {
                        conn.response.headers[constants::TRANSFER_ENCODING] = constants::TRANSFER_ENCODING_CHUNKED;
                        conn.response.headers.erase(constants::CONTENT_LENGTH);
                    }
                    
                    // SSE-specific headers
                    if (conn.response.headers[CONTENT_TYPE] == CONTENT_TYPE_SSE)
                    {
                        conn.response.headers[constants::CACHE_CONTROL] = constants::CACHE_CONTROL_NO_CACHE;
                        conn.response.headers[constants::X_ACCEL_BUFFERING] = "no";  // Disable nginx buffering
                        conn.keepalive = true;  // Keep connection alive for SSE
                    }
                }
                else
                {
                    // For HEAD requests, calculate Content-Length but clear body
                    if (isHeadRequest)
                    {
                        conn.response.headers[constants::CONTENT_LENGTH] = std::to_string(conn.response.body.size());
                        conn.response.body.clear();
                    }
                    else
                    {
                        conn.response.headers[constants::CONTENT_LENGTH] = std::to_string(conn.response.body.size());
                    }
                }
                
                // Restore original method
                conn.request.method = originalMethod;
            }

            static std::string formatTimestamp(time_t time)
            {
                tm tm;
#ifdef _WIN32
                gmtime_s(&tm, &time);
#else
                gmtime_r(&time, &tm);
#endif
                char buf[32];
                strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
                return buf;
            }

        public:
            static char const* getDefaultResponseMessage(int code)
            {
                switch (code)
                {
                    // *INDENT-OFF*
                case 100:
                    return "Continue";
                case 101:
                    return "Switching Protocols";
                case 200:
                    return "OK";
                case 201:
                    return "Created";
                case 202:
                    return "Accepted";
                case 203:
                    return "Non-Authoritative Information";
                case 204:
                    return "No Content";
                case 205:
                    return "Reset Content";
                case 206:
                    return "Partial Content";
                case 300:
                    return "Multiple Choices";
                case 301:
                    return "Moved Permanently";
                case 302:
                    return "Found";
                case 303:
                    return "See Other";
                case 304:
                    return "Not Modified";
                case 305:
                    return "Use Proxy";
                case 306:
                    return "Switch Proxy";
                case 307:
                    return "Temporary Redirect";
                case 308:
                    return "Permanent Redirect";
                case 400:
                    return "Bad Request";
                case 401:
                    return "Unauthorized";
                case 402:
                    return "Payment Required";
                case 403:
                    return "Forbidden";
                case 404:
                    return "Not Found";
                case 405:
                    return "Method Not Allowed";
                case 406:
                    return "Not Acceptable";
                case 407:
                    return "Proxy Authentication Required";
                case 408:
                    return "Request Timeout";
                case 409:
                    return "Conflict";
                case 410:
                    return "Gone";
                case 411:
                    return "Length Required";
                case 412:
                    return "Precondition Failed";
                case 413:
                    return "Payload Too Large";
                case 414:
                    return "URI Too Long";
                case 415:
                    return "Unsupported Media Type";
                case 416:
                    return "Range Not Satisfiable";
                case 417:
                    return "Expectation Failed";
                case 421:
                    return "Misdirected Request";
                case 426:
                    return "Upgrade Required";
                case 428:
                    return "Precondition Required";
                case 429:
                    return "Too Many Requests";
                case 431:
                    return "Request Header Fields Too Large";
                case 500:
                    return "Internal Server Error";
                case 501:
                    return "Not Implemented";
                case 502:
                    return "Bad Gateway";
                case 503:
                    return "Service Unavailable";
                case 504:
                    return "Gateway Timeout";
                case 505:
                    return "HTTP Version Not Supported";
                case 506:
                    return "Variant Also Negotiates";
                case 510:
                    return "Not Extended";
                case 511:
                    return "Network Authentication Required";
                default:
                    return "???";
                    // *INDENT-ON*
                }
            }
        };
    }
}
SOCKETSHPP_NS_END
