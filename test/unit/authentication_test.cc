// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <SocketsHpp/http/server/authentication.h>
#include <map>
#include <string>

using namespace SOCKETSHPP_NS::http::server;

// Mock request
struct MockRequest
{
    std::map<std::string, std::string> headers;

    bool has_header(const std::string& name) const
    {
        return headers.find(name) != headers.end();
    }

    std::string get_header_value(const std::string& name) const
    {
        auto it = headers.find(name);
        return (it != headers.end()) ? it->second : "";
    }
};

// Mock response
struct MockResponse
{
    int code = 200;
    std::map<std::string, std::string> headers;
    std::string body;

    void set_header(const std::string& name, const std::string& value)
    {
        headers[name] = value;
    }

    void set_content(const std::string& content, const std::string& /*contentType*/)
    {
        body = content;
    }
};

TEST(AuthResultTest, Success)
{
    auto result = AuthResult::success("user123");
    
    EXPECT_TRUE(result.authenticated);
    EXPECT_EQ(result.userId, "user123");
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result); // Test bool conversion
}

TEST(AuthResultTest, SuccessWithClaims)
{
    std::unordered_map<std::string, std::string> claims;
    claims["role"] = "admin";
    claims["email"] = "user@example.com";
    
    auto result = AuthResult::success("user123", claims);
    
    EXPECT_TRUE(result.authenticated);
    EXPECT_EQ(result.userId, "user123");
    EXPECT_EQ(result.claims["role"], "admin");
    EXPECT_EQ(result.claims["email"], "user@example.com");
}

TEST(AuthResultTest, Failure)
{
    auto result = AuthResult::failure("Invalid token");
    
    EXPECT_FALSE(result.authenticated);
    EXPECT_TRUE(result.userId.empty());
    EXPECT_EQ(result.error, "Invalid token");
    EXPECT_FALSE(result); // Test bool conversion
}

TEST(BearerTokenAuthTest, ValidToken)
{
    BearerTokenAuth<MockRequest> auth(
        [](const std::string& token) -> AuthResult {
            if (token == "valid-token-123")
            {
                return AuthResult::success("user123");
            }
            return AuthResult::failure("Invalid token");
        }
    );

    MockRequest req;
    req.headers["Authorization"] = "Bearer valid-token-123";

    auto result = auth.authenticate(req);
    
    EXPECT_TRUE(result.authenticated);
    EXPECT_EQ(result.userId, "user123");
}

TEST(BearerTokenAuthTest, InvalidToken)
{
    BearerTokenAuth<MockRequest> auth(
        [](const std::string& token) -> AuthResult {
            if (token == "valid-token-123")
            {
                return AuthResult::success("user123");
            }
            return AuthResult::failure("Invalid token");
        }
    );

    MockRequest req;
    req.headers["Authorization"] = "Bearer invalid-token";

    auto result = auth.authenticate(req);
    
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.error, "Invalid token");
}

TEST(BearerTokenAuthTest, MissingHeader)
{
    BearerTokenAuth<MockRequest> auth(
        [](const std::string&) -> AuthResult {
            return AuthResult::success("user123");
        }
    );

    MockRequest req;

    auto result = auth.authenticate(req);
    
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.error, "Missing Authorization header");
}

TEST(BearerTokenAuthTest, WrongScheme)
{
    BearerTokenAuth<MockRequest> auth(
        [](const std::string&) -> AuthResult {
            return AuthResult::success("user123");
        }
    );

    MockRequest req;
    req.headers["Authorization"] = "Basic dXNlcjpwYXNz";

    auto result = auth.authenticate(req);
    
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.error, "Invalid authorization scheme");
}

TEST(BearerTokenAuthTest, EmptyToken)
{
    BearerTokenAuth<MockRequest> auth(
        [](const std::string&) -> AuthResult {
            return AuthResult::success("user123");
        }
    );

    MockRequest req;
    req.headers["Authorization"] = "Bearer ";

    auto result = auth.authenticate(req);
    
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.error, "Empty bearer token");
}

TEST(BearerTokenAuthTest, TokenWithWhitespace)
{
    BearerTokenAuth<MockRequest> auth(
        [](const std::string& token) -> AuthResult {
            if (token == "token-with-spaces")
            {
                return AuthResult::success("user123");
            }
            return AuthResult::failure("Invalid token");
        }
    );

    MockRequest req;
    req.headers["Authorization"] = "Bearer   token-with-spaces  ";

    auto result = auth.authenticate(req);
    
    EXPECT_TRUE(result.authenticated);
}

