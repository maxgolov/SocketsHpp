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
#include <random>
#include <unordered_map>
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

        // Case-insensitive string comparator for HTTP headers
        struct CaseInsensitiveCompare
        {
            bool operator()(const std::string& lhs, const std::string& rhs) const
            {
                return std::lexicographical_compare(
                    lhs.begin(), lhs.end(),
                    rhs.begin(), rhs.end(),
                    [](unsigned char a, unsigned char b) {
                        return std::tolower(a) < std::tolower(b);
                    }
                );
            }
        };

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
                size_t maxHistorySize = config::DEFAULT_MAX_HISTORY_SIZE;
            };

            std::map<std::string, SessionData> m_sessions;
            std::mutex m_mutex;
            std::chrono::seconds m_sessionTimeout{config::DEFAULT_SESSION_TIMEOUT_SECONDS};
            std::chrono::milliseconds m_historyDuration{config::DEFAULT_HISTORY_DURATION_MS};
            size_t m_maxHistorySize = config::DEFAULT_MAX_HISTORY_SIZE;
            size_t m_maxSessions = config::DEFAULT_MAX_SESSIONS;
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

                // Enforce session limit
                if (m_sessions.size() >= m_maxSessions)
                {
                    // Clean up expired sessions first
                    cleanupExpiredSessionsLocked();

                    // If still at limit after cleanup, reject new session
                    if (m_sessions.size() >= m_maxSessions)
                    {
                        throw std::runtime_error("Maximum session limit reached (" +
                            std::to_string(m_maxSessions) + ")");
                    }
                }

                // Generate cryptographically secure session ID
                auto now = std::chrono::steady_clock::now();
                auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();

                // Use random_device for cryptographic randomness
                std::random_device rd;
                std::mt19937_64 gen(rd());
                std::uniform_int_distribution<uint64_t> dis;

                std::ostringstream ss;
                ss << "session-" << std::hex << timestamp << "-"
                   << dis(gen) << "-" << dis(gen);
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
                cleanupExpiredSessionsLocked();
            }

            /// @brief Set maximum number of sessions allowed
            void setMaxSessions(size_t maxSessions)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_maxSessions = maxSessions;
            }

            /// @brief Get current session count
            size_t getSessionCount() const
            {
                std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
                return m_sessions.size();
            }

        private:
            /// @brief Internal cleanup without acquiring lock (must be called with lock held)
            void cleanupExpiredSessionsLocked()
            {
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

        /// @brief Securely decode URL-encoded string
        /// @param encoded URL-encoded string
        /// @param maxLength Maximum allowed length after decoding
        /// @return Decoded string
        /// @throws std::invalid_argument on invalid encoding or length exceeded
        inline std::string urlDecode(const std::string& encoded, size_t maxLength = config::MAX_QUERY_VALUE_LENGTH)
        {
            std::string decoded;
            decoded.reserve(encoded.length());

            for (size_t i = 0; i < encoded.length(); ++i)
            {
                if (decoded.length() >= maxLength)
                {
                    throw std::invalid_argument("URL-decoded value exceeds maximum length");
                }

                if (encoded[i] == '%')
                {
                    // Need at least 2 more characters
                    if (i + 2 >= encoded.length())
                    {
                        throw std::invalid_argument("Invalid URL encoding: incomplete % sequence");
                    }

                    // Validate hex characters
                    char hex1 = encoded[i + 1];
                    char hex2 = encoded[i + 2];

                    if (!std::isxdigit(static_cast<unsigned char>(hex1)) ||
                        !std::isxdigit(static_cast<unsigned char>(hex2)))
                    {
                        throw std::invalid_argument("Invalid URL encoding: non-hex characters after %");
                    }

                    // Decode hex
                    char hex[3] = {hex1, hex2, 0};
                    unsigned long val = std::strtoul(hex, nullptr, 16);

                    // Reject null bytes (security risk)
                    if (val == 0)
                    {
                        throw std::invalid_argument("Invalid URL encoding: null byte (%00) not allowed");
                    }

                    // Reject control characters (0x01-0x1F, 0x7F-0x9F)
                    if (val < 0x20 || (val >= 0x7F && val <= 0x9F))
                    {
                        throw std::invalid_argument("Invalid URL encoding: control character not allowed");
                    }

                    decoded += static_cast<char>(val);
                    i += 2;
                }
                else if (encoded[i] == '+')
                {
                    decoded += ' ';
                }
                else
                {
                    decoded += encoded[i];
                }
            }

            return decoded;
        }

        struct HttpRequest
        {
            std::string client;
            std::string method;
            std::string uri;
            std::string protocol;
            std::map<std::string, std::string> headers;
            std::string content;

            // Convenience methods for header access

            /// @brief Normalize header name to Title-Case format
            /// @param name Header name in any case
            /// @return Normalized header name
            static std::string normalize_header_name(const std::string& name)
            {
                std::string result = name;
                bool first = true;
                for (char& ch : result)
                {
                    if (first)
                    {
                        ch = static_cast<char>(::toupper(static_cast<unsigned char>(ch)));
                        first = false;
                    }
                    else if (ch == '-')
                    {
                        first = true;
                    }
                    else
                    {
                        ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
                    }
                }
                return result;
            }

            /// @brief Check if header exists (case-insensitive)
            /// @param name Header name (any case)
            /// @return true if header exists
            bool has_header(const std::string& name) const
            {
                return headers.find(normalize_header_name(name)) != headers.end();
            }

            /// @brief Get header value (case-insensitive)
            /// @param name Header name (any case)
            /// @return Header value, or empty string if not found
            std::string get_header_value(const std::string& name) const
            {
                auto it = headers.find(normalize_header_name(name));
                return (it != headers.end()) ? it->second : "";
            }
            
            /// @brief Parse query parameters from URI with security validation
            /// @return Map of query parameter key-value pairs
            /// @throws std::invalid_argument on invalid encoding or exceeded limits
            /// @note Performs URL decoding and validates all inputs
            std::map<std::string, std::string> parse_query() const
            {
                std::map<std::string, std::string> params;

                // Find query string start
                size_t qpos = uri.find('?');
                if (qpos == std::string::npos)
                {
                    return params;
                }

                // Strip fragment if present (# should not be sent by client, but handle it)
                std::string query = uri.substr(qpos + 1);
                size_t fragment = query.find('#');
                if (fragment != std::string::npos)
                {
                    query = query.substr(0, fragment);
                }

                size_t start = 0;
                size_t paramCount = 0;

                while (start < query.length())
                {
                    // Enforce parameter count limit
                    if (paramCount >= config::MAX_QUERY_PARAMS)
                    {
                        throw std::invalid_argument("Too many query parameters (max: " +
                            std::to_string(config::MAX_QUERY_PARAMS) + ")");
                    }

                    // Find next key=value pair
                    size_t eq = query.find('=', start);
                    if (eq == std::string::npos)
                    {
                        // No more '=' found, could be trailing param without value
                        break;
                    }

                    size_t amp = query.find('&', eq);
                    size_t end = (amp == std::string::npos) ? query.length() : amp;

                    // Extract and validate key
                    std::string key = query.substr(start, eq - start);

                    if (key.empty())
                    {
                        throw std::invalid_argument("Empty query parameter key");
                    }

                    if (key.length() > config::MAX_QUERY_KEY_LENGTH)
                    {
                        throw std::invalid_argument("Query parameter key too long (max: " +
                            std::to_string(config::MAX_QUERY_KEY_LENGTH) + ")");
                    }

                    // Validate key contains only allowed characters (alphanumeric, _, -, .)
                    for (char c : key)
                    {
                        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.')
                        {
                            throw std::invalid_argument("Invalid character in query parameter key: '" +
                                std::string(1, c) + "'");
                        }
                    }

                    // Extract value
                    std::string value = query.substr(eq + 1, end - eq - 1);

                    if (value.length() > config::MAX_QUERY_VALUE_LENGTH)
                    {
                        throw std::invalid_argument("Query parameter value too long (max: " +
                            std::to_string(config::MAX_QUERY_VALUE_LENGTH) + ")");
                    }

                    // URL decode the value (validates encoding)
                    std::string decoded = urlDecode(value, config::MAX_QUERY_VALUE_LENGTH);

                    // Check for duplicate keys
                    if (params.find(key) != params.end())
                    {
                        LOG_WARN("Duplicate query parameter '%s' - overwriting previous value", key.c_str());
                    }

                    params[key] = decoded;
                    paramCount++;

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

            // Convenience methods for API consistency

            /// @brief Set HTTP status code
            /// @param statusCode HTTP status code (e.g., 200, 404, 500)
            /// @param statusMessage Optional custom message (defaults to standard message)
            void set_status(int statusCode, const std::string& statusMessage = "")
            {
                code = statusCode;
                message = statusMessage;  // Set message directly; will be filled with default if empty during processing
            }

            /// @brief Set response header (case-insensitive, normalized to Title-Case)
            /// @param name Header name (any case)
            /// @param value Header value
            void set_header(const std::string& name, const std::string& value)
            {
                headers[HttpRequest::normalize_header_name(name)] = value;
            }

            /// @brief Set response content with optional content type
            /// @param content Response body content
            /// @param contentType MIME type (defaults to text/plain)
            void set_content(const std::string& content, const std::string& contentType = CONTENT_TYPE_TEXT)
            {
                body = content;
                headers[constants::CONTENT_TYPE] = contentType;
            }

            /// @brief Send response body (convenience alias for set_content)
            /// @param content Response body content
            void send(const std::string& content)
            {
                body = content;
            }

            /// @brief Setup streaming response with chunked encoding
            /// @param callback Function that returns chunk data (empty string = end of stream)
            /// @param onEnd Optional callback when stream completes
            void send_chunk_stream(std::function<std::string()> callback, std::function<void()> onEnd = nullptr)
            {
                streaming = true;
                useChunkedEncoding = true;
                streamCallback = std::move(callback);
                onStreamEnd = std::move(onEnd);
            }

            /// @brief Send a single chunk (for manual chunked streaming)
            /// @param chunk Chunk data to send
            /// @note This is used internally; for setting up streaming use send_chunk_stream
            void send_chunk(const std::string& chunk)
            {
                // This is a placeholder for API compatibility
                // Actual chunking is handled by the server's streaming mechanism
                if (chunk.empty())
                {
                    streaming = false;
                }
                else
                {
                    body += chunk;
                }
            }
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
            size_t m_maxSessions;  // Maximum allowed sessions

            SessionManager m_sessionManager;
            CorsConfig m_corsConfig;

        public:
            void setKeepalive(bool keepAlive) { allowKeepalive = keepAlive; }

            HttpServer()
                : m_serverHost("unnamed"),
                allowKeepalive(true),
                m_reactor(*this),
                m_maxRequestHeadersSize(config::MAX_HTTP_HEADER_SIZE),
                m_maxRequestContentSize(config::MAX_HTTP_BODY_SIZE),
                m_maxSessions(config::DEFAULT_MAX_SESSIONS) {};

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
                if (socket.bind(addr) != 0)
                {
                    int err = socket.error();
                    throw std::runtime_error("Failed to bind to port " + std::to_string(port) +
                        ", error: " + std::to_string(err));
                }

                if (!socket.getsockname(addr))
                {
                    throw std::runtime_error("Failed to get socket name after bind");
                }

                if (!socket.listen(config::SOCKET_LISTEN_BACKLOG))
                {
                    int err = socket.error();
                    throw std::runtime_error("Failed to listen on port " + std::to_string(port) +
                        ", error: " + std::to_string(err));
                }

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

            /// @brief Register a route with a handler function (convenience method)
            /// @param path URI prefix to match
            /// @param handler Callback function to handle requests
            /// @return Reference to the created handler
            HttpRequestHandler& route(const std::string& path, CallbackFunction handler)
            {
                m_handlers.push_back({ path, nullptr });
                auto& handlerRef = m_handlers.back();
                handlerRef.second = new HttpRequestCallback(handler);
                LOG_INFO("HttpServer: Added route for %s", path.c_str());
                return handlerRef;
            }

            /// @brief Set maximum request content size (convenience alias)
            /// @param maxSize Maximum allowed content size in bytes
            void setMaxRequestContentSize(size_t maxSize)
            {
                m_maxRequestContentSize = maxSize;
            }

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

                char buffer[config::HTTP_RECV_BUFFER_SIZE] = { 0 };
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

                // Validate method length
                size_t methodLen = ptr - begin;
                if (methodLen == 0 || methodLen > config::MAX_METHOD_LENGTH)
                {
                    LOG_WARN("HTTP method length invalid: %zu", methodLen);
                    return false;
                }

                conn.request.method.assign(begin, ptr);

                // Validate method is in whitelist
                static const char* allowedMethods[] = {
                    "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH", "TRACE", "CONNECT"
                };
                bool validMethod = false;
                for (const char* allowed : allowedMethods)
                {
                    if (conn.request.method == allowed)
                    {
                        validMethod = true;
                        break;
                    }
                }
                if (!validMethod)
                {
                    LOG_WARN("Invalid HTTP method: %s", conn.request.method.c_str());
                    return false;
                }

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

                // Validate URI length
                size_t uriLen = ptr - begin;
                if (uriLen == 0 || uriLen > config::MAX_URI_LENGTH)
                {
                    LOG_WARN("HTTP URI length invalid: %zu (max: %zu)", uriLen, config::MAX_URI_LENGTH);
                    return false;
                }

                conn.request.uri.assign(begin, ptr);

                // Validate URI doesn't contain control characters
                for (char c : conn.request.uri)
                {
                    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
                    {
                        LOG_WARN("HTTP URI contains control character: 0x%02X", static_cast<unsigned char>(c));
                        return false;
                    }
                }

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

                // Validate protocol length
                size_t protoLen = ptr - begin;
                if (protoLen == 0 || protoLen > config::MAX_PROTOCOL_LENGTH)
                {
                    LOG_WARN("HTTP protocol length invalid: %zu", protoLen);
                    return false;
                }

                conn.request.protocol.assign(begin, ptr);

                // Validate protocol format (HTTP/x.y)
                if (conn.request.protocol.substr(0, 5) != "HTTP/" ||
                    conn.request.protocol.length() < 8)
                {
                    LOG_WARN("Invalid HTTP protocol: %s", conn.request.protocol.c_str());
                    return false;
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

                    // Validate header name length
                    size_t nameLen = ptr - begin;
                    if (nameLen == 0 || nameLen > config::MAX_HEADER_NAME_LENGTH)
                    {
                        LOG_WARN("HTTP header name length invalid: %zu", nameLen);
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

                    // Validate header value length
                    size_t valueLen = ptr - begin;
                    if (valueLen > config::MAX_HEADER_VALUE_LENGTH)
                    {
                        LOG_WARN("HTTP header value length invalid: %zu", valueLen);
                        return false;
                    }

                    // Validate header value doesn't contain control characters (except tab)
                    for (const char* p = begin; p < ptr; ++p)
                    {
                        unsigned char c = static_cast<unsigned char>(*p);
                        if (c < 0x20 && c != '\t')  // Allow tab (0x09) but not other control chars
                        {
                            LOG_WARN("HTTP header value contains control character: 0x%02X", c);
                            return false;
                        }
                        if (c == 0x7F)  // DEL character
                        {
                            LOG_WARN("HTTP header value contains DEL character");
                            return false;
                        }
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
