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
            bool authenticated = false;  ///< true if the credentials were accepted.
            std::string userId;          ///< Authenticated user identifier (success only).
            std::unordered_map<std::string, std::string> claims;  ///< Optional attributes of the user (success only).
            std::string error;           ///< Human-readable failure reason (failure only).

            /// @brief Successful result for @p user with no claims.
            static AuthResult success(const std::string& user)
            {
                AuthResult result;
                result.authenticated = true;
                result.userId = user;
                return result;
            }

            /// @brief Successful result for @p user carrying @p userClaims.
            static AuthResult success(const std::string& user,
                                     const std::unordered_map<std::string, std::string>& userClaims)
            {
                AuthResult result;
                result.authenticated = true;
                result.userId = user;
                result.claims = userClaims;
                return result;
            }

            /// @brief Failed result with reason @p err.
            static AuthResult failure(const std::string& err)
            {
                AuthResult result;
                result.authenticated = false;
                result.error = err;
                return result;
            }

            /// @brief Same as #authenticated (implicit conversion).
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
         * @tparam RequestType Request type providing has_header(name) and
         *         get_header_value(name), e.g. HttpRequest.
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
             * @return schemeName() unless overridden.
             */
            virtual std::string getChallenge() const
            {
                return schemeName();
            }
        };

        /**
         * @brief Bearer token authentication (RFC 6750).
         * 
         * Validates tokens from the "Authorization: Bearer TOKEN" header. The scheme
         * name is matched case-insensitively.
         */
        template<typename RequestType>
        class BearerTokenAuth : public AuthenticationStrategy<RequestType>
        {
        public:
            /// @brief Checks a bearer token and returns the outcome.
            using ValidatorFunc = std::function<AuthResult(const std::string& token)>;

        private:
            ValidatorFunc m_validator;
            std::string m_realm;

        public:
            /// @param validator Called with the (non-empty) token; must be thread-safe
            ///        if requests are handled concurrently.
            /// @param realm Realm reported in the WWW-Authenticate challenge.
            explicit BearerTokenAuth(ValidatorFunc validator, std::string realm = "API")
                : m_validator(std::move(validator)), m_realm(std::move(realm))
            {
            }

            /// @brief Extract the bearer token and pass it to the validator.
            /// @return The validator's result, or a failure if the header is missing,
            ///         uses another scheme, or carries an empty token.
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

            /// @brief Returns "Bearer".
            std::string schemeName() const override { return "Bearer"; }

            /// @brief Returns `Bearer realm="<realm>"`.
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
            /// @brief Checks an API key and returns the outcome.
            using ValidatorFunc = std::function<AuthResult(const std::string& apiKey)>;

        private:
            std::string m_headerName;
            ValidatorFunc m_validator;

        public:
            /// @param headerName Header carrying the key, e.g. "X-API-Key" (looked up
            ///        case-insensitively when RequestType is HttpRequest).
            /// @param validator Called with the (non-empty) header value; must be
            ///        thread-safe if requests are handled concurrently.
            ApiKeyAuth(std::string headerName, ValidatorFunc validator)
                : m_headerName(std::move(headerName)), m_validator(std::move(validator))
            {
            }

            /// @brief Read the API key header and pass its value to the validator.
            /// @return The validator's result, or a failure if the header is missing
            ///         or empty.
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

            /// @brief Returns "API-Key" (not a registered HTTP auth scheme).
            std::string schemeName() const override { return "API-Key"; }

            /// @brief Returns `API-Key header="<headerName>"`.
            std::string getChallenge() const override
            {
                return "API-Key header=\"" + m_headerName + "\"";
            }
        };

        /**
         * @brief HTTP Basic Authentication (RFC 7617).
         * 
         * Validates username/password from the "Authorization: Basic BASE64" header
         * (scheme matched case-insensitively). The decoded credentials are split at
         * the first ':'.
         */
        template<typename RequestType>
        class BasicAuth : public AuthenticationStrategy<RequestType>
        {
        public:
            /// @brief Checks a username/password pair and returns the outcome.
            using ValidatorFunc = std::function<AuthResult(
                const std::string& username, const std::string& password)>;

        private:
            ValidatorFunc m_validator;
            std::string m_realm;

        public:
            /// @param validator Called with the decoded username and password; must be
            ///        thread-safe if requests are handled concurrently. Compare secrets
            ///        in constant time.
            /// @param realm Realm reported in the WWW-Authenticate challenge.
            explicit BasicAuth(ValidatorFunc validator, std::string realm = "Restricted")
                : m_validator(std::move(validator)), m_realm(std::move(realm))
            {
            }

            /// @brief Decode the Basic credentials and pass them to the validator.
            /// @return The validator's result, or a failure if the header is missing,
            ///         uses another scheme, is not valid base64, or has no ':'.
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

            /// @brief Returns "Basic".
            std::string schemeName() const override { return "Basic"; }

            /// @brief Returns `Basic realm="<realm>"`.
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
         * Call authenticate() at the start of a request handler.
         *
         * @note Configure (addStrategy() etc.) before serving; authenticate() itself
         *       only reads the configuration, so concurrent calls are safe as long as
         *       the strategies and callback are thread-safe.
         * @tparam RequestType Request type (see AuthenticationStrategy).
         * @tparam ResponseType Response type providing set_header() and set_content(),
         *         e.g. HttpResponse.
         */
        template<typename RequestType, typename ResponseType>
        class AuthenticationMiddleware
        {
        public:
            /// @brief Shared pointer to a strategy.
            using StrategyPtr = std::shared_ptr<AuthenticationStrategy<RequestType>>;
            /// @brief Called with the request and the successful result.
            using AuthCallback = std::function<void(RequestType&, const AuthResult&)>;

        private:
            std::vector<StrategyPtr> m_strategies;
            AuthCallback m_onAuthenticated;
            bool m_requireAuth;

        public:
            /// @brief Create a middleware with no strategies that requires authentication.
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
             * @brief Set whether authentication is required (default true).
             * If false, allows unauthenticated requests but still attempts auth.
             * If true and no strategy is configured, every request is rejected.
             */
            void setRequireAuth(bool require)
            {
                m_requireAuth = require;
            }

            /**
             * @brief Authenticate a request.
             *
             * Strategies are tried in order; the first success invokes the
             * authenticated callback and returns true. If all fail and auth is
             * required, a combined WWW-Authenticate header (all challenges) and a JSON
             * error body are set on @p res.
             * @param req HTTP request
             * @param res HTTP response (modified if auth fails)
             * @return true if authenticated (or auth not required), false otherwise
             * @warning The status code is not set. With HttpServer, a handler that sets
             *          this body and returns 0 answers 200, so return 401 on false.
             *          With no strategies and auth required, false is returned without
             *          touching @p res.
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
             * @brief Get the configured strategies, in the order they are tried.
             */
            const std::vector<StrategyPtr>& getStrategies() const
            {
                return m_strategies;
            }
        };

    } // namespace server
} // namespace http
SOCKETSHPP_NS_END