TEST(BearerTokenAuthTest, GetChallenge)
{
    BearerTokenAuth<MockRequest> auth(
        [](const std::string&) -> AuthResult { return AuthResult::success(""); },
        "MyAPI"
    );

    EXPECT_EQ(auth.schemeName(), "Bearer");
    EXPECT_EQ(auth.getChallenge(), "Bearer realm=\"MyAPI\"");
}

TEST(ApiKeyAuthTest, ValidKey)
{
    ApiKeyAuth<MockRequest> auth(
        "X-API-Key",
        [](const std::string& key) -> AuthResult {
            if (key == "secret-key-123")
            {
                return AuthResult::success("api-client");
            }
            return AuthResult::failure("Invalid API key");
        }
    );

    MockRequest req;
    req.headers["X-API-Key"] = "secret-key-123";

    auto result = auth.authenticate(req);
    
    EXPECT_TRUE(result.authenticated);
    EXPECT_EQ(result.userId, "api-client");
}

TEST(ApiKeyAuthTest, InvalidKey)
{
    ApiKeyAuth<MockRequest> auth(
        "X-API-Key",
        [](const std::string& key) -> AuthResult {
            if (key == "secret-key-123")
            {
                return AuthResult::success("api-client");
            }
            return AuthResult::failure("Invalid API key");
        }
    );

    MockRequest req;
    req.headers["X-API-Key"] = "wrong-key";

    auto result = auth.authenticate(req);
    
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.error, "Invalid API key");
}

TEST(ApiKeyAuthTest, MissingHeader)
{
    ApiKeyAuth<MockRequest> auth(
        "X-API-Key",
        [](const std::string&) -> AuthResult {
            return AuthResult::success("api-client");
        }
    );

    MockRequest req;

    auto result = auth.authenticate(req);
    
    EXPECT_FALSE(result.authenticated);
    EXPECT_TRUE(result.error.find("Missing API key header") != std::string::npos);
}

TEST(BasicAuthTest, ValidCredentials)
{
    BasicAuth<MockRequest> auth(
        [](const std::string& user, const std::string& pass) -> AuthResult {
            if (user == "admin" && pass == "password123")
            {
                return AuthResult::success("admin");
            }
            return AuthResult::failure("Invalid credentials");
        }
    );

    MockRequest req;
    // "admin:password123" in base64
    req.headers["Authorization"] = "Basic YWRtaW46cGFzc3dvcmQxMjM=";

    auto result = auth.authenticate(req);
    
    EXPECT_TRUE(result.authenticated);
    EXPECT_EQ(result.userId, "admin");
}

TEST(BasicAuthTest, InvalidCredentials)
{
    BasicAuth<MockRequest> auth(
        [](const std::string& user, const std::string& pass) -> AuthResult {
            if (user == "admin" && pass == "password123")
            {
                return AuthResult::success("admin");
            }
            return AuthResult::failure("Invalid credentials");
        }
    );

    MockRequest req;
    // "admin:wrongpass" in base64
    req.headers["Authorization"] = "Basic YWRtaW46d3JvbmdwYXNz";

    auto result = auth.authenticate(req);
    
    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.error, "Invalid credentials");
}

TEST(BasicAuthTest, GetChallenge)
{
    BasicAuth<MockRequest> auth(
        [](const std::string&, const std::string&) -> AuthResult { 
            return AuthResult::success(""); 
        },
        "Admin Area"
    );

    EXPECT_EQ(auth.schemeName(), "Basic");
    EXPECT_EQ(auth.getChallenge(), "Basic realm=\"Admin Area\"");
}

TEST(AuthenticationMiddlewareTest, SingleStrategy_Success)
{
    AuthenticationMiddleware<MockRequest, MockResponse> middleware;

    auto strategy = std::make_shared<BearerTokenAuth<MockRequest>>(
        [](const std::string& token) -> AuthResult {
            if (token == "valid-token")
            {
                return AuthResult::success("user123");
            }
            return AuthResult::failure("Invalid token");
        }
    );
    middleware.addStrategy(strategy);

    MockRequest req;
    req.headers["Authorization"] = "Bearer valid-token";
    MockResponse res;

    bool authenticated = middleware.authenticate(req, res);
    
    EXPECT_TRUE(authenticated);
    EXPECT_EQ(res.code, 200); // Not modified
}

