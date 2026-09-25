// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/net/common/socket_tools.h>
#include <SocketsHpp/http/common/http_constants.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <cctype>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
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
        using ScopedSocket = net::utils::ScopedSocket;
        using SocketParams = net::utils::SocketParams;

        /// @brief Case-insensitive (ASCII) "less than" comparator for header names.
        /// @note Usable as the Compare argument of std::map; HttpRequest::headers
        ///       does not use it (it stores Title-Case-normalized names instead).
        struct CaseInsensitiveCompare
        {
            /// @brief Compare two strings lexicographically, ignoring ASCII case.
            /// @return true if @p lhs orders before @p rhs.
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

        /// @brief Thread-safe registry of session IDs with idle expiry and an optional
        ///        per-session event history for SSE Last-Event-ID resumption.
        /// @note All public members lock an internal mutex and may be called from
        ///       any thread. Sessions expire after setSessionTimeout() of inactivity
        ///       (validateSession() refreshes the timer); expired sessions are purged
        ///       lazily by validateSession(), cleanupExpiredSessions(), or when
        ///       createSession() hits the session limit.
        class SessionManager
        {
        private:
            struct StoredEvent
            {
                std::string id;
                std::string data;
                std::chrono::steady_clock::time_point time;
            };

            struct SessionData
            {
                std::chrono::steady_clock::time_point lastAccess;
                std::deque<StoredEvent> eventHistory;  // oldest first
            };

            /// Drop events older than the history duration or beyond the size limit.
            /// Caller holds m_mutex.
            void pruneHistoryLocked(std::deque<StoredEvent>& history) const
            {
                const auto cutoff = std::chrono::steady_clock::now() - m_historyDuration;
                while (!history.empty() && (history.size() > m_maxHistorySize || history.front().time < cutoff))
                {
                    history.pop_front();
                }
            }

            std::map<std::string, SessionData> m_sessions;
            std::mutex m_mutex;
            std::chrono::seconds m_sessionTimeout{config::DEFAULT_SESSION_TIMEOUT_SECONDS};
            std::chrono::milliseconds m_historyDuration{config::DEFAULT_HISTORY_DURATION_MS};
            size_t m_maxHistorySize = config::DEFAULT_MAX_HISTORY_SIZE;
            size_t m_maxSessions = config::DEFAULT_MAX_SESSIONS;
            bool m_resumabilityEnabled = false;
            
        public:
            /// @brief Turn event history recording (addEvent()/getEventsSince()) on or off.
            /// @param enabled When false, addEvent() is a no-op and getEventsSince()
            ///        returns nothing.
            /// @param historyDuration How long events are kept (older ones are dropped).
            /// @param maxHistorySize Events kept per session (oldest dropped first).
            void enableResumability(bool enabled, std::chrono::milliseconds historyDuration = std::chrono::milliseconds(300000), size_t maxHistorySize = 1000)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_resumabilityEnabled = enabled;
                m_historyDuration = historyDuration;
                m_maxHistorySize = maxHistorySize;
            }
            
            /// @brief Create a new session with a fresh random ID (see generateSessionId()).
            /// @return The new session ID.
            /// @throws std::runtime_error if the session limit (setMaxSessions()) is
            ///         still reached after purging expired sessions.
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

                auto now = std::chrono::steady_clock::now();
                std::string sessionId;
                do
                {
                    sessionId = generateSessionId();
                } while (m_sessions.find(sessionId) != m_sessions.end());

                SessionData data;
                data.lastAccess = now;
                m_sessions[sessionId] = data;
                return sessionId;
            }
            
            /// @brief Check that a session exists and has not expired, and refresh
            ///        its last-access time.
            /// @param sessionId Session identifier.
            /// @return true if the session is valid; false if unknown or expired
            ///         (an expired session is removed).
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
            
            /// @brief Remove a session and its event history.
            /// @param sessionId Session identifier.
            /// @return true if the session existed.
            bool terminateSession(const std::string& sessionId)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_sessions.erase(sessionId) > 0;
            }
            
            /// @brief Set the idle time after which a session expires.
            /// @param timeout Inactivity timeout; applies to existing sessions too.
            void setSessionTimeout(std::chrono::seconds timeout)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_sessionTimeout = timeout;
            }

            /// @brief Generate an unguessable session identifier.
            /// @return "session-" followed by 32 lowercase hex digits (128 bits drawn
            ///         directly from std::random_device, one 32-bit draw per word).
            static std::string generateSessionId()
            {
                static constexpr char hexDigits[] = "0123456789abcdef";
                std::random_device rd;
                std::string id = "session-";
                id.reserve(id.size() + 32);
                for (int word = 0; word < 4; ++word)
                {
                    const uint32_t v = static_cast<uint32_t>(rd());
                    for (int shift = 28; shift >= 0; shift -= 4)
                    {
                        id += hexDigits[(v >> shift) & 0xF];
                    }
                }
                return id;
            }
            
            /// @brief Append an event to a session's history for Last-Event-ID support.
            /// @note No-op if resumability is disabled or the session does not exist.
            ///       Events older than the history duration, and the oldest beyond the
            ///       history size limit, are dropped.
            /// @param sessionId Session identifier
            /// @param eventId Event identifier
            /// @param eventData Event data (SSE formatted)
            void addEvent(const std::string& sessionId, const std::string& eventId, const std::string& eventData)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_resumabilityEnabled)
                {
                    return;
                }
                auto it = m_sessions.find(sessionId);
                if (it == m_sessions.end())
                {
                    return;
                }
                
                auto& history = it->second.eventHistory;
                history.push_back({eventId, eventData, std::chrono::steady_clock::now()});
                pruneHistoryLocked(history);
            }
            
            /// @brief Get the stored events recorded after a given event ID.
            /// @param sessionId Session identifier
            /// @param lastEventId Last event ID received by the client; empty returns
            ///        the whole stored history.
            /// @return Event data (as passed to addEvent()) after @p lastEventId, oldest
            ///         first. Empty if resumability is disabled, the session is unknown,
            ///         or @p lastEventId is no longer (or never was) in the history.
            std::vector<std::string> getEventsSince(const std::string& sessionId, const std::string& lastEventId)
            {
                std::vector<std::string> events;

                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_resumabilityEnabled)
                {
                    return events;
                }
                auto it = m_sessions.find(sessionId);
                if (it == m_sessions.end())
                {
                    return events;
                }
                
                auto& history = it->second.eventHistory;
                pruneHistoryLocked(history);

                // If no lastEventId, return all recent events
                if (lastEventId.empty())
                {
                    for (const auto& event : history)
                    {
                        events.push_back(event.data);
                    }
                    return events;
                }

                // Find the position of lastEventId and return events after it
                bool found = false;
                for (const auto& event : history)
                {
                    if (found)
                    {
                        events.push_back(event.data);
                    }
                    else if (event.id == lastEventId)
                    {
                        found = true;
                    }
                }
                
                return events;
            }
            
            /// @brief Remove all sessions idle for longer than the session timeout.
            void cleanupExpiredSessions()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                cleanupExpiredSessionsLocked();
            }

            /// @brief Set the maximum number of live sessions; createSession() throws
            ///        beyond it.
            void setMaxSessions(size_t maxSessions)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_maxSessions = maxSessions;
            }

            /// @brief Get the current number of stored sessions (expired but not yet
            ///        purged sessions included).
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
        
        /// @brief CORS settings applied by HttpServer to every response when enabled.
        struct CorsConfig
        {
            bool enabled = false;  ///< Add Access-Control-* headers and answer unhandled OPTIONS with 204.
            std::string allowOrigin = constants::CORS_ALLOW_ORIGIN_ALL;             ///< Access-Control-Allow-Origin value.
            std::string allowMethods = constants::CORS_DEFAULT_METHODS;             ///< Access-Control-Allow-Methods value.
            std::string allowHeaders = constants::CORS_DEFAULT_ALLOW_HEADERS;       ///< Access-Control-Allow-Headers value.
            std::string exposeHeaders = constants::CORS_DEFAULT_EXPOSE_HEADERS;     ///< Access-Control-Expose-Headers value.
            std::string maxAge = constants::CORS_DEFAULT_MAX_AGE;                   ///< Access-Control-Max-Age value (OPTIONS responses only).
        };

        /// @brief A single Server-Sent Event, serialized with format().
        class SSEEvent
        {
        public:
            std::string event;  ///< Event type ("event:" field); omitted when empty.
            std::string data;   ///< Payload; each line becomes its own "data:" field; omitted when empty.
            std::string id;     ///< Event ID ("id:" field); omitted when empty.
            int retry = -1;     ///< Reconnection time in ms ("retry:" field); negative = not sent.

            /// @brief Serialize to the text/event-stream wire format.
            /// @return The event fields followed by the terminating blank line.
            /// @note CR and LF are stripped from @ref id and @ref event so they cannot
            ///       inject fields; @ref data is split on CRLF, CR and LF.
            std::string format() const
            {
                std::ostringstream oss;
                // Single-line fields must not contain line terminators: a CR or LF
                // would let a value inject extra fields (or end the event early).
                if (!id.empty())
                {
                    oss << "id: " << stripLineBreaks(id) << "\n";
                }
                if (!event.empty())
                {
                    oss << "event: " << stripLineBreaks(event) << "\n";
                }
                if (retry >= 0)
                {
                    oss << "retry: " << retry << "\n";
                }
                // Split data on every SSE line terminator (CRLF, CR or LF) so that
                // each line becomes its own "data:" field.
                if (!data.empty())
                {
                    size_t start = 0;
                    for (size_t i = 0; i < data.size(); ++i)
                    {
                        if (data[i] == '\r' || data[i] == '\n')
                        {
                            oss << "data: " << data.substr(start, i - start) << "\n";
                            if (data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n')
                            {
                                ++i;
                            }
                            start = i + 1;
                        }
                    }
                    oss << "data: " << data.substr(start) << "\n";
                }
                oss << "\n";  // Empty line terminates event
                return oss.str();
            }

        private:
            static std::string stripLineBreaks(const std::string& value)
            {
                std::string out;
                out.reserve(value.size());
                for (char c : value)
                {
                    if (c != '\r' && c != '\n')
                    {
                        out += c;
                    }
                }
                return out;
            }

        public:

            /// @brief Create an unnamed ("message") event.
            /// @param data Event payload.
            /// @param id Optional event ID.
            static SSEEvent message(const std::string& data, const std::string& id = "")
            {
                SSEEvent evt;
                evt.data = data;
                evt.id = id;
                return evt;
            }

            /// @brief Create an event with an explicit event type.
            /// @param event Event type ("event:" field).
            /// @param data Event payload.
            /// @param id Optional event ID.
            static SSEEvent custom(const std::string& event, const std::string& data, const std::string& id = "")
            {
                SSEEvent evt;
                evt.event = event;
                evt.data = data;
                evt.id = id;
                return evt;
            }
        };

        /// @brief Decode a URL-encoded (application/x-www-form-urlencoded) string.
        /// @note '+' decodes to a space. %00 and escaped ASCII control characters
        ///       are rejected; bytes >= 0x80 are accepted.
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

                    // Reject ASCII control characters (0x01-0x1F, 0x7F). Bytes >= 0x80
                    // are accepted: they are UTF-8 continuation/lead bytes, and
                    // rejecting 0x80-0x9F would break most multi-byte characters.
                    if (val < 0x20 || val == 0x7F)
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

        /// @brief A parsed HTTP request as passed to request handlers.
        struct HttpRequest
        {
            std::string client;    ///< Peer address as "ip:port".
            std::string method;    ///< Request method, e.g. "GET". Handlers see "GET" for HEAD requests.
            std::string uri;       ///< Request target exactly as received (path plus any query string), not decoded.
            std::string protocol;  ///< "HTTP/1.0" or "HTTP/1.1".
            /// @brief Header fields keyed by Title-Case name (see normalize_header_name()).
            ///        Repeated fields are joined with ", ". Look up with has_header() /
            ///        get_header_value() to avoid case mismatches.
            std::map<std::string, std::string> headers;
            std::string content;   ///< Request body; a chunked body is already decoded (Content-Length is then set).

            // Convenience methods for header access

            /// @brief Normalize a header name to Title-Case (each '-'-separated word
            ///        capitalized, the rest lower-cased), e.g. "x-api-KEY" -> "X-Api-Key".
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
            
            /// @brief Parse the query string of #uri (a fragment, if any, is ignored).
            /// @return Map of key to URL-decoded value; a bare "key" maps to "".
            ///         For duplicate keys the last value wins.
            /// @throws std::invalid_argument on invalid percent-encoding, an empty
            ///         parameter ("a&&b"), an empty key, a key with characters other
            ///         than alphanumerics, '_', '-' or '.', or when the count/length
            ///         limits in config (MAX_QUERY_PARAMS, MAX_QUERY_KEY_LENGTH,
            ///         MAX_QUERY_VALUE_LENGTH) are exceeded.
            /// @note Keys are not URL-decoded. The query is re-parsed on every call.
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
                    size_t amp = query.find('&', start);
                    size_t end = (amp == std::string::npos) ? query.length() : amp;
                    std::string pair = query.substr(start, end - start);
                    start = (amp == std::string::npos) ? query.length() : amp + 1;

                    if (pair.empty())
                    {
                        // A single trailing '&' is tolerated; "a=1&&b=2" is not.
                        if (amp == std::string::npos || start >= query.length())
                        {
                            continue;
                        }
                        throw std::invalid_argument("Empty query parameter");
                    }

                    // Enforce parameter count limit
                    if (paramCount >= config::MAX_QUERY_PARAMS)
                    {
                        throw std::invalid_argument("Too many query parameters (max: " +
                            std::to_string(config::MAX_QUERY_PARAMS) + ")");
                    }

                    // "key=value" or a bare flag "key" (empty value)
                    size_t eq = pair.find('=');
                    std::string key = pair.substr(0, eq);
                    std::string value = (eq == std::string::npos) ? std::string() : pair.substr(eq + 1);

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
                }

                return params;
            }
            
            /// @brief List the media ranges in the Accept header.
            /// @return Media ranges in header order with parameters (including ";q=")
            ///         removed; not sorted by quality. Empty if there is no Accept header.
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
            
            /// @brief Check whether the Accept header admits a media type (RFC 9110 12.5.1).
            ///
            /// The Accept header is parsed into media ranges; the most specific range
            /// matching @p mime_type (exact, then "type/*", then "*/*") decides, and the
            /// type is acceptable if that range's quality value is above 0. Parameters
            /// other than q are ignored; comparison is case-insensitive.
            /// @param mime_type The MIME type to check (e.g., "application/json")
            /// @return true if there is no Accept header or the type is acceptable.
            bool accepts(const std::string& mime_type) const
            {
                auto it = headers.find("Accept");
                if (it == headers.end())
                {
                    return true;  // No Accept header = accept all
                }

                auto lower = [](std::string v) {
                    std::transform(v.begin(), v.end(), v.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    return v;
                };
                auto trim = [](const std::string& v) {
                    const size_t b = v.find_first_not_of(" \t");
                    if (b == std::string::npos)
                        return std::string();
                    return v.substr(b, v.find_last_not_of(" \t") - b + 1);
                };

                const std::string wanted = lower(trim(mime_type));
                const std::string wantedType = wanted.substr(0, wanted.find('/'));

                int bestSpecificity = -1;  // 2 = exact, 1 = type/*, 0 = */*
                double bestQ = 0.0;
                std::stringstream ranges(it->second);
                std::string range;
                while (std::getline(ranges, range, ','))
                {
                    std::stringstream parts(range);
                    std::string media;
                    std::getline(parts, media, ';');
                    media = lower(trim(media));
                    if (media.empty())
                        continue;

                    double q = 1.0;
                    std::string param;
                    while (std::getline(parts, param, ';'))
                    {
                        param = lower(trim(param));
                        if (param.compare(0, 2, "q=") == 0)
                        {
                            char* endp = nullptr;
                            const std::string value = param.substr(2);
                            q = std::strtod(value.c_str(), &endp);
                            if (endp == value.c_str())
                                q = 1.0;  // unparsable: treat as default
                        }
                    }

                    int specificity = -1;
                    if (media == wanted)
                        specificity = 2;
                    else if (media == wantedType + "/*")
                        specificity = 1;
                    else if (media == "*/*")
                        specificity = 0;
                    if (specificity > bestSpecificity)
                    {
                        bestSpecificity = specificity;
                        bestQ = q;
                    }
                }
                return bestSpecificity >= 0 && bestQ > 0.0;
            }
        };

        /// @brief The response a request handler fills in.
        ///
        /// A handler either sets a status (set_status() or its return value) and/or
        /// a body (set_content(), send(), send_chunk()), or starts a stream with
        /// send_chunk_stream(). The server then adds Server, Connection, Date,
        /// Content-Length (or Transfer-Encoding: chunked) and, if enabled, CORS headers.
        struct HttpResponse
        {
            int code = 0;         ///< Status code; 0 = not set (see HttpServer request dispatch).
            std::string message;  ///< Reason phrase; filled from the status code when empty.
            /// @brief Response header fields. set_header() stores Title-Case names; when
            ///        writing this map directly, use the same form so server-managed
            ///        headers (e.g. Content-Length) are replaced rather than duplicated.
            std::map<std::string, std::string> headers;
            std::string body;     ///< Buffered response body (ignored for streaming responses).

            // Streaming support
            bool streaming = false;           ///< Streaming response; set by send_chunk_stream().
            /// Set by send_chunk_stream(); informational only. The framing of a streamed
            /// body follows the request's protocol: chunked for HTTP/1.1, raw and
            /// close-delimited for HTTP/1.0 (which has no chunked coding).
            bool useChunkedEncoding = false;
            /// @brief Produces the next chunk of a streaming response; returning an empty
            ///        string ends the stream. Set by send_chunk_stream().
            std::function<std::string()> streamCallback;
            /// @brief Invoked once after the stream callback returns "".
            /// @note Runs with the server's internal connection lock held; keep it short.
            std::function<void()> onStreamEnd;

            // Convenience methods for API consistency

            /// @brief Set the HTTP status code, which also marks the request as handled.
            /// @param statusCode HTTP status code (e.g., 200, 404, 500)
            /// @param statusMessage Optional reason phrase; empty selects the standard
            ///        one (HttpServer::getDefaultResponseMessage()).
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

            /// @brief Replace the body and set the Content-Type header.
            /// @param content Response body content
            /// @param contentType MIME type (defaults to CONTENT_TYPE_TEXT)
            void set_content(const std::string& content, const std::string& contentType = CONTENT_TYPE_TEXT)
            {
                body = content;
                headers[constants::CONTENT_TYPE] = contentType;
            }

            /// @brief Replace the response body. Unlike set_content(), Content-Type is
            ///        left unchanged.
            /// @note Nothing is sent until the handler returns.
            /// @param content Response body content
            void send(const std::string& content)
            {
                body = content;
            }

            /// @brief Make this a streaming response.
            ///
            /// After the headers are sent the server calls @p callback repeatedly and
            /// sends each returned string immediately - as one chunk of a chunked body for
            /// HTTP/1.1, or raw (the body then ends when the connection closes) for
            /// HTTP/1.0. Returning "" ends the stream, after which @p callback is not
            /// called again and @p onEnd runs. A HEAD request gets the headers only and
            /// the callback is never called.
            /// @note Without a thread pool the callback runs on the reactor thread and
            ///       blocks every other connection while it waits; with
            ///       HttpServer::enableThreadPool() each call runs on a worker thread.
            ///       The callback may be copied, but all copies share one state.
            /// @param callback Function that returns chunk data (empty string = end of stream)
            /// @param onEnd Optional callback invoked once when the stream ends
            void send_chunk_stream(std::function<std::string()> callback, std::function<void()> onEnd = nullptr)
            {
                streaming = true;
                useChunkedEncoding = true;
                streamCallback = std::move(callback);
                onStreamEnd = std::move(onEnd);
            }

            /// @brief Append data to a buffered (non-streaming) response body.
            /// @warning Nothing is sent until the handler returns: calling this in a loop
            ///          (e.g. to emit SSE events over time) delivers everything at once,
            ///          at the end. To stream, use send_chunk_stream(), whose callback is
            ///          invoked repeatedly and each returned chunk is sent immediately.
            /// @param chunk Data to append; an empty string is ignored.
            void send_chunk(const std::string& chunk)
            {
                body += chunk;
            }
        };

        /// @brief Request handler signature.
        ///
        /// Return a non-zero HTTP status to handle the request with that status, -1 to
        /// close the connection without a response, or 0 to leave the status to the
        /// response: the request still counts as handled if the handler called
        /// set_status() or set a body / stream (status then defaults to 200). Returning
        /// 0 without touching the response declines, and the next matching route runs.
        using CallbackFunction = std::function<int(HttpRequest const& request, HttpResponse& response)>;

        /// @brief Polymorphic request handler: wraps a CallbackFunction, or override
        ///        onHttpRequest() in a subclass.
        class HttpRequestCallback
        {
        protected:
            CallbackFunction callback = nullptr;  ///< Wrapped function; may be empty.

        public:
            /// @brief Create a handler with no function (declines every request).
            HttpRequestCallback() {};

            virtual ~HttpRequestCallback() = default;

            /// @brief Copy the wrapped function from @p other.
            HttpRequestCallback& operator=(HttpRequestCallback other)
            {
                callback = other.callback;
                return *this;
            };

            /// @brief Wrap @p func.
            HttpRequestCallback(CallbackFunction func) : callback(func) {};

            /// @brief Replace the wrapped function.
            HttpRequestCallback& operator=(CallbackFunction func)
            {
                callback = func;
                return (*this);
            }

            /// @brief Handle a request. The default calls the wrapped function.
            /// @param request The parsed request.
            /// @param response Response to fill in.
            /// @return See CallbackFunction; 0 if no function is set.
            virtual int onHttpRequest(HttpRequest const& request, HttpResponse& response)
            {
                if (callback != nullptr)
                {
                    return callback(request, response);
                }
                return 0;
            };
        };

        /// @brief Small embeddable HTTP/1.x server driven by a single reactor thread.
        ///
        /// Typical use:
        /// @code
        ///   HttpServer server("my-server", 0);         // bind all IPv4 interfaces, ephemeral port
        ///   server.route("/api/", [](const HttpRequest& req, HttpResponse& res) {
        ///       res.set_content("{}", CONTENT_TYPE_JSON);
        ///       return 200;
        ///   });
        ///   server.start();                           // non-blocking
        ///   int port = server.getListeningPort();
        ///   // ...
        ///   server.stop();
        /// @endcode
        ///
        /// Routing: every handler whose path is a prefix of the request URI is a
        /// candidate. Candidates run longest prefix first, then in registration order,
        /// until one handles the request (see CallbackFunction). If none does, OPTIONS
        /// gets 204 (CORS enabled) or 405, DELETE with an Mcp-Session-Id header
        /// terminates that session (200/404; 400 without the header), and anything
        /// else gets 404. HEAD is dispatched as GET and a buffered body is dropped.
        ///
        /// Threading: by default handlers and stream callbacks run on the reactor
        /// thread, one at a time, so a slow handler stalls all connections. With
        /// enableThreadPool() they run on worker threads instead.
        ///
        /// Configuration (listening ports, routes, limits, keep-alive, CORS, thread
        /// pool) is not synchronized with the reactor: finish it before start().
        ///
        /// Goals: enough of HTTP to embed or mock a service. Out of scope: high
        /// performance, TLS, full RFC 9110/9112 coverage.
        class HttpServer : private Reactor::SocketCallback
        {
        protected:
            /// @brief Per-connection state, keyed by socket in m_connections.
            struct Connection
            {
                Socket socket;              ///< Client socket.
                std::string receiveBuffer;  ///< Received bytes not yet consumed by the parser.
                std::string sendBuffer;     ///< Bytes queued for sending.
                /// @brief Connection state machine position.
                enum
                {
                    Idle,                 ///< Between requests; per-request state is reset on the next pass.
                    ReceivingHeaders,     ///< Waiting for the complete request head.
                    Sending100Continue,   ///< Sending "100 Continue" before reading the body.
                    ReceivingBody,        ///< Reading a Content-Length delimited body.
                    Processing,           ///< Request complete; handlers about to run.
                    ProcessingAsync,      ///< Handler or stream callback running on the thread pool.
                    SendingHeaders,       ///< Sending the status line and headers.
                    SendingBody,          ///< Sending the buffered body (or the terminal chunk).
                    StreamingChunked,     ///< Producing and sending chunks of a streaming response.
                    Closing,              ///< Send side shut down; waiting for the peer to close.
                    ReceivingChunkedBody  ///< Decoding a "Transfer-Encoding: chunked" request body.
                } state = Idle;
                size_t contentLength = 0;  ///< Declared Content-Length of the current request body.
                bool keepalive = false;    ///< Whether the connection stays open after this response.
                HttpRequest request;       ///< Current request.
                HttpResponse response;     ///< Current response.

                // Streaming state
                bool streamingActive = false;  ///< A streaming response is in progress.
                size_t chunksSent = 0;         ///< Chunks sent so far in the current stream.

                // Chunked request-body decoder state
                bool chunkedRequest = false;   ///< Current request body uses chunked transfer coding.
                /// @brief Chunked request-body decoder position.
                enum
                {
                    ChunkSize,     ///< Expecting the chunk-size line: hex size, optional ";ext", CRLF.
                    ChunkData,     ///< Copying chunk payload.
                    ChunkDataEnd,  ///< Expecting the CRLF that follows the payload.
                    ChunkTrailer   ///< Skipping trailer fields up to the empty line.
                } chunkState = ChunkSize;
                size_t chunkRemaining = 0;  ///< Payload bytes left in the current chunk.
                size_t trailerBytes = 0;    ///< Trailer bytes consumed (bounded by the header size limit).

                /// @brief Set when the connection must be torn down (a handler returned -1,
                /// or a hard send error). The connection is closed - and erased from
                /// m_connections - only by handleConnection() or flushAndContinue(), as
                /// the very last step, so nothing touches it afterwards.
                bool closeRequested = false;

                /// @brief Identifies the in-flight thread-pool dispatch; lets the worker detect
                /// that the connection was closed (and its fd possibly reused) meanwhile.
                uint64_t asyncToken = 0;

                /// Streamed body uses chunked framing (HTTP/1.1). HTTP/1.0 clients get the
                /// raw data, delimited by closing the connection.
                bool chunkedStream = true;
            };

            std::string m_serverHost;                ///< Value of the "Server" response header.
            bool allowKeepalive{ true };             ///< Server-wide keep-alive switch (setKeepalive()).
            Reactor m_reactor;                       ///< Socket event loop; owns the I/O thread.
            std::list<Socket> m_listeningSockets;    ///< Listening sockets, in the order added.
            std::vector<int> m_listeningPorts;       ///< Bound port of each listening socket.

            /// @brief A route: URI prefix (first) and non-owning handler pointer (second).
            ///        A null handler is skipped during dispatch.
            class HttpRequestHandler : public std::pair<std::string, HttpRequestCallback*>
            {
            public:
                /// @brief Route @p key to @p value (not owned; may be null).
                HttpRequestHandler(std::string key, HttpRequestCallback* value)
                {
                    first = key;
                    second = value;
                };

                /// @brief Empty route ("" with no handler).
                HttpRequestHandler() : std::pair<std::string, HttpRequestCallback*>()
                {
                    first = "";
                    second = nullptr;
                };

                /// @brief Replace both the prefix and the handler.
                HttpRequestHandler& operator=(std::pair<std::string, HttpRequestCallback*> other)
                {
                    first = other.first;
                    second = other.second;
                    return (*this);
                };

                /// @brief Set the handler (stored by address; @p cb must outlive the server).
                HttpRequestHandler& operator=(HttpRequestCallback& cb)
                {
                    second = &cb;
                    return (*this);
                };

                /// @brief Set the handler pointer (not owned; null disables the route).
                HttpRequestHandler& operator=(HttpRequestCallback* cb)
                {
                    second = cb;
                    return (*this);
                };
            };

            std::list<HttpRequestHandler> m_handlers;  ///< Routes in registration order.
            std::list<std::unique_ptr<HttpRequestCallback>> m_ownedCallbacks;  ///< Callbacks created by route(), owned by the server.

            std::map<Socket, Connection> m_connections;  ///< Open client connections; guarded by m_connectionsMutex.
            std::mutex m_connectionsMutex;               ///< Protects m_connections and each Connection.
            std::optional<BS::thread_pool<>> m_threadPool;  ///< Worker pool for handlers and stream callbacks, if enabled.
            std::atomic<uint64_t> m_asyncSerial{0};  ///< Source of Connection::asyncToken values for thread-pool dispatches.
            /// @brief Request head size limit (setRequestLimits()).
            size_t m_maxRequestHeadersSize, m_maxRequestContentSize;  ///< Request body size limit (setRequestLimits()).
            size_t m_maxSessions;  ///< Session limit forwarded to m_sessionManager (setMaxSessions()).

            SessionManager m_sessionManager;  ///< Sessions used by createSession() and the built-in DELETE handling.
            CorsConfig m_corsConfig;          ///< CORS settings.
            std::atomic<bool> m_stopped{false};  ///< Set by stop(); cleared by start().

        public:
            /// @brief Enable or disable HTTP keep-alive for all connections.
            /// @note When false, every response is sent with "Connection: close".
            ///       Call before start().
            void setKeepalive(bool keepAlive) { allowKeepalive = keepAlive; }

            /// @brief Create a server named "unnamed" with no listening socket; add one
            ///        with addListeningPort() before start().
            HttpServer()
                : m_serverHost("unnamed"),
                allowKeepalive(true),
                m_reactor(*this),
                m_maxRequestHeadersSize(config::MAX_HTTP_HEADER_SIZE),
                m_maxRequestContentSize(config::MAX_HTTP_BODY_SIZE),
                m_maxSessions(config::DEFAULT_MAX_SESSIONS) {};

            /// @brief Create a server and bind a listening socket on all IPv4 interfaces.
            /// @note @p serverHost is not a bind address: it only forms the "Server"
            ///       header ("serverHost:port", with the port as passed). To bind a
            ///       specific address, use the default constructor and
            ///       addListeningPort(host, port). Requests are served after start().
            /// @param serverHost Name used in the "Server" response header
            /// @param port Port to listen on; 0 picks an ephemeral port (see getListeningPort())
            /// @throws std::runtime_error if the port cannot be bound or listened on.
            HttpServer(std::string serverHost, int port = 30000) : HttpServer()
            {
                std::ostringstream os;
                os << serverHost << ":" << port;
                setServerName(os.str());
                addListeningPort(port);
            };

            /// @brief Stop the server, wait for queued thread-pool work to finish, and
            ///        close all listening and client sockets.
            /// @warning Blocks until in-flight handlers and stream callbacks return.
            virtual ~HttpServer()
            {
                // Join the reactor thread first: its callbacks use this object, so it
                // must not run while members are being destroyed.
                stop();
                // Let in-flight stream callbacks on the pool finish (they lock
                // m_connectionsMutex and touch m_connections).
                m_threadPool.reset();

                for (auto& sock : m_listeningSockets)
                {
                    if (!sock.invalid())
                    {
                        sock.close();  // close() invalidates, so each fd is closed once
                    }
                }
                std::lock_guard<std::mutex> lock(m_connectionsMutex);
                for (auto& entry : m_connections)
                {
                    Socket sock = entry.second.socket;
                    if (!sock.invalid())
                    {
                        sock.close();
                    }
                }
                m_connections.clear();
            }

            /// @brief Set request size limits.
            /// @param maxRequestHeadersSize Maximum size of the request line plus headers
            ///        (and of chunked-body trailers); larger requests get 431.
            /// @param maxRequestContentSize Maximum request body size; larger bodies get 413.
            /// @note Defaults: config::MAX_HTTP_HEADER_SIZE / config::MAX_HTTP_BODY_SIZE.
            ///       Call before start().
            void setRequestLimits(size_t maxRequestHeadersSize, size_t maxRequestContentSize)
            {
                m_maxRequestHeadersSize = maxRequestHeadersSize;
                m_maxRequestContentSize = maxRequestContentSize;
            }

            /// @brief Set the value of the "Server" response header. Call before start().
            void setServerName(std::string const& name) { m_serverHost = name; }

            /// @brief Run request handlers (and streaming callbacks) on a worker pool so
            /// slow handlers never block the I/O reactor or other connections.
            /// @note Handlers and stream callbacks may then run concurrently for
            ///       different connections and must be thread-safe. Requests on one
            ///       connection are still handled in order. Call before start();
            ///       calling again replaces the pool (waiting for queued work).
            /// @param numThreads Worker count (0 = hardware concurrency, or 4 if unknown)
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

            /// @brief Destroy the worker pool (waiting for queued work); handlers then run
            ///        on the reactor thread again.
            /// @warning Not synchronized with the reactor: call only while the server
            ///          is not running.
            void disableThreadPool()
            {
                m_threadPool.reset();
                LOG_INFO("HttpServer: Thread pool disabled");
            }

            /// @brief Whether enableThreadPool() is in effect.
            bool isThreadPoolEnabled() const
            {
                return m_threadPool.has_value();
            }

            // CORS configuration

            /// @brief Enable or disable CORS headers on all responses and 204 answers to
            ///        otherwise unhandled OPTIONS (preflight) requests. Call before start().
            void enableCors(bool enabled = true) { m_corsConfig.enabled = enabled; }

            /// @brief Set Access-Control-Allow-Origin (default "*"). Call before start().
            void setCorsOrigin(const std::string& origin) { m_corsConfig.allowOrigin = origin; }

            /// @brief Set Access-Control-Allow-Headers and, if non-empty,
            ///        Access-Control-Expose-Headers. Call before start().
            /// @param allowHeaders Access-Control-Allow-Headers value.
            /// @param exposeHeaders Access-Control-Expose-Headers value; empty keeps the current one.
            void setCorsHeaders(const std::string& allowHeaders, const std::string& exposeHeaders = "")
            {
                m_corsConfig.allowHeaders = allowHeaders;
                if (!exposeHeaders.empty())
                {
                    m_corsConfig.exposeHeaders = exposeHeaders;
                }
            }

            // Session management

            /// @brief Set the idle timeout of sessions (see SessionManager::setSessionTimeout()).
            /// @note Thread-safe.
            void setSessionTimeout(std::chrono::seconds timeout)
            {
                m_sessionManager.setSessionTimeout(timeout);
            }

            /// @brief Set the maximum number of live sessions (default
            ///        config::DEFAULT_MAX_SESSIONS); createSession() throws beyond it.
            /// @param maxSessions Session limit
            /// @note Thread-safe.
            void setMaxSessions(size_t maxSessions)
            {
                m_maxSessions = maxSessions;
                m_sessionManager.setMaxSessions(maxSessions);
            }

            /// @brief Create a session (see SessionManager::createSession()).
            /// @return The new session ID.
            /// @throws std::runtime_error if the session limit is reached.
            /// @note Thread-safe.
            std::string createSession()
            {
                return m_sessionManager.createSession();
            }

            /// @brief Check a session and refresh its idle timer (see
            ///        SessionManager::validateSession()).
            /// @return true if the session exists and has not expired.
            /// @note Thread-safe.
            bool validateSession(const std::string& sessionId)
            {
                return m_sessionManager.validateSession(sessionId);
            }

            /// @brief Remove a session.
            /// @return true if the session existed.
            /// @note Thread-safe. An unhandled DELETE request carrying an
            ///       Mcp-Session-Id header calls this too.
            bool terminateSession(const std::string& sessionId)
            {
                return m_sessionManager.terminateSession(sessionId);
            }

            /// @brief Listen on all IPv4 interfaces (INADDR_ANY).
            /// @param port Port to listen on; 0 picks an ephemeral port
            /// @return The bound port
            /// @throws std::runtime_error if the socket cannot be bound or listened on.
            /// @note Call before start().
            int addListeningPort(int port)
            {
                return addListeningSocket(SocketAddr(static_cast<u_long>(0), port));
            }

            /// @brief Listen on a specific local address, e.g. "127.0.0.1", "::1",
            /// "[::1]" or "localhost" (resolved to IPv4).
            /// @param host Local address or hostname to bind to
            /// @param port Port to listen on; 0 picks an ephemeral port
            /// @return The bound port
            /// @throws std::runtime_error if the socket cannot be bound or listened on.
            /// @note Call before start().
            int addListeningPort(const std::string& host, int port)
            {
                return addListeningSocket(SocketAddr(host.c_str(), port));
            }

        private:
            int addListeningSocket(SocketAddr addr)
            {
                const int port = addr.port();
                ScopedSocket scoped(addr.m_data.sa_family, SOCK_STREAM, IPPROTO_TCP);
                Socket& socket = scoped.get();
                socket.setNonBlocking();
                socket.setReuseAddr();

                if (socket.bind(addr) != 0)
                {
                    int err = socket.error();
                    throw std::runtime_error("Failed to bind to " + addr.toString() +
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

                // Success: the server owns the socket from here on.
                Socket owned = scoped.release();
                m_listeningSockets.push_back(owned);
                m_listeningPorts.push_back(addr.port());
                m_reactor.addSocket(owned, Reactor::Acceptable);
                LOG_INFO("HttpServer: Listening on %s", addr.toString().c_str());
                return addr.port();
            }

        public:
            /// @brief Port bound by the first addListeningPort() call (useful with port 0).
            /// @return The port, or -1 if the server is not listening.
            int getListeningPort() const
            {
                return m_listeningPorts.empty() ? -1 : m_listeningPorts.front();
            }

            /// @brief All ports this server listens on, in the order they were added.
            const std::vector<int>& getListeningPorts() const
            {
                return m_listeningPorts;
            }

            /// @brief Register @p handler for every URI starting with @p root.
            /// @param root URI prefix to match (plain string prefix, e.g. "/api/").
            /// @param handler Handler object; stored by address, not owned - it must
            ///        outlive the server.
            /// @return The new route entry.
            /// @note Routes are tried longest prefix first, then in registration order.
            ///       Not thread-safe: register routes before start().
            HttpRequestHandler& addHandler(const std::string& root, HttpRequestCallback& handler)
            {
                // No thread-safety here!
                m_handlers.push_back({ root, &handler });
                LOG_INFO("HttpServer: Added handler for %s", root.c_str());
                return m_handlers.back();
            }

            /// @brief Add a route for @p root with no handler yet; assign one to the
            ///        result, e.g. `server["/x"] = myCallback;` (stored by address, not owned).
            /// @note Every call appends a new route, even for an existing prefix. A
            ///       route left without a handler is ignored. Register before start().
            HttpRequestHandler& operator[](const std::string& root)
            {
                // No thread-safety here!
                m_handlers.push_back({ root, nullptr });
                LOG_INFO("HttpServer: Added handler for %s", root.c_str());
                return m_handlers.back();
            }

            /// @brief Register a {prefix, handler} pair; same as addHandler().
            /// @note The handler is stored by address and must outlive the server.
            HttpServer& operator+=(std::pair<const std::string&, HttpRequestCallback&> other)
            {
                LOG_INFO("HttpServer: Added handler for %s", other.first.c_str());
                m_handlers.push_back(HttpRequestHandler(other.first, &other.second));
                return (*this);
            };

            /// @brief Register a handler function for every URI starting with @p path.
            ///
            /// Matching is a plain string prefix test on the raw request target, so
            /// "/api" also matches "/apix" and "/api?x=1". Routes are tried longest
            /// prefix first, then in registration order, until one handles the request
            /// (see CallbackFunction for what counts as handled).
            /// @param path URI prefix to match
            /// @param handler Callback function to handle requests
            /// @return Reference to the created route entry
            /// @note The callback object is owned by the server. Not thread-safe:
            ///       register routes before start().
            HttpRequestHandler& route(const std::string& path, CallbackFunction handler)
            {
                m_ownedCallbacks.push_back(std::make_unique<HttpRequestCallback>(std::move(handler)));
                m_handlers.push_back({ path, m_ownedCallbacks.back().get() });
                LOG_INFO("HttpServer: Added route for %s", path.c_str());
                return m_handlers.back();
            }

            /// @brief Set the maximum request body size (see setRequestLimits()); larger
            ///        bodies get 413. Call before start().
            /// @param maxSize Maximum allowed content size in bytes
            void setMaxRequestContentSize(size_t maxSize)
            {
                m_maxRequestContentSize = maxSize;
            }

            /// @brief Start the reactor thread and begin serving. Non-blocking: returns
            ///        immediately.
            /// @note Add listening ports, routes and settings before calling this.
            ///       Restarting after stop() is not supported (stop() unregisters the
            ///       listening sockets and closes the first one).
            void start()
            {
                m_stopped = false;
                m_reactor.start();
            }

            /// @brief Stop serving and join the reactor thread. Idempotent.
            /// @note Client connections are closed by the destructor, not here. Thread-pool
            ///       work already queued still runs, but no longer sends responses.
            void stop()
            {
                if (m_stopped.exchange(true))
                {
                    return;
                }
                m_reactor.stop();
                // Reactor::stop() "unbinds" by closing the first socket registered with
                // it, which is our first listening socket. Forget that descriptor so it
                // is not closed a second time (by then the number may belong to an
                // unrelated, newly opened file).
                if (!m_listeningSockets.empty())
                {
                    m_listeningSockets.front().m_sock = Socket::Invalid;
                }
            }

        protected:
            /// @brief Reactor callback: accept a client on listening socket @p socket
            ///        and register the new connection. Runs on the reactor thread.
            virtual void onSocketAcceptable(Socket socket) override
            {
                LOG_TRACE("HttpServer: accepting socket fd=0x%llx", static_cast<unsigned long long>(socket.m_sock));
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

            /// @brief Reactor callback: read available bytes and advance the connection's
            ///        state machine (which may run handlers when no thread pool is used).
            ///        Closes the connection on EOF or a hard error. Runs on the reactor thread.
            virtual void onSocketReadable(Socket socket) override
            {
                LOG_TRACE("HttpServer: reading socket fd=0x%llx", static_cast<unsigned long long>(socket.m_sock));
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
                if (received < 0 && isTransientSocketError(socket.error()))
                {
                    return;  // Spurious wakeup - nothing to read yet
                }
                if (received <= 0)
                {
                    handleConnectionClosed(conn);
                    return;
                }
                conn.receiveBuffer.append(buffer, buffer + received);

                handleConnection(conn);  // May close and erase conn - must be the last use
            }

            /// @brief Reactor callback: send pending output and continue the state
            ///        machine (see flushAndContinue()). Runs on the reactor thread.
            virtual void onSocketWritable(Socket socket) override
            {
                LOG_TRACE("HttpServer: writing socket fd=0x%llx", static_cast<unsigned long long>(socket.m_sock));

                assert(std::find(m_listeningSockets.begin(), m_listeningSockets.end(), socket) ==
                    m_listeningSockets.end());

                std::lock_guard<std::mutex> lock(m_connectionsMutex);
                auto connIt = m_connections.find(socket);
                if (connIt == m_connections.end())
                {
                    return;
                }
                flushAndContinue(connIt->second);  // may close and erase the connection
            }

            /// @brief Send pending output, then advance the state machine - what a writable
            /// event does. Also used by thread-pool workers after they queue output: sending
            /// from the worker (rather than only arming Writable) is required on Windows,
            /// where FD_WRITE is signalled only after a send fails with WSAEWOULDBLOCK.
            /// Called with m_connectionsMutex held.
            /// @warning May close and erase conn; callers must not use it afterwards.
            void flushAndContinue(Connection& conn)
            {
                if (sendMore(conn))
                {
                    if (conn.closeRequested)
                    {
                        handleConnectionClosed(conn);  // conn is gone after this
                    }
                    return;
                }
                handleConnection(conn);
            }

            /// @brief Reactor callback: the peer closed (or reset) the connection; close
            ///        it and erase its state. Runs on the reactor thread.
            virtual void onSocketClosed(Socket socket) override
            {
                LOG_TRACE("HttpServer: closing socket fd=0x%llx", static_cast<unsigned long long>(socket.m_sock));
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

            /// @brief Whether a socket error means "try again later" (would-block,
            ///        EINTR, EAGAIN) rather than a broken connection.
            static bool isTransientSocketError(int err)
            {
                if (err == Socket::ErrorWouldBlock)
                {
                    return true;
                }
#ifndef _WIN32
                if (err == EINTR)
                {
                    return true;
                }
#  if defined(EAGAIN) && defined(EWOULDBLOCK) && (EAGAIN != EWOULDBLOCK)
                if (err == EAGAIN)
                {
                    return true;
                }
#  endif
#endif
                return false;
            }

            /// @brief Send as much of conn.sendBuffer as the socket accepts.
            /// @return true if the caller must stop and wait: either data is still
            ///         pending (Writable has been armed) or the connection hit a hard
            ///         error, in which case conn.closeRequested is set.
            ///         false if the buffer has been fully sent.
            bool sendMore(Connection& conn)
            {
                if (conn.closeRequested)
                {
                    return true;
                }
                if (conn.sendBuffer.empty())
                {
                    return false;
                }

                // Keep sending until the buffer is drained or the socket would block.
                // Stopping after a partial send is not enough: on Windows FD_WRITE is
                // only signalled again after a send() fails with WSAEWOULDBLOCK.
                size_t offset = 0;
                while (offset < conn.sendBuffer.size())
                {
                    int sent = conn.socket.send(conn.sendBuffer.data() + offset, conn.sendBuffer.size() - offset);
                    LOG_TRACE("HttpServer: [%s] sent %d", conn.request.client.c_str(), sent);
                    if (sent > 0)
                    {
                        offset += static_cast<size_t>(sent);
                        continue;
                    }
                    const int err = conn.socket.error();
                    if (sent < 0 && !isTransientSocketError(err))
                    {
                        LOG_WARN("HttpServer: [%s] send failed, error %d - closing", conn.request.client.c_str(), err);
                        conn.closeRequested = true;
                        return true;
                    }
                    break;  // Socket buffer full - retry when writable
                }
                conn.sendBuffer.erase(0, offset);

                if (!conn.sendBuffer.empty())
                {
                    m_reactor.addSocket(conn.socket,
                        Reactor::Writable | Reactor::Closed);
                    return true;
                }

                return false;
            }

        protected:
            /// @brief Close the connection and erase it from m_connections.
            /// @warning conn is a dangling reference once this returns.
            void handleConnectionClosed(Connection& conn)
            {
                LOG_TRACE("HttpServer: [%s] closed", conn.request.client.c_str());
                if (conn.state != Connection::Idle && conn.state != Connection::Closing && !conn.closeRequested)
                {
                    LOG_WARN("HttpServer: [%s] connection closed unexpectedly", conn.request.client.c_str());
                }
                m_reactor.removeSocket(conn.socket);
                Socket sock = conn.socket;
                auto connIt = m_connections.find(sock);
                sock.close();
                if (connIt != m_connections.end())
                {
                    m_connections.erase(connIt);
                }
            }

            /// @brief Drive the connection state machine.
            /// @warning May close the connection (erasing conn); callers must not use
            ///          conn after this returns.
            void handleConnection(Connection& conn)
            {
                runConnection(conn);
                if (conn.closeRequested)
                {
                    handleConnectionClosed(conn);
                }
            }

            /// @brief Reset per-request state so nothing leaks between keep-alive requests.
            static void resetForNextRequest(Connection& conn)
            {
                conn.request.method.clear();
                conn.request.uri.clear();
                conn.request.protocol.clear();
                conn.request.headers.clear();
                conn.request.content.clear();
                conn.response = HttpResponse();
                conn.response.code = 0;
                conn.contentLength = 0;
                conn.streamingActive = false;
                conn.chunksSent = 0;
                conn.chunkedRequest = false;
                conn.chunkState = Connection::ChunkSize;
                conn.chunkRemaining = 0;
                conn.trailerBytes = 0;
            }

            /// @brief Put the connection into "send an error response, then close" mode.
            static void failRequest(Connection& conn, int code)
            {
                conn.response.code = code;
                conn.keepalive = false;
                conn.state = Connection::Processing;
            }

            /// @brief Find the end of the request head: the first empty line, where
            ///        lines may end in CRLF or bare LF.
            /// @return Offset just past the empty line, or npos if not received yet.
            static size_t findHeadersEnd(const std::string& buf)
            {
                size_t pos = 0;
                while ((pos = buf.find('\n', pos)) != std::string::npos)
                {
                    if (pos + 1 < buf.size() && buf[pos + 1] == '\n')
                    {
                        return pos + 2;
                    }
                    if (pos + 2 < buf.size() && buf[pos + 1] == '\r' && buf[pos + 2] == '\n')
                    {
                        return pos + 3;
                    }
                    ++pos;
                }
                return std::string::npos;
            }

            /// @brief Strictly parse a Content-Length value: 1*DIGIT, no sign, no
            ///        whitespace, no list, no overflow.
            static bool parseContentLength(const std::string& value, size_t& out)
            {
                if (value.empty())
                {
                    return false;
                }
                size_t result = 0;
                for (char c : value)
                {
                    if (c < '0' || c > '9')
                    {
                        return false;
                    }
                    const size_t digit = static_cast<size_t>(c - '0');
                    if (result > ((std::numeric_limits<size_t>::max)() - digit) / 10)
                    {
                        return false;
                    }
                    result = result * 10 + digit;
                }
                out = result;
                return true;
            }

            /// @brief Return @p s with ASCII letters lower-cased.
            static std::string toLowerAscii(std::string s)
            {
                for (char& c : s)
                {
                    c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
                }
                return s;
            }

            /// @brief Strip leading and trailing optional whitespace (spaces and tabs).
            static std::string trimOws(const std::string& s)
            {
                size_t b = s.find_first_not_of(" \t");
                if (b == std::string::npos)
                {
                    return std::string();
                }
                size_t e = s.find_last_not_of(" \t");
                return s.substr(b, e - b + 1);
            }

            /// @brief Split a comma-separated header value into trimmed, lower-cased tokens.
            static std::vector<std::string> splitHeaderTokens(const std::string& value)
            {
                std::vector<std::string> tokens;
                size_t start = 0;
                while (start <= value.size())
                {
                    size_t comma = value.find(',', start);
                    size_t end = (comma == std::string::npos) ? value.size() : comma;
                    std::string tok = trimOws(value.substr(start, end - start));
                    if (!tok.empty())
                    {
                        tokens.push_back(toLowerAscii(tok));
                    }
                    if (comma == std::string::npos)
                    {
                        break;
                    }
                    start = comma + 1;
                }
                return tokens;
            }

            /// @brief Incrementally decode a chunked request body from conn.receiveBuffer.
            /// @return 0 if more data is needed, 1 when the body is complete, or an
            ///         HTTP error status (400/413/431) on malformed or oversized input.
            int decodeChunkedBody(Connection& conn)
            {
                static constexpr size_t kMaxChunkLine = 1024;
                std::string& buf = conn.receiveBuffer;
                for (;;)
                {
                    switch (conn.chunkState)
                    {
                    case Connection::ChunkSize:
                    {
                        size_t eol = buf.find('\n');
                        if (eol == std::string::npos)
                        {
                            return (buf.size() > kMaxChunkLine) ? 400 : 0;
                        }
                        if (eol > kMaxChunkLine)
                        {
                            return 400;
                        }
                        size_t lineEnd = (eol > 0 && buf[eol - 1] == '\r') ? eol - 1 : eol;
                        size_t i = 0;
                        size_t size = 0;
                        while (i < lineEnd && std::isxdigit(static_cast<unsigned char>(buf[i])))
                        {
                            const char c = buf[i];
                            const size_t digit = static_cast<size_t>(
                                (c >= '0' && c <= '9') ? c - '0' : (::tolower(static_cast<unsigned char>(c)) - 'a' + 10));
                            if (size > ((std::numeric_limits<size_t>::max)() - digit) / 16)
                            {
                                return 413;
                            }
                            size = size * 16 + digit;
                            ++i;
                        }
                        if (i == 0)
                        {
                            return 400;  // No chunk size
                        }
                        while (i < lineEnd && (buf[i] == ' ' || buf[i] == '\t'))
                        {
                            ++i;
                        }
                        if (i < lineEnd && buf[i] != ';')
                        {
                            return 400;  // Garbage after chunk size (extensions start with ';')
                        }
                        for (size_t k = i; k < lineEnd; ++k)
                        {
                            unsigned char c = static_cast<unsigned char>(buf[k]);
                            if ((c < 0x20 && c != '\t') || c == 0x7F)
                            {
                                return 400;
                            }
                        }
                        buf.erase(0, eol + 1);
                        if (size == 0)
                        {
                            conn.chunkState = Connection::ChunkTrailer;
                            conn.trailerBytes = 0;
                            break;
                        }
                        if (size > m_maxRequestContentSize - conn.request.content.size())
                        {
                            return 413;
                        }
                        conn.chunkRemaining = size;
                        conn.chunkState = Connection::ChunkData;
                        break;
                    }
                    case Connection::ChunkData:
                    {
                        if (buf.empty())
                        {
                            return 0;
                        }
                        size_t n = (std::min)(buf.size(), conn.chunkRemaining);
                        conn.request.content.append(buf, 0, n);
                        buf.erase(0, n);
                        conn.chunkRemaining -= n;
                        if (conn.chunkRemaining == 0)
                        {
                            conn.chunkState = Connection::ChunkDataEnd;
                        }
                        break;
                    }
                    case Connection::ChunkDataEnd:
                    {
                        if (buf.empty())
                        {
                            return 0;
                        }
                        if (buf[0] == '\n')
                        {
                            buf.erase(0, 1);
                        }
                        else if (buf[0] == '\r')
                        {
                            if (buf.size() < 2)
                            {
                                return 0;
                            }
                            if (buf[1] != '\n')
                            {
                                return 400;
                            }
                            buf.erase(0, 2);
                        }
                        else
                        {
                            return 400;  // Chunk longer than its declared size
                        }
                        conn.chunkState = Connection::ChunkSize;
                        break;
                    }
                    case Connection::ChunkTrailer:
                    {
                        size_t eol = buf.find('\n');
                        if (eol == std::string::npos)
                        {
                            return (conn.trailerBytes + buf.size() > m_maxRequestHeadersSize) ? 431 : 0;
                        }
                        conn.trailerBytes += eol + 1;
                        if (conn.trailerBytes > m_maxRequestHeadersSize)
                        {
                            return 431;
                        }
                        const bool emptyLine = (eol == 0) || (eol == 1 && buf[0] == '\r');
                        buf.erase(0, eol + 1);  // Trailer fields are discarded
                        if (emptyLine)
                        {
                            return 1;
                        }
                        break;
                    }
                    }
                }
            }

            /// @brief Advance the connection state machine as far as the buffered input
            ///        and socket allow: parse the head, read the body, run handlers (or
            ///        dispatch them to the thread pool), and send the response or stream.
            /// @note Called with m_connectionsMutex held. Sets conn.closeRequested
            ///       instead of closing; use handleConnection(), which then closes.
            void runConnection(Connection& conn)
            {
                for (;;)
                {
                    if (conn.closeRequested)
                    {
                        return;
                    }

                    if (conn.state == Connection::Idle)
                    {
                        resetForNextRequest(conn);
                        conn.state = Connection::ReceivingHeaders;
                        LOG_TRACE("HttpServer: [%s] receiving headers", conn.request.client.c_str());
                    }

                    if (conn.state == Connection::ReceivingHeaders)
                    {
                        // Ignore empty line(s) preceding the request-line (RFC 9112, 2.2).
                        size_t lead = conn.receiveBuffer.find_first_not_of("\r\n");
                        conn.receiveBuffer.erase(0, (lead == std::string::npos) ? conn.receiveBuffer.size() : lead);

                        size_t headersEnd = findHeadersEnd(conn.receiveBuffer);
                        size_t headersLen = (headersEnd != std::string::npos) ? headersEnd : conn.receiveBuffer.length();
                        if (headersLen > m_maxRequestHeadersSize)
                        {
                            LOG_WARN("HttpServer: [%s] headers too long - %u", conn.request.client.c_str(),
                                static_cast<unsigned>(headersLen));
                            failRequest(conn, 431);  // Request Header Fields Too Large
                            continue;
                        }
                        if (headersEnd == std::string::npos)
                        {
                            return;
                        }

                        size_t consumed = 0;
                        int parseError = parseRequestHead(conn, consumed);
                        if (parseError != 0)
                        {
                            LOG_WARN("HttpServer: [%s] invalid request head (%d)", conn.request.client.c_str(), parseError);
                            failRequest(conn, parseError);
                            continue;
                        }
                        LOG_INFO("HttpServer: [%s] %s %s %s", conn.request.client.c_str(),
                            conn.request.method.c_str(), conn.request.uri.c_str(),
                            conn.request.protocol.c_str());
                        conn.receiveBuffer.erase(0, consumed);

                        conn.keepalive = (conn.request.protocol == constants::HTTP_1_1);
                        auto const connection = conn.request.headers.find(constants::CONNECTION);
                        if (connection != conn.request.headers.end())
                        {
                            bool hasClose = false;
                            bool hasKeepAlive = false;
                            for (auto const& token : splitHeaderTokens(connection->second))
                            {
                                hasClose |= (token == constants::CONNECTION_CLOSE);
                                hasKeepAlive |= (token == constants::CONNECTION_KEEP_ALIVE);
                            }
                            if (hasClose)
                            {
                                conn.keepalive = false;
                            }
                            else if (hasKeepAlive)
                            {
                                conn.keepalive = true;
                            }
                        }

                        // Message body framing (RFC 9112, 6). Anything ambiguous is
                        // rejected rather than guessed at, to prevent request smuggling.
                        auto const transferEncoding = conn.request.headers.find(constants::TRANSFER_ENCODING);
                        auto const contentLength = conn.request.headers.find(constants::CONTENT_LENGTH);
                        conn.contentLength = 0;
                        conn.chunkedRequest = false;
                        if (transferEncoding != conn.request.headers.end())
                        {
                            if (contentLength != conn.request.headers.end())
                            {
                                LOG_WARN("HttpServer: [%s] both Transfer-Encoding and Content-Length", conn.request.client.c_str());
                                failRequest(conn, 400);
                                continue;
                            }
                            if (conn.request.protocol != constants::HTTP_1_1)
                            {
                                failRequest(conn, 400);  // Transfer-Encoding is not defined for HTTP/1.0
                                continue;
                            }
                            auto const codings = splitHeaderTokens(transferEncoding->second);
                            if (codings.size() != 1 || codings[0] != constants::TRANSFER_ENCODING_CHUNKED)
                            {
                                LOG_WARN("HttpServer: [%s] unsupported Transfer-Encoding: %s", conn.request.client.c_str(),
                                    transferEncoding->second.c_str());
                                failRequest(conn, 501);  // Not Implemented
                                continue;
                            }
                            conn.chunkedRequest = true;
                        }
                        else if (contentLength != conn.request.headers.end())
                        {
                            size_t length = 0;
                            if (!parseContentLength(contentLength->second, length))
                            {
                                LOG_WARN("HttpServer: [%s] invalid Content-Length: %s", conn.request.client.c_str(),
                                    contentLength->second.c_str());
                                failRequest(conn, 400);
                                continue;
                            }
                            conn.contentLength = length;
                        }
                        if (conn.contentLength > m_maxRequestContentSize)
                        {
                            LOG_WARN("HttpServer: [%s] content too long - %zu", conn.request.client.c_str(),
                                conn.contentLength);
                            failRequest(conn, 413);  // Payload Too Large
                            continue;
                        }

                        auto const expect = conn.request.headers.find(constants::EXPECT);
                        if (expect != conn.request.headers.end() && conn.request.protocol == constants::HTTP_1_1)
                        {
                            if (!equalsLowercased(expect->second, constants::EXPECT_100_CONTINUE))
                            {
                                LOG_WARN("HttpServer: [%s] unknown expectation - %s", conn.request.client.c_str(),
                                    expect->second.c_str());
                                failRequest(conn, 417);  // Expectation Failed
                                continue;
                            }
                            conn.sendBuffer = "HTTP/1.1 100 Continue\r\n\r\n";
                            conn.state = Connection::Sending100Continue;
                            LOG_TRACE("HttpServer: [%s] sending \"100 Continue\"", conn.request.client.c_str());
                            continue;
                        }

                        conn.state = conn.chunkedRequest ? Connection::ReceivingChunkedBody : Connection::ReceivingBody;
                        LOG_TRACE("HttpServer: [%s] receiving body", conn.request.client.c_str());
                    }

                    if (conn.state == Connection::Sending100Continue)
                    {
                        if (sendMore(conn))
                        {
                            return;
                        }

                        conn.state = conn.chunkedRequest ? Connection::ReceivingChunkedBody : Connection::ReceivingBody;
                        LOG_TRACE("HttpServer: [%s] receiving body", conn.request.client.c_str());
                    }

                    if (conn.state == Connection::ReceivingChunkedBody)
                    {
                        int result = decodeChunkedBody(conn);
                        if (result == 0)
                        {
                            return;  // Need more data
                        }
                        if (result != 1)
                        {
                            LOG_WARN("HttpServer: [%s] bad chunked body (%d)", conn.request.client.c_str(), result);
                            failRequest(conn, result);
                            continue;
                        }
                        // Present the decoded message to handlers as a plain
                        // Content-Length message (RFC 9112, 7.1.3).
                        conn.request.headers.erase(constants::TRANSFER_ENCODING);
                        conn.request.headers[constants::CONTENT_LENGTH] = std::to_string(conn.request.content.size());
                        conn.state = Connection::Processing;
                        LOG_TRACE("HttpServer: [%s] processing request", conn.request.client.c_str());
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
                        if (m_threadPool.has_value() && !m_stopped.load())
                        {
                            // Run the handlers on the pool so a slow handler never stalls
                            // the reactor (and with it every other connection).
                            dispatchToThreadPool(conn);
                            return;
                        }

                        processRequest(conn);
                        if (conn.closeRequested)
                        {
                            LOG_TRACE("HttpServer: [%s] closing by request", conn.request.client.c_str());
                            return;  // handleConnection() closes the connection
                        }
                        beginResponse(conn);
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
                            conn.response.body.clear();
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
                            // If a thread pool is available, offload the (potentially blocking)
                            // stream callback to a worker thread so the reactor remains free to
                            // service other connections (e.g. tools/list POST requests) while
                            // the SSE stream is held open.
                            if (m_threadPool.has_value())
                            {
                                Socket sock = conn.socket;
                                const uint64_t token = ++m_asyncSerial;
                                conn.asyncToken = token;
                                conn.state = Connection::ProcessingAsync;  // prevent re-entry
                                auto streamCb  = conn.response.streamCallback;  // copy by value
                                auto onEndCb   = conn.response.onStreamEnd;     // copy by value
                                bool kaAllowed = allowKeepalive;
                                m_threadPool->detach_task([this, sock, token, streamCb, onEndCb, kaAllowed]() mutable
                                {
                                    // *** Run outside the mutex — may block for up to writeDeadlineSeconds ***
                                    std::string chunkData = streamCb();

                                    // Re-acquire mutex to update the connection state
                                    std::lock_guard<std::mutex> lock(m_connectionsMutex);
                                    auto it = m_connections.find(sock);
                                    if (it == m_connections.end()) return;  // connection gone
                                    Connection& ac = it->second;
                                    if (ac.state != Connection::ProcessingAsync || ac.asyncToken != token)
                                        return;  // closed (fd possibly reused) or state changed

                                    if (chunkData.empty())
                                    {
                                        // End of stream: queue terminal chunk, transition to SendingBody
                                        // so the normal send/keepalive path handles the rest.
                                        ac.sendBuffer = streamTerminator(ac);
                                        ac.streamingActive = false;
                                        ac.keepalive &= kaAllowed;
                                        if (onEndCb) onEndCb();
                                        ac.state = Connection::SendingBody;
                                        LOG_TRACE("HttpServer: stream ended (thread pool path)");
                                    }
                                    else
                                    {
                                        ac.sendBuffer = frameStreamChunk(ac, chunkData);
                                        ac.chunksSent++;
                                        ac.state = Connection::StreamingChunked;
                                        LOG_TRACE("HttpServer: stream chunk #%zu ready (thread pool)", ac.chunksSent);
                                    }
                                    if (m_stopped.load()) return;
                                    // Send now; if the socket would block, sendMore() arms
                                    // Writable and the reactor finishes the chunk.
                                    flushAndContinue(ac);  // may close and erase ac - last use
                                });
                                return;  // reactor thread is now free
                            }

                            // --- Synchronous path (no thread pool) — may block reactor ---
                            std::string chunkData = conn.response.streamCallback();

                            if (chunkData.empty())
                            {
                                // End of stream: queue the terminal chunk and hand over to
                                // SendingBody, which finishes sending it (possibly across
                                // several writable events) and then applies keep-alive.
                                // Leaving StreamingChunked here guarantees the callback is
                                // never invoked again after it signalled end-of-stream.
                                conn.sendBuffer = streamTerminator(conn);  // Terminal chunk
                                LOG_TRACE("HttpServer: [%s] sending terminal chunk (sent %zu chunks)",
                                    conn.request.client.c_str(), conn.chunksSent);
                                conn.streamingActive = false;
                                conn.state = Connection::SendingBody;
                                if (conn.response.onStreamEnd)
                                {
                                    conn.response.onStreamEnd();
                                }
                                continue;
                            }

                            // Chunked framing (<size in hex>\r\n<data>\r\n), raw for HTTP/1.0
                            conn.sendBuffer = frameStreamChunk(conn, chunkData);
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
                        else
                        {
                            // No callback - end stream
                            conn.sendBuffer = streamTerminator(conn);
                            conn.streamingActive = false;
                            conn.state = Connection::SendingBody;
                            continue;
                        }
                    }

                    if (conn.state == Connection::Closing)
                    {
                        return;
                    }
                }
            }

            /// @brief HTTP version for the status line. Never empty: requests that
            ///        failed before (or while) parsing the request-line get HTTP/1.1.
            static const char* responseProtocol(const HttpRequest& request)
            {
                return (request.protocol == constants::HTTP_1_0) ? constants::HTTP_1_0 : constants::HTTP_1_1;
            }

            /// @brief Legacy wrapper around parseRequestHead().
            bool parseHeaders(Connection& conn)
            {
                size_t consumed = 0;
                return parseRequestHead(conn, consumed) == 0;
            }

            /// @brief Whether @p c is an RFC 9110 "tchar" (valid in a header field name).
            static bool isTokenChar(unsigned char c)
            {
                if (std::isalnum(c))
                {
                    return true;
                }
                switch (c)
                {
                case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
                case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
                    return true;
                default:
                    return false;
                }
            }

            /// @brief Parse request-line and header fields from conn.receiveBuffer.
            /// @param conn Connection whose receiveBuffer holds the request head; its
            ///        request fields are filled in.
            /// @param consumed Receives the number of bytes making up the request head
            ///        (including the terminating empty line).
            /// @return 0 on success, otherwise the HTTP status to reply with.
            int parseRequestHead(Connection& conn, size_t& consumed)
            {
                // Method
                char const* const bufferStart = conn.receiveBuffer.c_str();
                char const* begin = bufferStart;
                char const* ptr = begin;
                while (*ptr && *ptr != ' ' && *ptr != '\r' && *ptr != '\n')
                {
                    ptr++;
                }
                if (*ptr != ' ')
                {
                    return 400;
                }

                // Validate method length
                size_t methodLen = ptr - begin;
                if (methodLen == 0 || methodLen > config::MAX_METHOD_LENGTH)
                {
                    LOG_WARN("HTTP method length invalid: %zu", methodLen);
                    return 400;
                }

                std::string method(begin, ptr);

                // Validate method is in whitelist
                static const char* allowedMethods[] = {
                    "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH", "TRACE", "CONNECT"
                };
                bool validMethod = false;
                for (const char* allowed : allowedMethods)
                {
                    if (method == allowed)
                    {
                        validMethod = true;
                        break;
                    }
                }
                if (!validMethod)
                {
                    LOG_WARN("Invalid HTTP method: %s", method.c_str());
                    return 400;
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
                    return 400;
                }

                // Validate URI length
                size_t uriLen = ptr - begin;
                if (uriLen == 0)
                {
                    return 400;
                }
                if (uriLen > config::MAX_URI_LENGTH)
                {
                    LOG_WARN("HTTP URI length invalid: %zu (max: %zu)", uriLen, config::MAX_URI_LENGTH);
                    return 414;
                }

                std::string uri(begin, ptr);

                // Validate URI doesn't contain control characters
                for (char c : uri)
                {
                    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
                    {
                        LOG_WARN("HTTP URI contains control character: 0x%02X", static_cast<unsigned char>(c));
                        return 400;
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
                    return 400;
                }

                // Validate protocol format: exactly "HTTP/" DIGIT "." DIGIT
                std::string protocol(begin, ptr);
                if (protocol.size() != 8 || protocol.compare(0, 5, "HTTP/") != 0 ||
                    !std::isdigit(static_cast<unsigned char>(protocol[5])) || protocol[6] != '.' ||
                    !std::isdigit(static_cast<unsigned char>(protocol[7])))
                {
                    LOG_WARN("Invalid HTTP protocol: %s", protocol.c_str());
                    return 400;
                }
                if (protocol[5] != '1')
                {
                    return 505;  // HTTP Version Not Supported
                }
                if (*ptr == '\r')
                {
                    ptr++;
                }
                if (*ptr != '\n')
                {
                    return 400;
                }
                ptr++;

                // The request-line is valid: publish it.
                conn.request.method = std::move(method);
                conn.request.uri = std::move(uri);
                conn.request.protocol = std::move(protocol);

                // Headers
                conn.request.headers.clear();
                while (*ptr != '\r' && *ptr != '\n')
                {
                    // Name (a token; whitespace before ':' is not allowed - RFC 9112, 5.1)
                    begin = ptr;
                    while (*ptr && isTokenChar(static_cast<unsigned char>(*ptr)))
                    {
                        ptr++;
                    }
                    if (*ptr != ':')
                    {
                        return 400;
                    }

                    // Validate header name length
                    size_t nameLen = ptr - begin;
                    if (nameLen == 0 || nameLen > config::MAX_HEADER_NAME_LENGTH)
                    {
                        LOG_WARN("HTTP header name length invalid: %zu", nameLen);
                        return nameLen == 0 ? 400 : 431;
                    }

                    std::string name = normalizeHeaderName(begin, ptr);
                    ptr++;
                    while (*ptr == ' ' || *ptr == '\t')
                    {
                        ptr++;
                    }

                    // Value
                    begin = ptr;
                    while (*ptr && *ptr != '\r' && *ptr != '\n')
                    {
                        ptr++;
                    }
                    char const* valueEnd = ptr;
                    while (valueEnd > begin && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t'))
                    {
                        valueEnd--;
                    }

                    // Validate header value length
                    size_t valueLen = valueEnd - begin;
                    if (valueLen > config::MAX_HEADER_VALUE_LENGTH)
                    {
                        LOG_WARN("HTTP header value length invalid: %zu", valueLen);
                        return 431;
                    }

                    // Validate header value doesn't contain control characters (except tab)
                    for (const char* p = begin; p < valueEnd; ++p)
                    {
                        unsigned char c = static_cast<unsigned char>(*p);
                        if (c < 0x20 && c != '\t')  // Allow tab (0x09) but not other control chars
                        {
                            LOG_WARN("HTTP header value contains control character: 0x%02X", c);
                            return 400;
                        }
                        if (c == 0x7F)  // DEL character
                        {
                            LOG_WARN("HTTP header value contains DEL character");
                            return 400;
                        }
                    }

                    std::string value(begin, valueEnd);
                    auto existing = conn.request.headers.find(name);
                    if (existing == conn.request.headers.end())
                    {
                        conn.request.headers.emplace(std::move(name), std::move(value));
                    }
                    else if (name == constants::CONTENT_LENGTH || name == constants::HOST)
                    {
                        // Repeated framing/routing fields are a smuggling vector.
                        LOG_WARN("Duplicate %s header", name.c_str());
                        return 400;
                    }
                    else
                    {
                        // Combine repeated fields into a list (RFC 9110, 5.3)
                        existing->second += ", ";
                        existing->second += value;
                    }

                    if (*ptr == '\r')
                    {
                        ptr++;
                    }
                    if (*ptr != '\n')
                    {
                        return 400;
                    }
                    ptr++;
                }

                if (*ptr == '\r')
                {
                    ptr++;
                }
                if (*ptr != '\n')
                {
                    return 400;
                }
                ptr++;

                consumed = static_cast<size_t>(ptr - bufferStart);
                return 0;
            }

            /// @brief Case-insensitive equality of @p str with the lower-case string @p mask.
            static bool equalsLowercased(std::string const& str, char const* mask)
            {
                char const* ptr = str.c_str();
                while (*ptr && *mask && ::tolower(static_cast<unsigned char>(*ptr)) == static_cast<unsigned char>(*mask))
                {
                    ptr++;
                    mask++;
                }
                return !*ptr && !*mask;
            }

            /// @brief HttpRequest::normalize_header_name() for the range [begin, end).
            static std::string normalizeHeaderName(char const* begin, char const* end)
            {
                return HttpRequest::normalize_header_name(std::string(begin, end));
            }

            /// @brief Whether responses with this status never carry a body (1xx, 204, 304).
            static bool isBodylessStatus(int code)
            {
                return (code >= 100 && code < 200) || code == 204 || code == 304;
            }

            /// @brief Wire format of one streamed chunk: chunked framing, or raw data for
            ///        HTTP/1.0 (see Connection::chunkedStream).
            /// @param conn Connection being streamed to
            /// @param data Chunk payload returned by the stream callback
            /// @return Bytes to send
            static std::string frameStreamChunk(const Connection& conn, const std::string& data)
            {
                if (!conn.chunkedStream)
                {
                    return data;
                }
                std::ostringstream framed;
                framed << std::hex << data.size() << "\r\n" << data << "\r\n";
                return framed.str();
            }

            /// @brief End-of-stream marker: the terminal chunk, or nothing for HTTP/1.0
            ///        (closing the connection ends the body).
            /// @param conn Connection being streamed to
            /// @return Bytes to send
            static std::string streamTerminator(const Connection& conn)
            {
                return conn.chunkedStream ? std::string("0\r\n\r\n") : std::string();
            }

            /// @brief Serialize the status line and headers and move to SendingHeaders.
            void beginResponse(Connection& conn)
            {
                std::ostringstream os;
                os << responseProtocol(conn.request) << ' ' << conn.response.code << ' ' << conn.response.message
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

            /// @brief Run processRequest() for `conn` on the thread pool.
            ///
            /// The handlers work on a detached copy of the request, so the reactor can
            /// keep serving other connections (and may even close this one) meanwhile.
            /// When done, the worker re-locates the connection under m_connectionsMutex
            /// and starts sending the response itself. If the socket would block, the
            /// remainder is finished by the reactor on the next writable event; this
            /// also arms FD_WRITE on Windows, which only signals after WSAEWOULDBLOCK.
            /// Called with m_connectionsMutex held.
            void dispatchToThreadPool(Connection& conn)
            {
                auto work = std::make_shared<Connection>();
                work->request = std::move(conn.request);
                work->response = std::move(conn.response);
                work->keepalive = conn.keepalive;
                conn.request.client = work->request.client;  // keep for logging

                const uint64_t token = ++m_asyncSerial;
                conn.asyncToken = token;
                conn.state = Connection::ProcessingAsync;
                Socket sock = conn.socket;
                LOG_TRACE("HttpServer: [%s] processing on thread pool", conn.request.client.c_str());

                m_threadPool->detach_task([this, sock, token, work]() {
                    processRequest(*work);  // user handlers: no server lock held

                    std::lock_guard<std::mutex> lock(m_connectionsMutex);
                    if (m_stopped.load())
                    {
                        return;
                    }
                    auto it = m_connections.find(sock);
                    if (it == m_connections.end() || it->second.asyncToken != token ||
                        it->second.state != Connection::ProcessingAsync)
                    {
                        return;  // connection closed (fd possibly reused) meanwhile
                    }
                    Connection& c = it->second;
                    c.request = std::move(work->request);
                    c.response = std::move(work->response);
                    c.keepalive = work->keepalive;
                    c.closeRequested = work->closeRequested;
                    c.streamingActive = work->streamingActive;
                    c.chunksSent = work->chunksSent;
                    if (!c.closeRequested)
                    {
                        beginResponse(c);
                    }
                    handleConnection(c);  // may close and erase c - last use
                });
            }

            /// @brief Dispatch the request to the matching routes and finalize the
            ///        response headers.
            ///
            /// If conn.response.code is already non-zero (the request was rejected
            /// while being read), only the error response is prepared. Otherwise
            /// candidate routes run longest prefix first, then in registration order,
            /// until one handles the request (see CallbackFunction); -1 sets
            /// conn.closeRequested. Unhandled requests get the built-in OPTIONS /
            /// DELETE / 404 fallbacks. Finally Server, Connection, Date, CORS and
            /// framing headers are set; buffered bodies of HEAD and 1xx/204/304
            /// responses are dropped (a streaming response is sent as is).
            /// @note Runs user handlers: on the reactor thread with m_connectionsMutex
            ///       held, or on a pool worker without it (on a detached Connection).
            void processRequest(Connection& conn)
            {
                conn.response.message.clear();
                conn.response.headers.clear();
                conn.response.body.clear();
                conn.response.streaming = false;
                conn.response.useChunkedEncoding = false;
                conn.response.streamCallback = nullptr;
                conn.response.onStreamEnd = nullptr;

                // Store original method for HEAD handling
                std::string originalMethod = conn.request.method;
                bool isHeadRequest = (conn.request.method == "HEAD");

                // A non-zero code means the request was already rejected while
                // being read (400/413/431/...): only the error response is sent,
                // nothing is dispatched.
                if (conn.response.code == 0)
                {
                    // Treat HEAD as GET for processing, but skip body in response
                    if (isHeadRequest)
                    {
                        conn.request.method = "GET";
                    }

                    // Registered handlers run first, for every method. Candidates are the
                    // handlers whose path is a prefix of the URI, most specific (longest)
                    // first, then in registration order - so a "/" catch-all never shadows
                    // "/events". A handler handles the request - and later candidates are
                    // skipped - when it returns a non-zero status, sets a status itself
                    // (set_status()), or produces a response (a body or a streaming
                    // callback; the status then defaults to 200). Returning 0 without
                    // touching the response declines, so the next candidate runs.
                    conn.response.code = 0;
                    std::vector<HttpRequestHandler*> candidates;
                    for (auto& handler : m_handlers)
                    {
                        if (handler.second != nullptr &&
                            conn.request.uri.compare(0, handler.first.length(), handler.first) == 0)
                        {
                            candidates.push_back(&handler);
                        }
                    }
                    std::stable_sort(candidates.begin(), candidates.end(),
                        [](const HttpRequestHandler* a, const HttpRequestHandler* b) {
                            return a->first.length() > b->first.length();
                        });
                    for (HttpRequestHandler* handler : candidates)
                    {
                        LOG_TRACE("HttpServer: [%s] using handler for %s", conn.request.client.c_str(),
                            handler->first.c_str());
                        int result = handler->second->onHttpRequest(conn.request, conn.response);
                        if (result != 0)
                        {
                            conn.response.code = result;
                            break;
                        }
                        if (conn.response.code != 0)
                        {
                            break;  // status set via set_status()
                        }
                        if (!conn.response.body.empty() || conn.response.streamCallback)
                        {
                            conn.response.code = 200;
                            break;
                        }
                    }

                    if (conn.response.code == -1)
                    {
                        // Handler asked to drop the connection. Do NOT close here:
                        // the caller closes it as its last action (see handleConnection).
                        conn.request.method = originalMethod;
                        conn.closeRequested = true;
                        return;
                    }

                    if (conn.response.code == 0)
                    {
                        // No handler took the request: built-in fallbacks.
                        if (originalMethod == "OPTIONS")
                        {
                            // CORS preflight
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
                        else if (originalMethod == "DELETE")
                        {
                            // Session termination against the server's own SessionManager
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
                        else
                        {
                            conn.response.code = 404;  // Not Found
                        }
                    }

                    // Restore original method
                    conn.request.method = originalMethod;
                }

                if (conn.response.message.empty())
                {
                    conn.response.message = getDefaultResponseMessage(conn.response.code);
                }

                // Decide on connection persistence before emitting the Connection header,
                // so the header always matches what the server actually does.
                bool isStreaming = conn.response.streaming && conn.response.streamCallback;
                // HEAD never carries a body: answer a streaming route with its headers only.
                const bool headOfStream = isStreaming && isHeadRequest;
                if (headOfStream)
                {
                    isStreaming = false;
                    conn.response.streaming = false;
                    conn.response.streamCallback = nullptr;
                    conn.response.onStreamEnd = nullptr;
                }
                // Chunked transfer coding exists only in HTTP/1.1; an HTTP/1.0 client gets
                // the raw stream, delimited by closing the connection.
                const bool chunkedResponse = conn.request.protocol == constants::HTTP_1_1;
                if (isStreaming && !chunkedResponse)
                {
                    conn.keepalive = false;
                }
                conn.keepalive &= allowKeepalive;

                conn.response.headers.erase(constants::HOST);
                conn.response.headers["Server"] = m_serverHost;
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
                if (isStreaming)
                {
                    conn.streamingActive = true;
                    conn.chunksSent = 0;
                    conn.chunkedStream = chunkedResponse;

                    // The thread-pool path copies the callback for every chunk. Share
                    // one instance so a stateful callback (e.g. a mutable lambda that
                    // counts events) keeps its state across calls.
                    auto shared = std::make_shared<std::function<std::string()>>(
                        std::move(conn.response.streamCallback));
                    conn.response.streamCallback = [shared]() { return (*shared)(); };

                    // Use chunked encoding for streaming
                    if (chunkedResponse)
                    {
                        conn.response.headers[constants::TRANSFER_ENCODING] = constants::TRANSFER_ENCODING_CHUNKED;
                        conn.response.headers.erase(constants::CONTENT_LENGTH);
                    }

                    // SSE-specific headers
                    auto contentType = conn.response.headers.find(CONTENT_TYPE);
                    if (contentType != conn.response.headers.end() && contentType->second == CONTENT_TYPE_SSE)
                    {
                        conn.response.headers[constants::CACHE_CONTROL] = constants::CACHE_CONTROL_NO_CACHE;
                        conn.response.headers[constants::X_ACCEL_BUFFERING] = "no";  // Disable nginx buffering
                    }
                }
                else if (headOfStream)
                {
                    // Same headers as the GET would get, but no body
                    if (chunkedResponse)
                    {
                        conn.response.headers[constants::TRANSFER_ENCODING] = constants::TRANSFER_ENCODING_CHUNKED;
                    }
                    conn.response.headers.erase(constants::CONTENT_LENGTH);
                    conn.response.body.clear();
                }
                else if (isBodylessStatus(conn.response.code))
                {
                    // 1xx/204/304 never carry a body or Content-Length (RFC 9110, 8.6)
                    conn.response.headers.erase(constants::CONTENT_LENGTH);
                    conn.response.headers.erase(constants::TRANSFER_ENCODING);
                    conn.response.body.clear();
                }
                else
                {
                    conn.response.headers.erase(constants::TRANSFER_ENCODING);
                    conn.response.headers[constants::CONTENT_LENGTH] = std::to_string(conn.response.body.size());
                    // For HEAD requests, advertise the Content-Length but send no body
                    if (isHeadRequest)
                    {
                        conn.response.body.clear();
                    }
                }
            }

            /// @brief Format @p time as an IMF-fixdate for the Date header,
            ///        e.g. "Sun, 06 Nov 1994 08:49:37 GMT".
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
            /// @brief Standard reason phrase for an HTTP status code.
            /// @return e.g. "Not Found" for 404, or "???" for an unknown code.
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
