// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>
#include <string_view>

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace common
    {
        /// @brief Minimal URL splitter; parses in the constructor and exposes the parts
        /// as public fields.
        ///
        /// Accepted forms include:
        /// - http://user:password@host:port/path1/path2?key1=val2&key2=val2 (userinfo is dropped)
        /// - http://host:port/path1/path2?key1=val1&key2=val2
        /// - http://[::1]:8080/path
        /// - host:port/path1
        /// - host:port (path defaults to "/")
        /// - host:port?
        ///
        /// Only the authority (between "scheme://" and the first '/', '?' or '#')
        /// is searched for userinfo ('@') and the port (':'), so characters in the
        /// path or query can never change the host. No percent-decoding is done.
        ///
        /// @note Check success_ before using the other fields; after a failure they may
        ///       be partially filled.
        class UrlParser
        {
        public:
            /// @brief The original URL, unchanged.
            std::string url_;
            /// @brief Host name, IPv4 literal, or IPv6 literal *without* brackets.
            std::string host_;
            /// @brief Lower-cased scheme ("http" when absent).
            std::string scheme_;
            /// @brief Path ("/" when absent); no query or fragment.
            std::string path_;
            /// @brief Explicit port, else the scheme default (http/ws 80, https/wss 443),
            /// else 0. An empty port ("host:") also means the default.
            uint16_t port_ = 0;
            /// @brief Text after '?' up to '#' (no leading '?'); empty if none.
            std::string query_;
            /// @brief False if the URL is empty or malformed (bad scheme, empty host, bad
            /// IPv6 literal, non-numeric or out-of-range port).
            bool success_ = false;

            /// @brief Parse `url`; the result is reported in success_.
            /// @param url URL to parse. Never throws on malformed input.
            UrlParser(std::string url) : url_(std::move(url))
            {
                success_ = parse();
            }

        private:
            static uint16_t defaultPort(const std::string& scheme)
            {
                if (scheme == "http" || scheme == "ws")
                {
                    return 80;
                }
                if (scheme == "https" || scheme == "wss")
                {
                    return 443;
                }
                return 0;
            }

            static bool isValidScheme(const std::string& scheme)
            {
                if (scheme.empty() || !std::isalpha(static_cast<unsigned char>(scheme[0])))
                {
                    return false;
                }
                for (char c : scheme)
                {
                    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.')
                    {
                        return false;
                    }
                }
                return true;
            }

            /// @brief Parse a decimal port (1-5 digits, <= 65535).
            static bool parsePort(const std::string& text, uint16_t& port)
            {
                if (text.empty() || text.size() > 5)
                {
                    return false;
                }
                unsigned long value = 0;
                for (char c : text)
                {
                    if (c < '0' || c > '9')
                    {
                        return false;
                    }
                    value = value * 10 + static_cast<unsigned long>(c - '0');
                }
                if (value > 65535)
                {
                    return false;
                }
                port = static_cast<uint16_t>(value);
                return true;
            }

            bool parse()
            {
                if (url_.empty())
                {
                    return false;
                }

                // Scheme: only if "://" appears before any path/query/fragment delimiter.
                size_t cpos = 0;
                size_t schemeEnd = url_.find("://");
                size_t firstDelim = url_.find_first_of("/?#");
                if (schemeEnd != std::string::npos && (firstDelim == std::string::npos || schemeEnd <= firstDelim))
                {
                    scheme_ = url_.substr(0, schemeEnd);
                    for (char& c : scheme_)
                    {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    if (!isValidScheme(scheme_))
                    {
                        return false;
                    }
                    cpos = schemeEnd + 3;
                }
                else
                {
                    scheme_ = "http";  // scheme missing, use default
                }
                port_ = defaultPort(scheme_);

                // Authority ends at the first '/', '?' or '#'.
                size_t authorityEnd = url_.find_first_of("/?#", cpos);
                if (authorityEnd == std::string::npos)
                {
                    authorityEnd = url_.size();
                }
                std::string authority = url_.substr(cpos, authorityEnd - cpos);

                // Userinfo (credentials) - ignored; the last '@' ends it.
                size_t at = authority.rfind('@');
                if (at != std::string::npos)
                {
                    authority.erase(0, at + 1);
                }

                // Host and optional port
                std::string portText;
                bool hasPort = false;
                if (!authority.empty() && authority[0] == '[')
                {
                    // IPv6 literal: "[addr]" or "[addr]:port"
                    size_t close = authority.find(']');
                    if (close == std::string::npos)
                    {
                        return false;
                    }
                    host_ = authority.substr(1, close - 1);
                    if (host_.empty())
                    {
                        return false;
                    }
                    for (char c : host_)
                    {
                        if (!std::isxdigit(static_cast<unsigned char>(c)) && c != ':' && c != '.' && c != '%')
                        {
                            return false;
                        }
                    }
                    std::string rest = authority.substr(close + 1);
                    if (!rest.empty())
                    {
                        if (rest[0] != ':')
                        {
                            return false;
                        }
                        hasPort = true;
                        portText = rest.substr(1);
                    }
                }
                else
                {
                    size_t colon = authority.find(':');
                    if (colon != std::string::npos)
                    {
                        hasPort = true;
                        portText = authority.substr(colon + 1);
                        authority.erase(colon);
                    }
                    host_ = authority;
                }
                if (host_.empty())
                {
                    return false;
                }
                // An empty port ("host:") means the scheme default (RFC 3986, 3.2.3).
                if (hasPort && !portText.empty() && !parsePort(portText, port_))
                {
                    return false;
                }

                // Path, query, fragment
                size_t fragment = url_.find('#', authorityEnd);
                size_t end = (fragment == std::string::npos) ? url_.size() : fragment;
                size_t qmark = url_.find('?', authorityEnd);
                if (qmark != std::string::npos && qmark > end)
                {
                    qmark = std::string::npos;
                }
                size_t pathEnd = (qmark == std::string::npos) ? end : qmark;
                path_ = url_.substr(authorityEnd, pathEnd - authorityEnd);
                if (path_.empty())
                {
                    path_ = "/";
                }
                if (qmark != std::string::npos)
                {
                    query_ = url_.substr(qmark + 1, end - qmark - 1);
                }
                return true;
            }
        };

    }  // namespace common
}  // namespace http
SOCKETSHPP_NS_END
