// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/utils/base64.h>
#include <cctype>
#include <functional>
#include <stdexcept>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace server
    {
        /**
         * @brief Result of an authentication attempt.
         */
        struct AuthResult
        {
            bool authenticated = false;
            std::string userId;
            std::unordered_map<std::string, std::string> claims;
            std::string error;

            static AuthResult success(const std::string& user)
            {
                AuthResult result;
                result.authenticated = true;
                result.userId = user;
                return result;
            }

            static AuthResult success(const std::string& user,
                                     const std::unordered_map<std::string, std::string>& userClaims)
            {
                AuthResult result;
                result.authenticated = true;
                result.userId = user;
                result.claims = userClaims;
                return result;
            }

            static AuthResult failure(const std::string& err)
            {
                AuthResult result;
                result.authenticated = false;
                result.error = err;
                return result;
            }

            operator bool() const { return authenticated; }
        };

        namespace detail
        {
            /**
             * @brief Match an Authorization header against an auth-scheme.
             *
             * The scheme is case-insensitive (RFC 9110, 11.1) and must be followed
             * by at least one space. On success @p credentials receives the rest of
             * the header with surrounding whitespace removed.
             */
            inline bool matchAuthScheme(const std::string& header, const std::string& scheme, std::string& credentials)
            {
                size_t start = header.find_first_not_of(" \t");
                if (start == std::string::npos || header.size() < start + scheme.size() + 1)
                {
                    return false;
                }
                for (size_t i = 0; i < scheme.size(); ++i)
                {
                    if (std::tolower(static_cast<unsigned char>(header[start + i])) !=
                        std::tolower(static_cast<unsigned char>(scheme[i])))
                    {
                        return false;
                    }
                }
                size_t sep = start + scheme.size();
                if (header[sep] != ' ' && header[sep] != '\t')
                {
                    return false;
                }
                size_t first = header.find_first_not_of(" \t", sep);
                if (first == std::string::npos)
                {
                    credentials.clear();
                    return true;
                }
                size_t last = header.find_last_not_of(" \t");
                credentials = header.substr(first, last - first + 1);
                return true;
            }
        }

        /**
         * @brief Base class for authentication strategies.
         * 
         * Implement this interface to create custom authentication mechanisms.
         */
        template<typename RequestType>
        class AuthenticationStrategy
        {
        public:
            virtual ~AuthenticationStrategy() = default;

            /**
             * @brief Authenticate a request.
             * @param req The HTTP request to authenticate
             * @return Authentication result
             */
            virtual AuthResult authenticate(const RequestType& req) = 0;

            /**
             * @brief Get the authentication scheme name (e.g., "Bearer", "Basic").
             */
            virtual std::string schemeName() const = 0;

            /**
             * @brief Get the WWW-Authenticate challenge for 401 responses.
             */
            virtual std::string getChallenge() const
            {
                return schemeName();
            }
        };

        /**
         * @brief Bearer token authentication (RFC 6750).
         * 
         * Validates tokens from "Authorization: Bearer <token>" header.
         */
        template<typename RequestType>
        class BearerTokenAuth : public AuthenticationStrategy<RequestType>
        {
        public:
            using ValidatorFunc = std::function<AuthResult(const std::string& token)>;

        private:
            ValidatorFunc m_validator;
            std::string m_realm;

        public:
            explicit BearerTokenAuth(ValidatorFunc validator, std::string realm = "API")
                : m_validator(std::move(validator)), m_realm(std::move(realm))
            {
            }

            AuthResult authenticate(const RequestType& req) override
            {
                // Extract Authorization header using duck-typed interface
                if (!req.has_header("Authorization"))
                {
                    return AuthResult::failure("Missing Authorization header");
                }
                
                std::string authHeader = req.get_header_value("Authorization");

                // Check for Bearer scheme (case-insensitive) and extract the token
                std::string token;
                if (!detail::matchAuthScheme(authHeader, "Bearer", token))
                {
                    return AuthResult::failure("Invalid authorization scheme");
                }

                if (token.empty())
                {
                    return AuthResult::failure("Empty bearer token");
                }

                // Validate token
                return m_validator(token);
            }

            std::string schemeName() const override { return "Bearer"; }

            std::string getChallenge() const override
            {
                return "Bearer realm=\"" + m_realm + "\"";
            }
        };

        /**
         * @brief API Key authentication.
         * 
         * Validates API keys from custom headers (e.g., X-API-Key).
         */
        template<typename RequestType>
        class ApiKeyAuth : public AuthenticationStrategy<RequestType>
        {
        public:
            using ValidatorFunc = std::function<AuthResult(const std::string& apiKey)>;

        private:
            std::string m_headerName;
            ValidatorFunc m_validator;

        public:
            ApiKeyAuth(std::string headerName, ValidatorFunc validator)
                : m_headerName(std::move(headerName)), m_validator(std::move(validator))
            {
            }

            AuthResult authenticate(const RequestType& req) override
            {
                // Extract API key header using duck-typed interface
                if (!req.has_header(m_headerName))
                {
                    return AuthResult::failure("Missing API key header: " + m_headerName);
                }
                
                std::string apiKey = req.get_header_value(m_headerName);

                if (apiKey.empty())
                {
                    return AuthResult::failure("Empty API key");
                }

                // Validate key
                return m_validator(apiKey);
            }

            std::string schemeName() const override { return "API-Key"; }

            std::string getChallenge() const override
            {
                return "API-Key header=\"" + m_headerName + "\"";
            }
        };

        /**
         * @brief HTTP Basic Authentication (RFC 7617).
         * 
         * Validates username/password from "Authorization: Basic <base64>" header.
         */
        template<typename RequestType>
        class BasicAuth : public AuthenticationStrategy<RequestType>
        {
        public:
            using ValidatorFunc = std::function<AuthResult(
                const std::string& username, const std::string& password)>;

        private:
            ValidatorFunc m_validator;
            std::string m_realm;

        public:
            explicit BasicAuth(ValidatorFunc validator, std::string realm = "Restricted")
                : m_validator(std::move(validator)), m_realm(std::move(realm))
            {
            }

            AuthResult authenticate(const RequestType& req) override
            {
                // Extract Authorization header using duck-typed interface
                if (!req.has_header("Authorization"))
                {
                    return AuthResult::failure("Missing Authorization header");
                }
                
                std::string authHeader = req.get_header_value("Authorization");

                // Check for Basic scheme (case-insensitive) and extract base64 credentials
                std::string base64Creds;
                if (!detail::matchAuthScheme(authHeader, "Basic", base64Creds))
                {
                    return AuthResult::failure("Invalid authorization scheme");
                }

                // Decode base64 using unified utility
                std::string credentials;
                try
                {
                    credentials = SocketsHpp::utils::Base64::decode(base64Creds);
                }
                catch (const std::invalid_argument&)
                {
                    return AuthResult::failure("Invalid base64 encoding in credentials");
                }

                // Split into username:password
                auto colonPos = credentials.find(':');
                if (colonPos == std::string::npos)
                {
                    return AuthResult::failure("Invalid credentials format");
                }

                auto username = credentials.substr(0, colonPos);
                auto password = credentials.substr(colonPos + 1);

                // Validate credentials
                return m_validator(username, password);
            }

            std::string schemeName() const override { return "Basic"; }

            std::string getChallenge() const override
            {
                return "Basic realm=\"" + m_realm + "\"";
            }
        };

        /**
         * @brief Authentication middleware with multiple strategy support.
         * 
         * Tries multiple authentication strategies in order until one succeeds.
         * Useful for supporting multiple authentication methods (e.g., Bearer + API Key).
         */
        template<typename RequestType, typename ResponseType>
        class AuthenticationMiddleware
        {
        public:
            using StrategyPtr = std::shared_ptr<AuthenticationStrategy<RequestType>>;
            using AuthCallback = std::function<void(RequestType&, const AuthResult&)>;

        private:
            std::vector<StrategyPtr> m_strategies;
            AuthCallback m_onAuthenticated;
            bool m_requireAuth;

        public:
            AuthenticationMiddleware() : m_requireAuth(true) {}

            /**
             * @brief Add an authentication strategy.
             * Strategies are tried in the order they're added.
             */
            void addStrategy(StrategyPtr strategy)
            {
                m_strategies.push_back(std::move(strategy));
            }

            /**
             * @brief Set callback invoked when authentication succeeds.
             * Use this to store user info in the request context.
             */
            void setAuthenticatedCallback(AuthCallback callback)
            {
                m_onAuthenticated = std::move(callback);
            }

            /**
             * @brief Set whether authentication is required.
             * If false, allows unauthenticated requests but still attempts auth.
             */
            void setRequireAuth(bool require)
            {
                m_requireAuth = require;
            }

            /**
             * @brief Authenticate a request.
             * @param req HTTP request
             * @param res HTTP response (modified if auth fails)
             * @return true if authenticated (or auth not required), false otherwise
             */
            bool authenticate(RequestType& req, ResponseType& res)
            {
                if (m_strategies.empty())
                {
                    return !m_requireAuth; // Pass through if no strategies
                }

                AuthResult result;
                std::vector<std::string> challenges;

                // Try each strategy
                for (const auto& strategy : m_strategies)
                {
                    result = strategy->authenticate(req);
                    if (result.authenticated)
                    {
                        // Success! Invoke callback
                        if (m_onAuthenticated)
                        {
                            m_onAuthenticated(req, result);
                        }
                        return true;
                    }
                    
                    // Collect challenge for 401 response
                    challenges.push_back(strategy->getChallenge());
                }

                // All strategies failed
                if (m_requireAuth)
                {
                    // Response headers are a single-valued map, so emit all challenges
                    // in one WWW-Authenticate field as a comma-separated list, which
                    // RFC 9110 (11.6.1) defines as equivalent to repeated fields.
                    std::string combined;
                    for (const auto& challenge : challenges)
                    {
                        if (!combined.empty())
                        {
                            combined += ", ";
                        }
                        combined += challenge;
                    }
                    res.set_header("WWW-Authenticate", combined);
                    res.set_content(R"({"error": "Unauthorized"})", "application/json");
                    
                    return false;
                }

                return true; // Auth not required
            }

            /**
             * @brief Get the most recent authentication result.
             */
            const std::vector<StrategyPtr>& getStrategies() const
            {
                return m_strategies;
            }
        };

    } // namespace server
} // namespace http
SOCKETSHPP_NS_END
