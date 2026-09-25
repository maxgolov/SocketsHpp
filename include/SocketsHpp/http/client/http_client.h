// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/net/common/socket_tools.h>
#include <SocketsHpp/http/common/url_parser.h>
#include <SocketsHpp/http/common/http_constants.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#  include <cerrno>
#  include <poll.h>
#  include <sys/time.h>
#endif

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace client
    {
        using Socket = net::utils::Socket;
        using SocketAddr = net::utils::SocketAddr;
        using SocketParams = net::utils::SocketParams;
        using UrlParser = http::common::UrlParser;

        // Import commonly used constants
        using constants::CONTENT_TYPE;
        using constants::CONTENT_LENGTH;
        using constants::TRANSFER_ENCODING;
        using constants::TRANSFER_ENCODING_CHUNKED;
        using constants::CONNECTION;
        using constants::CONNECTION_CLOSE;
        using constants::HOST;
        using constants::ACCEPT;
        using constants::HTTP_1_1;
        using constants::METHOD_GET;
        using constants::METHOD_POST;
        using constants::METHOD_PUT;
        using constants::METHOD_DELETE;
        using constants::METHOD_HEAD;
        using constants::METHOD_OPTIONS;
        using constants::METHOD_PATCH;

        namespace detail
        {
            /// ASCII-only lower-casing (locale independent; HTTP tokens are ASCII).
            inline char asciiLower(char c) noexcept
            {
                return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            }

            inline std::string asciiLower(const std::string& s)
            {
                std::string out(s);
                for (auto& c : out)
                {
                    c = asciiLower(c);
                }
                return out;
            }

            /// Case-insensitive (ASCII) string equality, as required for HTTP field names.
            inline bool iequals(const std::string& a, const std::string& b) noexcept
            {
                if (a.size() != b.size())
                {
                    return false;
                }
                for (size_t i = 0; i < a.size(); ++i)
                {
                    if (asciiLower(a[i]) != asciiLower(b[i]))
                    {
                        return false;
                    }
                }
                return true;
            }

            /// Remove every header whose name matches case-insensitively.
            template <typename Map>
            inline void eraseHeader(Map& headers, const std::string& name)
            {
                for (auto it = headers.begin(); it != headers.end();)
                {
                    if (iequals(it->first, name))
                        it = headers.erase(it);
                    else
                        ++it;
                }
            }

            /// Find a header by name, case-insensitively. Exact-case match is tried first.
            template <typename Map>
            inline auto findHeader(Map& headers, const std::string& name) -> decltype(headers.begin())
            {
                auto it = headers.find(name);
                if (it != headers.end())
                {
                    return it;
                }
                for (it = headers.begin(); it != headers.end(); ++it)
                {
                    if (iequals(it->first, name))
                    {
                        return it;
                    }
                }
                return headers.end();
            }

            inline bool isOws(char c) noexcept { return c == ' ' || c == '\t'; }

            inline std::string trimOws(const std::string& s)
            {
                size_t b = 0;
                size_t e = s.size();
                while (b < e && isOws(s[b]))
                {
                    ++b;
                }
                while (e > b && isOws(s[e - 1]))
                {
                    --e;
                }
                return s.substr(b, e - b);
            }

            /// Parse a non-negative decimal integer made only of ASCII digits.
            /// Rejects empty input, signs, whitespace and overflow.
            inline bool parseDecimal(const std::string& s, uint64_t& out) noexcept
            {
                if (s.empty())
                {
                    return false;
                }
                uint64_t value = 0;
                for (char c : s)
                {
                    if (c < '0' || c > '9')
                    {
                        return false;
                    }
                    const uint64_t digit = static_cast<uint64_t>(c - '0');
                    if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
                    {
                        return false;
                    }
                    value = value * 10 + digit;
                }
                out = value;
                return true;
            }

            /// Parse a Content-Length field value. Accepts surrounding whitespace and
            /// a comma-separated list of identical values (RFC 9110 8.6); rejects
            /// anything else (signs, garbage, differing values, overflow).
            inline bool parseContentLength(const std::string& value, uint64_t& out) noexcept
            {
                bool have = false;
                uint64_t result = 0;
                size_t start = 0;
                while (start <= value.size())
                {
                    size_t comma = value.find(',', start);
                    if (comma == std::string::npos)
                    {
                        comma = value.size();
                    }
                    uint64_t v = 0;
                    if (!parseDecimal(trimOws(value.substr(start, comma - start)), v))
                    {
                        return false;
                    }
                    if (have && v != result)
                    {
                        return false;
                    }
                    result = v;
                    have = true;
                    start = comma + 1;
                }
                out = result;
                return have;
            }

            /// Parse a chunk-size line: 1*HEXDIG [ BWS ";" chunk-ext ].
            inline bool parseChunkSize(const std::string& line, uint64_t& out) noexcept
            {
                uint64_t value = 0;
                size_t i = 0;
                for (; i < line.size(); ++i)
                {
                    const char c = line[i];
                    unsigned digit;
                    if (c >= '0' && c <= '9')
                        digit = static_cast<unsigned>(c - '0');
                    else if (c >= 'a' && c <= 'f')
                        digit = static_cast<unsigned>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F')
                        digit = static_cast<unsigned>(c - 'A' + 10);
                    else
                        break;
                    if (value > (std::numeric_limits<uint64_t>::max() >> 4))
                    {
                        return false;  // overflow
                    }
                    value = (value << 4) | digit;
                }
                if (i == 0)
                {
                    return false;  // no hex digits
                }
                while (i < line.size() && isOws(line[i]))
                {
                    ++i;
                }
                if (i < line.size() && line[i] != ';')
                {
                    return false;  // trailing garbage that is not a chunk extension
                }
                out = value;
                return true;
            }

            /// Components of an http(s) URL. Supports bracketed IPv6 literals.
            struct ParsedUrl
            {
                std::string scheme;
                std::string host;  // without IPv6 brackets
                uint16_t port = 0;
                bool explicitPort = false;
                std::string path;
                std::string query;  // without leading '?'
                bool ipv6 = false;

                static uint16_t defaultPort(const std::string& scheme)
                {
                    return (scheme == "https" || scheme == "wss") ? 443 : 80;
                }

                /// Host header value: brackets for IPv6, port only if not the scheme default.
                std::string hostHeader() const
                {
                    std::string h = ipv6 ? ("[" + host + "]") : host;
                    if (port != defaultPort(scheme))
                    {
                        h += ":" + std::to_string(port);
                    }
                    return h;
                }

                /// Request-target (origin-form): path plus "?query" when a query is present.
                std::string target() const { return query.empty() ? path : (path + "?" + query); }

                bool parse(const std::string& url)
                {
                    std::string rest = url;
                    scheme = "http";
                    const size_t sep = url.find("://");
                    if (sep != std::string::npos)
                    {
                        scheme = asciiLower(url.substr(0, sep));
                        rest = url.substr(sep + 3);
                    }

                    const size_t authEnd = rest.find_first_of("/?#");
                    std::string authority = rest.substr(0, authEnd);
                    std::string remainder = (authEnd == std::string::npos) ? std::string() : rest.substr(authEnd);

                    const size_t at = authority.rfind('@');
                    if (at != std::string::npos)
                    {
                        authority.erase(0, at + 1);  // credentials are not supported; drop them
                    }

                    std::string portStr;
                    bool hasPortSep = false;
                    if (!authority.empty() && authority[0] == '[')
                    {
                        const size_t close = authority.find(']');
                        if (close == std::string::npos)
                        {
                            return false;
                        }
                        host = authority.substr(1, close - 1);
                        ipv6 = true;
                        const std::string after = authority.substr(close + 1);
                        if (!after.empty())
                        {
                            if (after[0] != ':')
                            {
                                return false;
                            }
                            hasPortSep = true;
                            portStr = after.substr(1);
                        }
                    }
                    else if (std::count(authority.begin(), authority.end(), ':') > 1)
                    {
                        // Unbracketed IPv6 literal (no port possible).
                        host = authority;
                        ipv6 = true;
                    }
                    else
                    {
                        const size_t colon = authority.find(':');
                        host = authority.substr(0, colon);
                        if (colon != std::string::npos)
                        {
                            hasPortSep = true;
                            portStr = authority.substr(colon + 1);
                        }
                    }
                    if (host.empty())
                    {
                        return false;
                    }

                    port = defaultPort(scheme);
                    if (hasPortSep && !portStr.empty())
                    {
                        uint64_t p = 0;
                        if (!parseDecimal(portStr, p) || p == 0 || p > 65535)
                        {
                            return false;
                        }
                        port = static_cast<uint16_t>(p);
                        explicitPort = true;
                    }

                    const size_t hash = remainder.find('#');
                    if (hash != std::string::npos)
                    {
                        remainder.erase(hash);
                    }
                    const size_t q = remainder.find('?');
                    if (q != std::string::npos)
                    {
                        query = remainder.substr(q + 1);
                        remainder.erase(q);
                    }
                    path = remainder.empty() ? std::string("/") : remainder;
                    return true;
                }
            };

            inline bool isRedirectStatus(int code)
            {
                return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
            }

            inline bool sameOrigin(const ParsedUrl& a, const ParsedUrl& b)
            {
                return a.scheme == b.scheme && asciiLower(a.host) == asciiLower(b.host) && a.port == b.port;
            }

            /// Resolve a Location header against the URL of the request that produced it
            /// (absolute URL, scheme-relative "//host/path", absolute path, or relative path).
            inline std::string resolveLocation(const ParsedUrl& base, const std::string& location)
            {
                if (location.find("://") != std::string::npos)
                {
                    return location;
                }
                const std::string authority = base.scheme + "://" + base.hostHeader();
                if (location.compare(0, 2, "//") == 0)
                {
                    return base.scheme + ":" + location;
                }
                if (!location.empty() && location[0] == '/')
                {
                    return authority + location;
                }
                if (!location.empty() && location[0] == '?')
                {
                    return authority + base.path + location;
                }
                // Relative path: replace the last segment of the base path.
                const size_t slash = base.path.rfind('/');
                const std::string dir = (slash == std::string::npos) ? "/" : base.path.substr(0, slash + 1);
                return authority + dir + location;
            }
        }  // namespace detail

        /// @brief An HTTP request to be sent with HttpClient::send().
        ///
        /// Missing Host, User-Agent, Accept ("*/*"), Content-Length (for a non-empty body
        /// without Transfer-Encoding) and Connection ("close") headers are filled in by
        /// the client when the request is sent.
        struct HttpClientRequest
        {
            /// @brief Request method, sent verbatim (default "GET").
            std::string method = METHOD_GET;
            /// @brief Target URL, e.g. "http://host:8080/path?query". The scheme defaults
            /// to "http" when omitted; only "http" is accepted by HttpClient. Userinfo
            /// ("user:pass@") and any "#fragment" are dropped. Bracketed IPv6 literals
            /// are supported.
            std::string uri;
            /// @brief Protocol version written in the request line (default "HTTP/1.1").
            std::string protocol = HTTP_1_1;
            /// @brief Request headers, written as "Name: value" lines in map order. Keys are
            /// case-sensitive in the map; use setHeader() to replace case-insensitively.
            std::map<std::string, std::string> headers;
            /// @brief Request body, sent as-is after the header section.
            /// @note If you set "Transfer-Encoding: chunked" yourself, the body must
            ///       already be chunk-encoded; the client does not encode it.
            std::string body;

            /// @brief Set a header. Field names are case-insensitive: an existing header whose
            /// name differs only in case is replaced rather than duplicated.
            /// @param name Header field name.
            /// @param value Header field value.
            void setHeader(const std::string& name, const std::string& value)
            {
                auto it = detail::findHeader(headers, name);
                if (it != headers.end() && it->first != name)
                {
                    headers.erase(it);
                }
                headers[name] = value;
            }

            /// @brief Set the Content-Type header (see setHeader()).
            /// @param contentType Media type, e.g. "application/json".
            void setContentType(const std::string& contentType)
            {
                setHeader(CONTENT_TYPE, contentType);
            }

            /// @brief Set the User-Agent header for this request, overriding
            /// HttpClient::setUserAgent().
            /// @param userAgent User-Agent value.
            void setUserAgent(const std::string& userAgent)
            {
                setHeader("User-Agent", userAgent);
            }

            /// @brief Set the Accept header (the client sends "*/*" when none is set).
            /// @param accept Accept value, e.g. "text/event-stream".
            void setAccept(const std::string& accept)
            {
                setHeader(ACCEPT, accept);
            }
        };

        /// @brief Response filled in by HttpClient::send().
        ///
        /// Status, headers, body and isChunked are reset at the start of every exchange;
        /// the chunkCallback and onComplete callbacks are kept. After a redirect chain
        /// the object describes the last response received.
        struct HttpClientResponse
        {
            /// @brief Status code (e.g. 200); 0 until a status line has been parsed.
            int code = 0;
            /// @brief Reason phrase from the status line (may be empty).
            std::string message;
            /// @brief Protocol version from the status line, e.g. "HTTP/1.1".
            std::string protocol;
            /// @brief Response headers, keyed by the field name exactly as received from the
            /// server, with surrounding whitespace trimmed from values. HTTP field names are
            /// case-insensitive, so prefer getHeader() / hasHeader(), which match names
            /// case-insensitively. Repeated fields are combined into one entry with ", "
            /// (RFC 9110 5.3).
            std::map<std::string, std::string> headers;
            /// @brief Accumulated response body. Stays empty when chunkCallback is set.
            /// Limited by HttpClient::setMaxResponseBodySize().
            std::string body;

            /// @brief True if the response used "Transfer-Encoding: chunked" (as the final coding).
            bool isChunked = false;
            /// @brief Optional streaming sink. When set, body data is delivered here instead
            /// of being appended to `body`: once per HTTP chunk (decoded, without chunk
            /// framing) for chunked responses, and once per received buffer (up to 4 KiB,
            /// plus any bytes read together with the headers) for Content-Length and
            /// read-until-close responses. Never called with empty data.
            /// @note Invoked synchronously on the thread running HttpClient::send(). When
            ///       redirects are followed it is only called for the final response.
            std::function<void(const std::string&)> chunkCallback;
            /// @brief Optional callback invoked once after the whole body has been received
            /// successfully (also for responses without a body). Not called when the
            /// exchange fails; only called for the final response of a redirect chain.
            std::function<void()> onComplete;

            /// @brief Get a header value (case-insensitive name match).
            /// @param name Header field name.
            /// @return The (combined) value, or "" if the header is absent.
            std::string getHeader(const std::string& name) const
            {
                auto it = detail::findHeader(headers, name);
                return (it != headers.end()) ? it->second : "";
            }

            /// @brief Check header presence (case-insensitive name match).
            /// @param name Header field name.
            /// @return true if the response contains the header.
            bool hasHeader(const std::string& name) const
            {
                return detail::findHeader(headers, name) != headers.end();
            }
        };

        /// @brief Blocking HTTP/1.1 client (plain http only, no TLS).
        ///
        /// Each request opens a new TCP connection (IPv4/IPv6/DNS via getaddrinfo) and,
        /// unless the request sets its own Connection header, sends "Connection: close";
        /// there is no connection reuse. Follows redirects by default (see send()).
        ///
        /// @note Thread-safety: an instance runs one request at a time and its setters are
        ///       not synchronized. cancel() is the only member intended to be called from
        ///       another thread. Copying a client copies its settings only.
        class HttpClient
        {
        protected:
            /// @brief Default User-Agent, used when a request sets none.
            std::string m_userAgent = "SocketsHpp/1.1";
            /// @brief Connect timeout in ms (default 10 s); <= 0 means a plain blocking
            /// connect() limited only by the OS.
            int m_connectTimeoutMs = 10000;
            /// @brief Socket receive/send timeout in ms (default 30 s); <= 0 means none.
            int m_readTimeoutMs = 30000;
            /// @brief Whether send() follows 301/302/303/307/308 redirects (default true).
            bool m_followRedirects = true;
            /// @brief Maximum number of redirects followed per send() (default 10).
            int m_maxRedirects = 10;
            /// @brief Upper bound on a response body accumulated into HttpClientResponse::body,
            /// and on any single chunk of a chunked response (default 1 GiB).
            size_t m_maxResponseBodySize = static_cast<size_t>(1) << 30;  // 1 GiB

            /// @brief Maximum size of the status line + header section.
            static constexpr size_t kMaxHeaderBytes = 1024 * 1024;
            /// @brief Maximum length of a chunk-size line (including extensions) or trailer line.
            static constexpr size_t kMaxChunkLineBytes = 64 * 1024;

        public:
            /// @brief Create a client with default settings.
            HttpClient() = default;

            /// @brief Set the User-Agent sent when a request does not set one
            /// (default "SocketsHpp/1.1").
            /// @param userAgent User-Agent value.
            void setUserAgent(const std::string& userAgent) { m_userAgent = userAgent; }
            /// @brief Get the default User-Agent.
            /// @return The User-Agent sent when a request does not set one.
            const std::string& getUserAgent() const { return m_userAgent; }
            /// @brief Set the connect timeout in milliseconds (default 10000).
            /// @param ms Timeout; > 0 uses a non-blocking connect() + poll()/select(), applied
            ///           to each resolved address in turn; <= 0 uses a blocking connect()
            ///           limited only by the OS.
            /// @note DNS resolution (getaddrinfo) is not covered by this timeout.
            void setConnectTimeout(int ms) { m_connectTimeoutMs = ms; }
            /// @brief Set the socket receive and send timeout in milliseconds (default 30000).
            /// @param ms Applied as SO_RCVTIMEO and SO_SNDTIMEO; <= 0 means no timeout.
            /// @note The limit applies to each individual recv()/send() call, not to the whole
            ///       request; a timeout fails the request.
            void setReadTimeout(int ms) { m_readTimeoutMs = ms; }
            /// @brief Enable or disable following redirects (default enabled).
            /// @param follow false returns 3xx responses to the caller unchanged.
            void setFollowRedirects(bool follow) { m_followRedirects = follow; }
            /// @brief Set the maximum number of redirects followed per send() (default 10).
            /// @param max Maximum hops; <= 0 disables redirect following. Exceeding the limit
            ///            makes send() return false.
            void setMaxRedirects(int max) { m_maxRedirects = max; }
            /// @brief Set the maximum response body size in bytes (default 1 GiB).
            /// @param bytes Limit on the body accumulated into HttpClientResponse::body and on
            ///              any single chunk of a chunked response (the chunk limit also
            ///              applies when a chunkCallback is used). Exceeding it fails the request.
            void setMaxResponseBodySize(size_t bytes) { m_maxResponseBodySize = bytes; }

            /// @brief Abort the request currently in flight on this client (if any) from another
            /// thread by shutting its socket down. The blocked send() then returns false.
            /// @note Thread-safe. Only affects a request whose connection is already
            ///       established; it does not interrupt DNS resolution or connect(), and has
            ///       no effect on later requests.
            void cancel() { shutdownActiveSocket(false); }

            /// @brief Send a GET request (see send()).
            /// @param url Target URL (http:// only).
            /// @param response Receives the response.
            /// @return true if a complete response was received (any status code).
            bool get(const std::string& url, HttpClientResponse& response)
            {
                HttpClientRequest request;
                request.method = METHOD_GET;
                request.uri = url;
                return send(request, response);
            }

            /// @brief Send a POST request with a body and no Content-Type header (see send()).
            /// @param url Target URL (http:// only).
            /// @param body Request body.
            /// @param response Receives the response.
            /// @return true if a complete response was received (any status code).
            bool post(const std::string& url, const std::string& body, HttpClientResponse& response)
            {
                HttpClientRequest request;
                request.method = METHOD_POST;
                request.uri = url;
                request.body = body;
                return send(request, response);
            }

            /// @brief Send a request and read the response. Blocks until done, failed,
            /// timed out or cancel()ed.
            ///
            /// Redirects (301/302/303/307/308 with a Location header) are
            /// followed up to setMaxRedirects() times unless disabled with
            /// setFollowRedirects(false):
            ///  - 303, and 301/302 for methods other than GET/HEAD, switch to GET without
            ///    a body (HEAD stays HEAD); 307/308 repeat the original method and body.
            ///  - Authorization, Cookie and Proxy-Authorization are dropped when the
            ///    redirect leaves the original scheme/host/port.
            ///  - Relative Location values are resolved against the current URL.
            /// Streaming callbacks (chunkCallback/onComplete) only see the final response
            /// (which may itself be a 3xx that is not followed, e.g. without Location).
            /// Only plain "http" URLs are supported; https (TLS) is rejected rather than
            /// sent in cleartext.
            /// @param request Request to send. It is not modified: default headers (Host,
            ///        User-Agent, ...) are added to the copy that is sent.
            /// @param response Receives the (final) response; see HttpClientResponse.
            /// @return true if a complete response was received, whatever its status code.
            ///         false on an invalid or non-http URL, DNS/connect failure, send/receive
            ///         error or timeout, malformed response, size limit exceeded, cancel(),
            ///         an invalid redirect Location or too many redirects (errors are logged).
            bool send(HttpClientRequest& request, HttpClientResponse& response)
            {
                HttpClientRequest current = request;  // never modify the caller's request
                if (!m_followRedirects || m_maxRedirects <= 0)
                {
                    return sendOnce(current, response);
                }

                // Streaming callbacks must only see the final response. Output of a
                // redirect response is held back and delivered only if that response
                // turns out to be final (a 3xx that is not followed).
                auto userChunk = response.chunkCallback;
                auto userComplete = response.onComplete;
                std::string heldBody;
                bool heldComplete = false;
                auto restore = [&]() {
                    response.chunkCallback = userChunk;
                    response.onComplete = userComplete;
                };
                if (userChunk)
                {
                    response.chunkCallback = [&response, &heldBody, userChunk](const std::string& data) {
                        if (detail::isRedirectStatus(response.code))
                            heldBody += data;
                        else
                            userChunk(data);
                    };
                }
                if (userComplete)
                {
                    response.onComplete = [&response, &heldComplete, userComplete]() {
                        if (detail::isRedirectStatus(response.code))
                            heldComplete = true;
                        else
                            userComplete();
                    };
                }

                for (int hop = 0;; ++hop)
                {
                    heldBody.clear();
                    heldComplete = false;
                    const bool ok = sendOnce(current, response);
                    const std::string location = response.getHeader("Location");
                    if (!ok || !detail::isRedirectStatus(response.code) || location.empty())
                    {
                        restore();
                        if (ok && detail::isRedirectStatus(response.code))
                        {
                            // A redirect we do not follow is the final response.
                            if (userChunk && !heldBody.empty())
                                userChunk(heldBody);
                            if (userComplete && heldComplete)
                                userComplete();
                        }
                        return ok;
                    }
                    if (hop >= m_maxRedirects)
                    {
                        LOG_ERROR("HttpClient: Too many redirects (max %d)", m_maxRedirects);
                        restore();
                        return false;
                    }

                    detail::ParsedUrl from;
                    from.parse(current.uri);
                    const std::string next = detail::resolveLocation(from, location);
                    detail::ParsedUrl to;
                    if (!to.parse(next))
                    {
                        LOG_ERROR("HttpClient: Invalid redirect Location: %s", location.c_str());
                        restore();
                        return false;
                    }

                    // Rebuild the request for the next hop.
                    detail::eraseHeader(current.headers, HOST);  // recomputed for the new URL
                    if (response.code == 303 ||
                        ((response.code == 301 || response.code == 302) &&
                         current.method != METHOD_GET && current.method != METHOD_HEAD))
                    {
                        if (current.method != METHOD_HEAD)
                        {
                            current.method = METHOD_GET;
                        }
                        current.body.clear();
                        detail::eraseHeader(current.headers, CONTENT_LENGTH);
                        detail::eraseHeader(current.headers, CONTENT_TYPE);
                        detail::eraseHeader(current.headers, TRANSFER_ENCODING);
                    }
                    if (!detail::sameOrigin(from, to))
                    {
                        detail::eraseHeader(current.headers, "Authorization");
                        detail::eraseHeader(current.headers, "Proxy-Authorization");
                        detail::eraseHeader(current.headers, "Cookie");
                    }
                    current.uri = next;
                }
            }

        protected:
            /// @brief Send a single request/response exchange (no redirect handling).
            /// Adds missing default headers to `request`, connects, sends and reads the
            /// response; the socket is always closed before returning.
            /// @return true if a complete response was received.
            bool sendOnce(HttpClientRequest& request, HttpClientResponse& response)
            {
                detail::ParsedUrl url;
                if (!url.parse(request.uri))
                {
                    LOG_ERROR("HttpClient: Failed to parse URL: %s", request.uri.c_str());
                    return false;
                }
                if (url.scheme != "http")
                {
                    // No TLS support: never send an https request (and its credentials)
                    // in cleartext.
                    LOG_ERROR("HttpClient: Unsupported URL scheme '%s' (only http is supported)",
                              url.scheme.c_str());
                    return false;
                }

                // Set default headers (names are matched case-insensitively)
                auto hasReqHeader = [&request](const std::string& name) {
                    return detail::findHeader(request.headers, name) != request.headers.end();
                };
                if (!hasReqHeader(HOST))
                {
                    request.headers[HOST] = url.hostHeader();
                }
                if (!hasReqHeader("User-Agent"))
                {
                    request.headers["User-Agent"] = m_userAgent;
                }
                if (!hasReqHeader(ACCEPT))
                {
                    request.headers[ACCEPT] = "*/*";
                }
                if (!request.body.empty() && !hasReqHeader(CONTENT_LENGTH) && !hasReqHeader(TRANSFER_ENCODING))
                {
                    request.headers[CONTENT_LENGTH] = std::to_string(request.body.size());
                }
                if (!hasReqHeader(CONNECTION))
                {
                    request.headers[CONNECTION] = CONNECTION_CLOSE;
                }

                // Connect to server (socket is closed on every exit path by ScopedSocket)
                net::utils::ScopedSocket scoped{Socket()};
                if (!connectTo(url, scoped))
                {
                    LOG_ERROR("HttpClient: Failed to connect to %s:%d", url.host.c_str(), static_cast<int>(url.port));
                    return false;
                }
                Socket& socket = scoped.get();
                applyIoTimeouts(socket);

                // Publish the socket so cancel() can interrupt blocking I/O. The guard is
                // destroyed (and the socket unpublished) before `scoped` closes the handle.
                ActiveSocketGuard active(*this, socket);
                if (active.cancelled())
                {
                    return false;
                }

                // Build and send request
                std::ostringstream requestStream;
                requestStream << request.method << " " << url.target() << " " << request.protocol << "\r\n";
                for (const auto& header : request.headers)
                {
                    requestStream << header.first << ": " << header.second << "\r\n";
                }
                requestStream << "\r\n";

                const std::string requestStr = requestStream.str();
                if (!sendAll(socket, requestStr.data(), requestStr.size()))
                {
                    LOG_ERROR("HttpClient: Failed to send request headers");
                    return false;
                }
                if (!request.body.empty() && !sendAll(socket, request.body.data(), request.body.size()))
                {
                    LOG_ERROR("HttpClient: Failed to send request body");
                    return false;
                }

                return receiveResponse(socket, response, request.method);
            }

            // ---------------------------------------------------------------------
            // Active socket tracking (for cancel()/SSEClient::close())
            // ---------------------------------------------------------------------

            /// @brief Holds the socket of the in-flight request. Copying an HttpClient yields a
            /// fresh, idle slot (the mutex and the in-flight socket are never shared).
            struct ActiveSocketSlot
            {
                /// @brief Guards sock and stickyCancel.
                std::mutex mutex;
                /// @brief Socket of the in-flight request, or Socket::Invalid when idle.
                Socket::Type sock = Socket::Invalid;
                /// @brief When set, new requests are refused until clearCancel().
                bool stickyCancel = false;  // cancel also the next request(s) until cleared

                /// @brief Create an idle slot.
                ActiveSocketSlot() = default;
                /// @brief Copying yields an idle slot; nothing is copied.
                ActiveSocketSlot(const ActiveSocketSlot&) {}
                /// @brief Assignment is a no-op; the target keeps its own state.
                /// @return *this
                ActiveSocketSlot& operator=(const ActiveSocketSlot&) { return *this; }
            };
            /// @brief In-flight socket slot used by cancel() and shutdownActiveSocket().
            ActiveSocketSlot m_active;

            /// @brief RAII helper that publishes a connected socket in m_active for the
            /// duration of an exchange, so another thread can shut it down.
            class ActiveSocketGuard
            {
            public:
                /// @brief Publish `sock` unless a sticky cancel is pending.
                /// @param client Owning client.
                /// @param sock Connected socket of the current exchange.
                ActiveSocketGuard(HttpClient& client, Socket& sock) : m_client(client)
                {
                    std::lock_guard<std::mutex> lock(m_client.m_active.mutex);
                    m_cancelled = m_client.m_active.stickyCancel;
                    if (!m_cancelled)
                    {
                        m_client.m_active.sock = sock.m_sock;
                    }
                }
                /// @brief Unpublish the socket.
                ~ActiveSocketGuard()
                {
                    std::lock_guard<std::mutex> lock(m_client.m_active.mutex);
                    m_client.m_active.sock = Socket::Invalid;
                }
                ActiveSocketGuard(const ActiveSocketGuard&) = delete;
                ActiveSocketGuard& operator=(const ActiveSocketGuard&) = delete;
                /// @brief True if a sticky cancel was pending; the exchange must be abandoned.
                /// @return Whether the request was cancelled before it started.
                bool cancelled() const { return m_cancelled; }

            private:
                HttpClient& m_client;
                bool m_cancelled = false;
            };

            /// @brief Shut down the in-flight socket (unblocks recv/send in the request thread).
            /// If sticky, subsequent requests are refused until clearCancel() is called.
            /// @param sticky Also cancel requests started later (used by SSEClient::close()).
            /// @note Thread-safe.
            void shutdownActiveSocket(bool sticky)
            {
                std::lock_guard<std::mutex> lock(m_active.mutex);
                if (sticky)
                {
                    m_active.stickyCancel = true;
                }
                if (m_active.sock != Socket::Invalid)
                {
                    Socket(m_active.sock).shutdown(Socket::ShutdownBoth);
                }
            }

            /// @brief Clear a sticky cancel set by shutdownActiveSocket(true). Thread-safe.
            void clearCancel()
            {
                std::lock_guard<std::mutex> lock(m_active.mutex);
                m_active.stickyCancel = false;
            }

            // ---------------------------------------------------------------------
            // Low-level socket helpers
            // ---------------------------------------------------------------------

            /// @brief True if the last socket error was EINTR (always false on Windows).
            /// @param socket Socket whose last error is checked.
            /// @return Whether the failed call should be retried.
            static bool interrupted(Socket& socket)
            {
#ifdef _WIN32
                (void)socket;
                return false;
#else
                return socket.error() == EINTR;
#endif
            }

            /// @brief send() until all bytes are written or an error occurs (EINTR is retried).
            /// @return true if every byte was sent.
            static bool sendAll(Socket& socket, const char* data, size_t len)
            {
                while (len > 0)
                {
                    const size_t piece = std::min(len, static_cast<size_t>(1) << 30);
                    const int sent = socket.send(data, piece);
                    if (sent < 0 && interrupted(socket))
                    {
                        continue;
                    }
                    if (sent <= 0)
                    {
                        return false;
                    }
                    data += sent;
                    len -= static_cast<size_t>(sent);
                }
                return true;
            }

            /// @brief recv() retrying on EINTR.
            /// @return >0 bytes received, 0 on orderly EOF, <0 on error/timeout.
            static int recvSome(Socket& socket, char* buf, size_t size)
            {
                while (true)
                {
                    const int received = socket.recv(buf, size);
                    if (received < 0 && interrupted(socket))
                    {
                        continue;
                    }
                    return received;
                }
            }

            /// @brief Apply m_readTimeoutMs as SO_RCVTIMEO and SO_SNDTIMEO (no-op if <= 0).
            void applyIoTimeouts(Socket& socket) const
            {
                if (m_readTimeoutMs <= 0)
                {
                    return;
                }
#ifdef _WIN32
                DWORD tv = static_cast<DWORD>(m_readTimeoutMs);
#else
                timeval tv{};
                tv.tv_sec = static_cast<decltype(tv.tv_sec)>(m_readTimeoutMs / 1000);
                tv.tv_usec = static_cast<decltype(tv.tv_usec)>((m_readTimeoutMs % 1000) * 1000);
#endif
                ::setsockopt(socket.m_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
                ::setsockopt(socket.m_sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
            }

            /// @brief Switch a socket between blocking and non-blocking mode (errors ignored).
            /// @param sock Native socket handle.
            /// @param nonBlocking true for non-blocking mode.
            static void setNonBlockingMode(Socket::Type sock, bool nonBlocking)
            {
#ifdef _WIN32
                u_long value = nonBlocking ? 1 : 0;
                ::ioctlsocket(sock, FIONBIO, &value);
#else
                int flags = ::fcntl(sock, F_GETFL, 0);
                if (flags < 0)
                {
                    return;
                }
                flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
                ::fcntl(sock, F_SETFL, flags);
#endif
            }

            /// @brief connect() honoring m_connectTimeoutMs (non-blocking connect + poll/select).
            /// The socket is left in blocking mode.
            /// @return true if the connection was established within the timeout.
            bool connectWithTimeout(Socket::Type sock, const sockaddr* addr, size_t addrLen) const
            {
                if (m_connectTimeoutMs <= 0)
                {
                    return ::connect(sock, addr, static_cast<socklen_t>(addrLen)) == 0;
                }

                setNonBlockingMode(sock, true);
                bool ok = false;
#ifdef _WIN32
                if (::connect(sock, addr, static_cast<int>(addrLen)) == 0)
                {
                    ok = true;
                }
                else if (::WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    fd_set writeSet;
                    fd_set exceptSet;
                    FD_ZERO(&writeSet);
                    FD_ZERO(&exceptSet);
                    FD_SET(sock, &writeSet);
                    FD_SET(sock, &exceptSet);
                    timeval tv{};
                    tv.tv_sec = static_cast<long>(m_connectTimeoutMs / 1000);
                    tv.tv_usec = static_cast<long>((m_connectTimeoutMs % 1000) * 1000);
                    if (::select(0, nullptr, &writeSet, &exceptSet, &tv) > 0 && FD_ISSET(sock, &writeSet))
                    {
                        int soError = 0;
                        int len = sizeof(soError);
                        ok = ::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len) == 0 &&
                             soError == 0;
                    }
                }
#else
                if (::connect(sock, addr, static_cast<socklen_t>(addrLen)) == 0)
                {
                    ok = true;
                }
                else if (errno == EINPROGRESS || errno == EINTR)
                {
                    const auto deadline =
                        std::chrono::steady_clock::now() + std::chrono::milliseconds(m_connectTimeoutMs);
                    while (true)
                    {
                        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now()).count();
                        if (remaining <= 0)
                        {
                            break;
                        }
                        pollfd pfd{};
                        pfd.fd = sock;
                        pfd.events = POLLOUT;
                        const int rc = ::poll(&pfd, 1, static_cast<int>(remaining));
                        if (rc < 0 && errno == EINTR)
                        {
                            continue;
                        }
                        if (rc > 0)
                        {
                            int soError = 0;
                            socklen_t len = sizeof(soError);
                            ok = ::getsockopt(sock, SOL_SOCKET, SO_ERROR, &soError, &len) == 0 && soError == 0;
                        }
                        break;
                    }
                }
#endif
                setNonBlockingMode(sock, false);
                return ok;
            }

            /// @brief Resolve the URL host (IPv4, IPv6 or DNS name) and connect to the first
            /// address that accepts. On success `out` owns the connected socket.
            /// @return true if connected.
            bool connectTo(const detail::ParsedUrl& url, net::utils::ScopedSocket& out) const
            {
                addrinfo hints{};
                hints.ai_family = AF_UNSPEC;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_protocol = IPPROTO_TCP;
#ifdef AI_NUMERICSERV
                hints.ai_flags |= AI_NUMERICSERV;
#endif
                addrinfo* result = nullptr;
                const std::string service = std::to_string(url.port);
                if (::getaddrinfo(url.host.c_str(), service.c_str(), &hints, &result) != 0 || result == nullptr)
                {
                    LOG_ERROR("HttpClient: Failed to resolve host %s", url.host.c_str());
                    return false;
                }

                bool connected = false;
                for (addrinfo* ai = result; ai != nullptr && !connected; ai = ai->ai_next)
                {
                    try
                    {
                        net::utils::ScopedSocket candidate(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
                        if (connectWithTimeout(candidate.get().m_sock, ai->ai_addr, static_cast<size_t>(ai->ai_addrlen)))
                        {
                            out = std::move(candidate);
                            connected = true;
                        }
                    }
                    catch (const std::exception&)
                    {
                        // socket() failed for this family (e.g. no IPv6 support); try next.
                    }
                }
                ::freeaddrinfo(result);
                return connected;
            }

            // ---------------------------------------------------------------------
            // Response parsing
            // ---------------------------------------------------------------------

            /// @brief Deliver body bytes to the chunk callback, or append them to response.body.
            /// @return false if appending would exceed m_maxResponseBodySize.
            bool deliverBody(HttpClientResponse& response, const std::string& data) const
            {
                if (data.empty())
                {
                    return true;
                }
                if (response.chunkCallback)
                {
                    response.chunkCallback(data);
                    return true;
                }
                if (data.size() > m_maxResponseBodySize - std::min(m_maxResponseBodySize, response.body.size()))
                {
                    LOG_ERROR("HttpClient: Response body exceeds maximum size");
                    return false;
                }
                response.body.append(data);
                return true;
            }

            /// @brief Invoke response.onComplete (if set).
            /// @return Always true.
            static bool complete(HttpClientResponse& response)
            {
                if (response.onComplete)
                {
                    response.onComplete();
                }
                return true;
            }

            /// @brief Backward-compatible overload (assumes a request method with a response body).
            /// @return See receiveResponse(Socket&, HttpClientResponse&, const std::string&).
            bool receiveResponse(Socket& socket, HttpClientResponse& response)
            {
                return receiveResponse(socket, response, METHOD_GET);
            }

            /// @brief Read one response from `socket` into `response`.
            ///
            /// Skips interim 1xx responses (except 101), then reads the body framed by
            /// chunked Transfer-Encoding, Content-Length, or connection close. HEAD, 1xx,
            /// 204 and 304 responses have no body.
            /// @param socket Connected socket.
            /// @param response Reset, then filled in; its callbacks are invoked.
            /// @param method Request method (a HEAD response carries no body).
            /// @return true if a complete response was received.
            bool receiveResponse(Socket& socket, HttpClientResponse& response, const std::string& method)
            {
                response.code = 0;
                response.message.clear();
                response.protocol.clear();
                response.headers.clear();
                response.body.clear();
                response.isChunked = false;

                std::string buffer;
                char tempBuf[4096];

                while (true)
                {
                    // Read until we have the complete header section
                    size_t headerEnd = std::string::npos;
                    size_t termLen = 0;
                    size_t searchFrom = 0;
                    while (true)
                    {
                        const size_t crlf = buffer.find("\r\n\r\n", searchFrom);
                        const size_t lf = buffer.find("\n\n", searchFrom);
                        if (crlf != std::string::npos && (lf == std::string::npos || crlf < lf))
                        {
                            headerEnd = crlf;
                            termLen = 4;
                            break;
                        }
                        if (lf != std::string::npos)
                        {
                            headerEnd = lf;
                            termLen = 2;
                            break;
                        }
                        if (buffer.size() > kMaxHeaderBytes)
                        {
                            LOG_ERROR("HttpClient: Response header section too large");
                            return false;
                        }
                        searchFrom = buffer.size() > 3 ? buffer.size() - 3 : 0;
                        const int received = recvSome(socket, tempBuf, sizeof(tempBuf));
                        if (received <= 0)
                        {
                            LOG_ERROR("HttpClient: Connection closed while reading headers");
                            return false;
                        }
                        buffer.append(tempBuf, static_cast<size_t>(received));
                    }

                    if (!parseHeaders(buffer.substr(0, headerEnd + termLen), response))
                    {
                        LOG_ERROR("HttpClient: Malformed response header section");
                        return false;
                    }
                    buffer.erase(0, headerEnd + termLen);

                    // Interim 1xx responses (except 101 Switching Protocols) are followed by
                    // the final response on the same connection.
                    if (response.code >= 100 && response.code < 200 && response.code != 101)
                    {
                        continue;
                    }
                    break;
                }

                std::string bodyBuffer;
                bodyBuffer.swap(buffer);

                // Responses that never carry a body (RFC 9110 6.4.1)
                if (detail::iequals(method, METHOD_HEAD) || (response.code >= 100 && response.code < 200) ||
                    response.code == 204 || response.code == 304)
                {
                    return complete(response);
                }

                // Transfer-Encoding: chunked (must be the final coding) takes precedence
                const std::string transferEncoding = detail::asciiLower(response.getHeader(TRANSFER_ENCODING));
                if (!transferEncoding.empty())
                {
                    const size_t comma = transferEncoding.rfind(',');
                    const std::string lastCoding = detail::trimOws(
                        comma == std::string::npos ? transferEncoding : transferEncoding.substr(comma + 1));
                    if (lastCoding == TRANSFER_ENCODING_CHUNKED)
                    {
                        response.isChunked = true;
                        return receiveChunkedBody(socket, bodyBuffer, response);
                    }
                    return receiveUntilClose(socket, bodyBuffer, response);
                }

                if (response.hasHeader(CONTENT_LENGTH))
                {
                    uint64_t contentLength = 0;
                    if (!detail::parseContentLength(response.getHeader(CONTENT_LENGTH), contentLength))
                    {
                        LOG_ERROR("HttpClient: Invalid Content-Length");
                        return false;
                    }
                    if (contentLength > std::numeric_limits<size_t>::max() ||
                        (!response.chunkCallback && contentLength > m_maxResponseBodySize))
                    {
                        LOG_ERROR("HttpClient: Content-Length exceeds maximum body size");
                        return false;
                    }
                    return receiveFixedBody(socket, bodyBuffer, static_cast<size_t>(contentLength), response);
                }

                return receiveUntilClose(socket, bodyBuffer, response);
            }

            /// @brief Read the body until the peer closes the connection. A receive error or
            /// timeout (as opposed to an orderly close) fails the request.
            /// @return true on an orderly close.
            bool receiveUntilClose(Socket& socket, std::string& bodyBuffer, HttpClientResponse& response)
            {
                if (!deliverBody(response, bodyBuffer))
                {
                    return false;
                }
                char tempBuf[4096];
                while (true)
                {
                    const int received = recvSome(socket, tempBuf, sizeof(tempBuf));
                    if (received == 0)
                    {
                        break;
                    }
                    if (received < 0)
                    {
                        LOG_ERROR("HttpClient: Receive error or timeout while reading body");
                        return false;
                    }
                    if (!deliverBody(response, std::string(tempBuf, static_cast<size_t>(received))))
                    {
                        return false;
                    }
                }
                return complete(response);
            }

            /// @brief Parse a status line and header section (CRLF or LF line endings) into
            /// response.protocol/code/message/headers. Repeated fields are joined with ", ".
            /// @return false if the status line or a header line is malformed.
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
                if (response.protocol.compare(0, 5, "HTTP/") != 0)
                {
                    return false;
                }

                while (*ptr == ' ')
                {
                    ptr++;
                }

                // Status code: exactly three digits
                begin = ptr;
                while (*ptr && *ptr != ' ' && *ptr != '\r' && *ptr != '\n')
                {
                    ptr++;
                }
                std::string codeStr(begin, ptr);
                uint64_t code = 0;
                if (codeStr.size() != 3 || !detail::parseDecimal(codeStr, code) || code < 100)
                {
                    return false;
                }
                response.code = static_cast<int>(code);

                response.message.clear();
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

                // Parse header fields
                response.headers.clear();
                while (*ptr && *ptr != '\r' && *ptr != '\n')
                {
                    // Name
                    begin = ptr;
                    while (*ptr && *ptr != ':' && *ptr != '\r' && *ptr != '\n')
                    {
                        ptr++;
                    }
                    if (*ptr != ':' || ptr == begin)
                    {
                        return false;
                    }
                    std::string name(begin, ptr);
                    ptr++;

                    // Value (surrounding whitespace trimmed)
                    begin = ptr;
                    while (*ptr && *ptr != '\r' && *ptr != '\n')
                    {
                        ptr++;
                    }
                    std::string value = detail::trimOws(std::string(begin, ptr));

                    auto existing = detail::findHeader(response.headers, name);
                    if (existing != response.headers.end())
                    {
                        existing->second += ", " + value;
                    }
                    else
                    {
                        response.headers[name] = value;
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
                }

                return true;
            }

            /// @brief Read exactly `contentLength` body bytes (bytes beyond it are ignored).
            /// @return false if the connection ends early or the size limit is exceeded.
            bool receiveFixedBody(Socket& socket, std::string& bodyBuffer, size_t contentLength, HttpClientResponse& response)
            {
                if (bodyBuffer.size() > contentLength)
                {
                    bodyBuffer.resize(contentLength);  // ignore bytes past the declared length
                }
                size_t remaining = contentLength - bodyBuffer.size();
                if (!deliverBody(response, bodyBuffer))
                {
                    return false;
                }

                char tempBuf[4096];
                while (remaining > 0)
                {
                    const size_t toRead = std::min(sizeof(tempBuf), remaining);
                    const int received = recvSome(socket, tempBuf, toRead);
                    if (received <= 0)
                    {
                        LOG_ERROR("HttpClient: Connection closed before receiving complete body");
                        return false;
                    }
                    remaining -= static_cast<size_t>(received);
                    if (!deliverBody(response, std::string(tempBuf, static_cast<size_t>(received))))
                    {
                        return false;
                    }
                }

                return complete(response);
            }

            /// @brief Read one line (terminated by LF, optional preceding CR) from buffer+socket.
            /// On success the line (without terminator) is removed from the buffer.
            /// @return false on EOF/error or a line longer than kMaxChunkLineBytes.
            static bool readLine(Socket& socket, std::string& buffer, std::string& line)
            {
                size_t lineEnd = buffer.find('\n');
                while (lineEnd == std::string::npos)
                {
                    if (buffer.size() > kMaxChunkLineBytes)
                    {
                        return false;
                    }
                    char tempBuf[4096];
                    const int received = recvSome(socket, tempBuf, sizeof(tempBuf));
                    if (received <= 0)
                    {
                        return false;
                    }
                    const size_t oldSize = buffer.size();
                    buffer.append(tempBuf, static_cast<size_t>(received));
                    lineEnd = buffer.find('\n', oldSize);
                }
                if (lineEnd > kMaxChunkLineBytes)
                {
                    return false;
                }
                line.assign(buffer, 0, lineEnd);
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                buffer.erase(0, lineEnd + 1);
                return true;
            }

            /// @brief Decode a chunked body, delivering each chunk via deliverBody(); chunk
            /// extensions and trailers are ignored.
            /// @return false on malformed framing, an oversized chunk or early EOF before
            ///         the last chunk.
            bool receiveChunkedBody(Socket& socket, std::string& buffer, HttpClientResponse& response)
            {
                // Chunked encoding: <size in hex>[;ext]\r\n<data>\r\n ... 0\r\n[trailers]\r\n
                std::string line;
                while (true)
                {
                    if (!readLine(socket, buffer, line))
                    {
                        LOG_ERROR("HttpClient: Connection closed or invalid line while reading chunk size");
                        return false;
                    }

                    uint64_t chunkSize64 = 0;
                    if (!detail::parseChunkSize(line, chunkSize64))
                    {
                        LOG_ERROR("HttpClient: Invalid chunk size");
                        return false;
                    }
                    if (chunkSize64 > m_maxResponseBodySize ||
                        chunkSize64 > std::numeric_limits<size_t>::max() - 2)
                    {
                        LOG_ERROR("HttpClient: Chunk size exceeds maximum");
                        return false;
                    }
                    const size_t chunkSize = static_cast<size_t>(chunkSize64);

                    if (chunkSize == 0)
                    {
                        // Last chunk: consume the (optional) trailer section up to the
                        // empty line. The body is complete either way, so a peer that
                        // closes early is tolerated.
                        while (readLine(socket, buffer, line) && !line.empty())
                        {
                        }
                        return complete(response);
                    }

                    // Read chunk data plus the trailing CRLF
                    while (buffer.size() < chunkSize + 2)
                    {
                        char tempBuf[4096];
                        const int received = recvSome(socket, tempBuf, sizeof(tempBuf));
                        if (received <= 0)
                        {
                            LOG_ERROR("HttpClient: Connection closed while reading chunk data");
                            return false;
                        }
                        buffer.append(tempBuf, static_cast<size_t>(received));
                    }
                    if (buffer[chunkSize] != '\r' || buffer[chunkSize + 1] != '\n')
                    {
                        LOG_ERROR("HttpClient: Missing CRLF after chunk data");
                        return false;
                    }

                    std::string chunkData = buffer.substr(0, chunkSize);
                    buffer.erase(0, chunkSize + 2);

                    if (!deliverBody(response, chunkData))
                    {
                        return false;
                    }
                }
            }
        };

    }  // namespace client
}  // namespace http
SOCKETSHPP_NS_END
