// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace server
    {
        /**
         * @brief Trust proxy configuration for proxy-aware HTTP servers.
         * 
         * When running behind a reverse proxy (nginx, Apache, load balancer),
         * the server loses visibility of the original client connection details.
         * This class helps configure which proxies to trust for forwarded headers.
         * @note Trusted proxies are matched by exact IP string (no CIDR ranges, no
         *       IPv6 canonicalization). Not synchronized: configure before use; the
         *       const members are safe to call concurrently afterwards.
         */
        class TrustProxyConfig
        {
        public:
            /// @brief Which peers are trusted to supply forwarded headers.
            enum class TrustMode
            {
                None,           ///< Don't trust any proxy headers
                TrustAll,       ///< Trust all proxy headers (use with caution: clients can then spoof them)
                TrustSpecific   ///< Trust only specific proxy IPs
            };

        private:
            TrustMode m_mode;
            std::vector<std::string> m_trustedProxies;

        public:
            /// @brief Trust nobody (TrustMode::None).
            TrustProxyConfig() : m_mode(TrustMode::None) {}

            /// @brief Use @p mode with an empty proxy list.
            explicit TrustProxyConfig(TrustMode mode) : m_mode(mode) {}

            /// @brief Trust only @p proxies (TrustMode::TrustSpecific). Entries may
            ///        carry a port, which is ignored.
            TrustProxyConfig(const std::vector<std::string>& proxies)
                : m_mode(TrustMode::TrustSpecific), m_trustedProxies(proxies) {}

            /**
             * @brief Reduce an address to its bare IP / host part.
             *
             * Accepts "1.2.3.4", "1.2.3.4:5678", "::1", "[::1]", "[::1]:5678" and
             * quoted forms ("\"[::1]:80\"") as used by the Forwarded header, and
             * returns "1.2.3.4" / "::1". HttpRequest::client is "ip:port", so this
             * is needed before comparing against a list of trusted proxy IPs.
             */
            static std::string stripPort(const std::string& address)
            {
                std::string a = address;
                a.erase(0, a.find_first_not_of(" \t"));
                a.erase(a.find_last_not_of(" \t") + 1);
                if (a.size() >= 2 && a.front() == '"' && a.back() == '"')
                {
                    a = a.substr(1, a.size() - 2);
                }
                if (!a.empty() && a.front() == '[')
                {
                    auto close = a.find(']');
                    return (close != std::string::npos) ? a.substr(1, close - 1) : a;
                }
                auto colon = a.find(':');
                if (colon != std::string::npos && a.find(':', colon + 1) == std::string::npos)
                {
                    return a.substr(0, colon);  // IPv4 (or host name) with port
                }
                return a;  // No port, or a bare IPv6 address
            }

            /**
             * @brief Check if a given IP address is trusted.
             * @param remoteAddr The IP address to check; a trailing port
             *        ("ip:port" / "[v6]:port") is ignored.
             * @return true if the address should be trusted
             */
            bool isTrusted(const std::string& remoteAddr) const
            {
                switch (m_mode)
                {
                case TrustMode::None:
                    return false;
                case TrustMode::TrustAll:
                    return true;
                case TrustMode::TrustSpecific:
                {
                    const std::string ip = stripPort(remoteAddr);
                    if (ip.empty())
                    {
                        return false;
                    }
                    for (const auto& proxy : m_trustedProxies)
                    {
                        if (stripPort(proxy) == ip)
                        {
                            return true;
                        }
                    }
                    return false;
                }
                }
                return false;
            }

            /// @brief Set the trust mode (the proxy list is kept).
            void setMode(TrustMode mode) { m_mode = mode; }
            /// @brief Add a trusted proxy address and switch to TrustMode::TrustSpecific.
            void addTrustedProxy(const std::string& proxy) 
            { 
                m_trustedProxies.push_back(proxy);
                m_mode = TrustMode::TrustSpecific;
            }

            /// @brief Get the trusted proxy addresses as added.
            const std::vector<std::string>& getTrustedProxies() const 
            { 
                return m_trustedProxies; 
            }
        };

        /**
         * @brief Helper functions for proxy-aware request handling.
         * 
         * These utilities help extract original client information from
         * proxy-forwarded headers like X-Forwarded-Proto, X-Forwarded-For, etc.
         * Forwarded headers are only consulted when the direct peer (remoteAddr) is
         * trusted. HeaderMap is any map from header name to value, e.g.
         * HttpRequest::headers; names are matched case-insensitively. Pass
         * HttpRequest::client as remoteAddr (its port is ignored).
         */
        class ProxyAwareHelpers
        {
        public:
            /**
             * @brief Extract the original protocol (http or https) from request.
             *
             * Checks, in order, X-Forwarded-Proto and X-Forwarded-Protocol (values
             * other than http/https are skipped), X-Forwarded-Ssl (if present it
             * decides: "on" = https, anything else = http), and the first proto=
             * parameter of Forwarded.
             * @param headers Map of HTTP headers
             * @param remoteAddr Direct connection IP address
             * @param trustConfig Trust proxy configuration
             * @return "https" if original request was HTTPS, "http" otherwise
             */
            template<typename HeaderMap>
            static std::string getProtocol(
                const HeaderMap& headers,
                const std::string& remoteAddr,
                const TrustProxyConfig& trustConfig)
            {
                // Only trust forwarded headers from trusted proxies
                if (!trustConfig.isTrusted(remoteAddr))
                {
                    return "http"; // Default for direct connections
                }

                // Check X-Forwarded-Proto (most common)
                if (const std::string* value = findHeader(headers, "X-Forwarded-Proto"))
                {
                    const auto proto = toLower(trim(*value));
                    if (proto == "https" || proto == "http")
                    {
                        return proto;
                    }
                }

                // Check X-Forwarded-Protocol (alternative)
                if (const std::string* value = findHeader(headers, "X-Forwarded-Protocol"))
                {
                    const auto proto = toLower(trim(*value));
                    if (proto == "https" || proto == "http")
                    {
                        return proto;
                    }
                }

                // Check X-Forwarded-Ssl
                if (const std::string* value = findHeader(headers, "X-Forwarded-Ssl"))
                {
                    return (toLower(trim(*value)) == "on") ? "https" : "http";
                }

                // Check Forwarded header (RFC 7239)
                if (const std::string* value = findHeader(headers, "Forwarded"))
                {
                    auto proto = toLower(extractForwardedParam(*value, "proto"));
                    if (proto == "https" || proto == "http")
                    {
                        return proto;
                    }
                }

                return "http";
            }

            /**
             * @brief Extract the original client IP address from request.
             *
             * Uses X-Forwarded-For, then X-Real-IP, then the for= parameters of
             * Forwarded. For the list headers the hops are walked right to left and
             * the first address that is not a trusted proxy is returned (if all are
             * trusted, the left-most one).
             * @param headers Map of HTTP headers
             * @param remoteAddr Direct connection address ("ip:port" accepted)
             * @param trustConfig Trust proxy configuration
             * @return Original client IP (port stripped), or the direct connection IP
             *         if the peer is not trusted or no forwarded address is present
             * @warning With TrustMode::TrustAll every hop is trusted, so the result is
             *          the left-most, client-controlled X-Forwarded-For entry.
             */
            template<typename HeaderMap>
            static std::string getClientIP(
                const HeaderMap& headers,
                const std::string& remoteAddr,
                const TrustProxyConfig& trustConfig)
            {
                const std::string directIP = TrustProxyConfig::stripPort(remoteAddr);

                // Only trust forwarded headers from trusted proxies
                if (!trustConfig.isTrusted(remoteAddr))
                {
                    return directIP;
                }

                // X-Forwarded-For: client, proxy1, proxy2
                // Each proxy appends the address it received the request from, so
                // only the right-most entries (added by our own trusted proxies) are
                // reliable; anything to their left may have been forged by the
                // client. Walk right-to-left and return the first address that is
                // not a trusted proxy.
                if (const std::string* xff = findHeader(headers, "X-Forwarded-For"))
                {
                    auto clientIP = rightmostUntrusted(splitList(*xff), trustConfig);
                    if (!clientIP.empty())
                    {
                        return clientIP;
                    }
                }

                // Check X-Real-IP (nginx)
                if (const std::string* realIP = findHeader(headers, "X-Real-IP"))
                {
                    auto ip = TrustProxyConfig::stripPort(*realIP);
                    if (!ip.empty())
                    {
                        return ip;
                    }
                }

                // Check Forwarded header (RFC 7239): one element per hop, same
                // right-to-left rule as X-Forwarded-For.
                if (const std::string* forwarded = findHeader(headers, "Forwarded"))
                {
                    std::vector<std::string> hops;
                    for (const auto& element : splitList(*forwarded))
                    {
                        auto forParam = extractForwardedParam(element, "for");
                        if (!forParam.empty())
                        {
                            hops.push_back(forParam);
                        }
                    }
                    auto clientIP = rightmostUntrusted(hops, trustConfig);
                    if (!clientIP.empty())
                    {
                        return clientIP;
                    }
                }

                return directIP;
            }

            /**
             * @brief Extract the original host from request.
             * @param headers Map of HTTP headers
             * @param remoteAddr Direct connection address ("ip:port" accepted)
             * @param trustConfig Trust proxy configuration
             * @param fallbackHost Fallback host if not found
             * @return For a trusted peer, X-Forwarded-Host (verbatim) or the first
             *         host= parameter of Forwarded; otherwise the Host header; else
             *         @p fallbackHost. May include a port.
             */
            template<typename HeaderMap>
            static std::string getHost(
                const HeaderMap& headers,
                const std::string& remoteAddr,
                const TrustProxyConfig& trustConfig,
                const std::string& fallbackHost = "localhost")
            {
                // Check X-Forwarded-Host if from trusted proxy
                if (trustConfig.isTrusted(remoteAddr))
                {
                    const std::string* value = findHeader(headers, "X-Forwarded-Host");
                    if (value && !value->empty())
                    {
                        return *value;
                    }

                    // Check Forwarded header (RFC 7239)
                    value = findHeader(headers, "Forwarded");
                    if (value)
                    {
                        auto hostParam = extractForwardedParam(*value, "host");
                        if (!hostParam.empty())
                        {
                            return hostParam;
                        }
                    }
                }

                // Fallback to Host header
                const std::string* host = findHeader(headers, "Host");
                if (host && !host->empty())
                {
                    return *host;
                }

                return fallbackHost;
            }

            /**
             * @brief Check if the request was made over HTTPS.
             * @return true if getProtocol() returns "https".
             */
            template<typename HeaderMap>
            static bool isSecure(
                const HeaderMap& headers,
                const std::string& remoteAddr,
                const TrustProxyConfig& trustConfig)
            {
                return getProtocol(headers, remoteAddr, trustConfig) == "https";
            }

        private:
            static std::string trim(const std::string& value)
            {
                auto first = value.find_first_not_of(" \t");
                if (first == std::string::npos)
                {
                    return std::string();
                }
                auto last = value.find_last_not_of(" \t");
                return value.substr(first, last - first + 1);
            }

            static std::string toLower(std::string value)
            {
                for (char& c : value)
                {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                return value;
            }

            static bool iequals(const std::string& a, const std::string& b)
            {
                return a.size() == b.size() &&
                    std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                        return std::tolower(static_cast<unsigned char>(x)) ==
                               std::tolower(static_cast<unsigned char>(y));
                    });
            }

            /// Header lookup that works for both exact-case maps and HttpRequest's
            /// Title-Case-normalized map (where "X-Real-IP" is stored as "X-Real-Ip").
            template<typename HeaderMap>
            static const std::string* findHeader(const HeaderMap& headers, const std::string& name)
            {
                auto it = headers.find(name);
                if (it != headers.end())
                {
                    return &it->second;
                }
                for (const auto& entry : headers)
                {
                    if (iequals(entry.first, name))
                    {
                        return &entry.second;
                    }
                }
                return nullptr;
            }

            /// Split a comma-separated header value into trimmed, non-empty items.
            static std::vector<std::string> splitList(const std::string& value)
            {
                std::vector<std::string> items;
                size_t start = 0;
                for (;;)
                {
                    auto comma = value.find(',', start);
                    auto item = trim(value.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
                    if (!item.empty())
                    {
                        items.push_back(item);
                    }
                    if (comma == std::string::npos)
                    {
                        break;
                    }
                    start = comma + 1;
                }
                return items;
            }

            /// Walk a hop list right-to-left and return the first address that is
            /// not a trusted proxy (port stripped). If every hop is trusted, the
            /// left-most one is returned. Empty if the list is empty.
            static std::string rightmostUntrusted(const std::vector<std::string>& hops,
                                                  const TrustProxyConfig& trustConfig)
            {
                std::string candidate;
                for (auto it = hops.rbegin(); it != hops.rend(); ++it)
                {
                    auto ip = TrustProxyConfig::stripPort(*it);
                    if (ip.empty())
                    {
                        continue;
                    }
                    candidate = ip;
                    if (!trustConfig.isTrusted(candidate))
                    {
                        return candidate;
                    }
                }
                return candidate;
            }

            /**
             * @brief Extract a parameter from one RFC 7239 Forwarded element.
             * Example: "for=192.0.2.60;proto=https;host=example.com"
             * Parameter names are case-insensitive; quoted values are unquoted.
             */
            static std::string extractForwardedParam(
                const std::string& forwarded,
                const std::string& param)
            {
                size_t start = 0;
                for (;;)
                {
                    auto end = forwarded.find_first_of(";,", start);
                    auto pair = trim(forwarded.substr(start, end == std::string::npos ? std::string::npos : end - start));
                    auto eq = pair.find('=');
                    if (eq != std::string::npos && iequals(trim(pair.substr(0, eq)), param))
                    {
                        auto value = trim(pair.substr(eq + 1));
                        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                        {
                            value = value.substr(1, value.size() - 2);
                        }
                        return value;
                    }
                    if (end == std::string::npos)
                    {
                        break;
                    }
                    start = end + 1;
                }
                return "";
            }
        };

    } // namespace server
} // namespace http
SOCKETSHPP_NS_END