TEST(AuthenticationMiddlewareTest, SingleStrategy_Failure)
{
    AuthenticationMiddleware<MockRequest, MockResponse> middleware;

    auto strategy = std::make_shared<BearerTokenAuth<MockRequest>>(
        [](const std::string&) -> AuthResult {
            return AuthResult::failure("Invalid token");
        }
    );
    middleware.addStrategy(strategy);

    MockRequest req;
    req.headers["Authorization"] = "Bearer invalid-token";
    MockResponse res;

    bool authenticated = middleware.authenticate(req, res);
    
    EXPECT_FALSE(authenticated);
    // Note: The middleware doesn't set HTTP status codes - that's application responsibility
    // EXPECT_EQ(res.code, 401);
    EXPECT_TRUE(res.headers.count("WWW-Authenticate") > 0); // Challenge header should be set
}

TEST(AuthenticationMiddlewareTest, MultipleStrategies_FirstSucceeds)
{
    AuthenticationMiddleware<MockRequest, MockResponse> middleware;

    // Bearer auth (will succeed)
    auto bearerAuth = std::make_shared<BearerTokenAuth<MockRequest>>(
        [](const std::string& token) -> AuthResult {
            if (token == "valid-bearer")
            {
                return AuthResult::success("bearer-user");
            }
            return AuthResult::failure("Invalid bearer token");
        }
    );
    middleware.addStrategy(bearerAuth);

    // API key auth (won't be tried)
    auto apiKeyAuth = std::make_shared<ApiKeyAuth<MockRequest>>(
        "X-API-Key",
        [](const std::string&) -> AuthResult {
            return AuthResult::success("api-user");
        }
    );
    middleware.addStrategy(apiKeyAuth);

    MockRequest req;
    req.headers["Authorization"] = "Bearer valid-bearer";
    MockResponse res;

    bool authenticated = middleware.authenticate(req, res);
    
    EXPECT_TRUE(authenticated);
}

TEST(AuthenticationMiddlewareTest, MultipleStrategies_SecondSucceeds)
{
    AuthenticationMiddleware<MockRequest, MockResponse> middleware;

    // Bearer auth (will fail)
    auto bearerAuth = std::make_shared<BearerTokenAuth<MockRequest>>(
        [](const std::string&) -> AuthResult {
            return AuthResult::failure("Invalid bearer token");
        }
    );
    middleware.addStrategy(bearerAuth);

    // API key auth (will succeed)
    auto apiKeyAuth = std::make_shared<ApiKeyAuth<MockRequest>>(
        "X-API-Key",
        [](const std::string& key) -> AuthResult {
            if (key == "valid-api-key")
            {
                return AuthResult::success("api-user");
            }
            return AuthResult::failure("Invalid API key");
        }
    );
    middleware.addStrategy(apiKeyAuth);

    MockRequest req;
    req.headers["Authorization"] = "Bearer invalid";
    req.headers["X-API-Key"] = "valid-api-key";
    MockResponse res;

    bool authenticated = middleware.authenticate(req, res);
    
    EXPECT_TRUE(authenticated);
}

TEST(AuthenticationMiddlewareTest, AuthCallback)
{
    AuthenticationMiddleware<MockRequest, MockResponse> middleware;

    auto strategy = std::make_shared<BearerTokenAuth<MockRequest>>(
        [](const std::string&) -> AuthResult {
            std::unordered_map<std::string, std::string> claims;
            claims["role"] = "admin";
            return AuthResult::success("user123", claims);
        }
    );
    middleware.addStrategy(strategy);

    std::string authenticatedUser;
    std::string userRole;

    middleware.setAuthenticatedCallback(
        [&](MockRequest&, const AuthResult& result) {
            authenticatedUser = result.userId;
            userRole = result.claims.at("role");
        }
    );

    MockRequest req;
    req.headers["Authorization"] = "Bearer token";
    MockResponse res;

    middleware.authenticate(req, res);
    
    EXPECT_EQ(authenticatedUser, "user123");
    EXPECT_EQ(userRole, "admin");
}

TEST(AuthenticationMiddlewareTest, OptionalAuth)
{
    AuthenticationMiddleware<MockRequest, MockResponse> middleware;
    middleware.setRequireAuth(false); // Make authentication optional

    auto strategy = std::make_shared<BearerTokenAuth<MockRequest>>(
        [](const std::string&) -> AuthResult {
            return AuthResult::failure("Invalid token");
        }
    );
    middleware.addStrategy(strategy);

    MockRequest req;
    req.headers["Authorization"] = "Bearer invalid";
    MockResponse res;

    bool authenticated = middleware.authenticate(req, res);
    
    EXPECT_TRUE(authenticated); // Passes even though auth failed
    EXPECT_NE(res.code, 401); // No 401 response
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
