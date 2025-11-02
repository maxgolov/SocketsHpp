// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <string>
#include <vector>
#include <algorithm>

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
         */
        class TrustProxyConfig
        {
        public:
            enum class TrustMode
            {
                None,           // Don't trust any proxy headers
                TrustAll,       // Trust all proxy headers (use with caution!)
                TrustSpecific   // Trust only specific proxy IPs
            };

        private:
            TrustMode m_mode;
            std::vector<std::string> m_trustedProxies;

        public:
            TrustProxyConfig() : m_mode(TrustMode::None) {}

            explicit TrustProxyConfig(TrustMode mode) : m_mode(mode) {}

            TrustProxyConfig(const std::vector<std::string>& proxies)
                : m_mode(TrustMode::TrustSpecific), m_trustedProxies(proxies) {}

            /**
             * @brief Check if a given IP address is trusted.
             * @param remoteAddr The IP address to check
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
                    return std::find(m_trustedProxies.begin(), m_trustedProxies.end(),
                                   remoteAddr) != m_trustedProxies.end();
                }
                return false;
            }

            void setMode(TrustMode mode) { m_mode = mode; }
            void addTrustedProxy(const std::string& proxy) 
            { 
                m_trustedProxies.push_back(proxy);
                m_mode = TrustMode::TrustSpecific;
            }

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
         */
        class ProxyAwareHelpers
        {
        public:
            /**
             * @brief Extract the original protocol (http or https) from request.
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
                auto it = headers.find("X-Forwarded-Proto");
                if (it != headers.end())
                {
                    const auto& proto = it->second;
                    if (proto == "https" || proto == "http")
                    {
                        return proto;
                    }
                }

                // Check X-Forwarded-Protocol (alternative)
                it = headers.find("X-Forwarded-Protocol");
                if (it != headers.end())
                {
                    const auto& proto = it->second;
                    if (proto == "https" || proto == "http")
                    {
                        return proto;
                    }
                }

                // Check X-Forwarded-Ssl
                it = headers.find("X-Forwarded-Ssl");
                if (it != headers.end())
                {
                    return (it->second == "on") ? "https" : "http";
                }

                // Check Forwarded header (RFC 7239)
                it = headers.find("Forwarded");
                if (it != headers.end())
                {
                    auto proto = extractForwardedParam(it->second, "proto");
                    if (!proto.empty())
                    {
                        return proto;
                    }
                }

                return "http";
            }

            /**
             * @brief Extract the original client IP address from request.
             * @param headers Map of HTTP headers
             * @param remoteAddr Direct connection IP address
             * @param trustConfig Trust proxy configuration
             * @return Original client IP or direct connection IP if not trusted
             */
            template<typename HeaderMap>
            static std::string getClientIP(
                const HeaderMap& headers,
                const std::string& remoteAddr,
                const TrustProxyConfig& trustConfig)
            {
                // Only trust forwarded headers from trusted proxies
                if (!trustConfig.isTrusted(remoteAddr))
                {
                    return remoteAddr;
                }

                // Check X-Forwarded-For (most common)
                auto it = headers.find("X-Forwarded-For");
                if (it != headers.end())
                {
                    // X-Forwarded-For: client, proxy1, proxy2
                    // First IP is the original client
                    auto xff = it->second;
                    auto comma = xff.find(',');
                    auto clientIP = (comma != std::string::npos) 
                        ? xff.substr(0, comma) 
                        : xff;
                    
                    // Trim whitespace
                    clientIP.erase(0, clientIP.find_first_not_of(" \t"));
                    clientIP.erase(clientIP.find_last_not_of(" \t") + 1);
                    
                    if (!clientIP.empty())
                    {
                        return clientIP;
                    }
                }

                // Check X-Real-IP (nginx)
                it = headers.find("X-Real-IP");
                if (it != headers.end() && !it->second.empty())
                {
                    return it->second;
                }

                // Check Forwarded header (RFC 7239)
                it = headers.find("Forwarded");
                if (it != headers.end())
                {
                    auto forParam = extractForwardedParam(it->second, "for");
                    if (!forParam.empty())
                    {
                        // Remove quotes and port if present
                        if (forParam.front() == '"' && forParam.back() == '"')
                        {
                            forParam = forParam.substr(1, forParam.length() - 2);
                        }
                        // Strip port (e.g., "192.168.1.1:12345" -> "192.168.1.1")
                        auto colon = forParam.find(':');
                        if (colon != std::string::npos)
                        {
                            forParam = forParam.substr(0, colon);
                        }
                        return forParam;
                    }
                }

                return remoteAddr;
            }

            /**
             * @brief Extract the original host from request.
             * @param headers Map of HTTP headers
             * @param remoteAddr Direct connection IP address
             * @param trustConfig Trust proxy configuration
             * @param fallbackHost Fallback host if not found
             * @return Original host header value
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
                    auto it = headers.find("X-Forwarded-Host");
                    if (it != headers.end() && !it->second.empty())
                    {
                        return it->second;
                    }

                    // Check Forwarded header (RFC 7239)
                    it = headers.find("Forwarded");
                    if (it != headers.end())
                    {
                        auto hostParam = extractForwardedParam(it->second, "host");
                        if (!hostParam.empty())
                        {
                            return hostParam;
                        }
                    }
                }

                // Fallback to Host header
                auto it = headers.find("Host");
                if (it != headers.end() && !it->second.empty())
                {
                    return it->second;
                }

                return fallbackHost;
            }

            /**
             * @brief Check if the request was made over HTTPS.
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
            /**
             * @brief Extract a parameter from RFC 7239 Forwarded header.
             * Example: "for=192.0.2.60;proto=https;host=example.com"
             */
            static std::string extractForwardedParam(
                const std::string& forwarded,
                const std::string& param)
            {
                auto paramKey = param + "=";
                auto pos = forwarded.find(paramKey);
                if (pos == std::string::npos)
                {
                    return "";
                }

                pos += paramKey.length();
                auto end = forwarded.find_first_of(";,", pos);
                auto value = (end != std::string::npos)
                    ? forwarded.substr(pos, end - pos)
                    : forwarded.substr(pos);

                // Trim whitespace
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                return value;
            }
        };

    } // namespace server
} // namespace http
SOCKETSHPP_NS_END
