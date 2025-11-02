// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <gtest/gtest.h>
#include <SocketsHpp/mcp/common/mcp_config.h>
#include <nlohmann/json.hpp>

using namespace SocketsHpp::mcp;
using json = nlohmann::json;

// Test MCP configuration parsing
TEST(MCPConfigTest, StdioConfigFromJson) {
    json config_json = {
        {"command", "npx"},
        {"args", json::array({"-y", "@modelcontextprotocol/server-example"})},
        {"env", {{"NODE_ENV", "production"}}},
        {"envFile", ".env"},
        {"cwd", "/path/to/working/dir"}
    };

    auto config = StdioConfig::fromJson(config_json);
    
    EXPECT_EQ(config.command, "npx");
    ASSERT_EQ(config.args.size(), 2);
    EXPECT_EQ(config.args[0], "-y");
    EXPECT_EQ(config.args[1], "@modelcontextprotocol/server-example");
    EXPECT_EQ(config.env["NODE_ENV"], "production");
    EXPECT_TRUE(config.envFile.has_value());
    EXPECT_EQ(config.envFile.value(), ".env");
    EXPECT_TRUE(config.cwd.has_value());
    EXPECT_EQ(config.cwd.value(), "/path/to/working/dir");
}

TEST(MCPConfigTest, HttpConfigFromJson) {
    json config_json = {
        {"url", "https://api.example.com/mcp"},
        {"headers", {
            {"Authorization", "Bearer token123"},
            {"x-api-key", "key456"}
        }},
        {"timeout", 60}
    };

    auto config = HttpConfig::fromJson(config_json);
    
    EXPECT_EQ(config.url, "https://api.example.com/mcp");
    EXPECT_EQ(config.headers["Authorization"], "Bearer token123");
    EXPECT_EQ(config.headers["x-api-key"], "key456");
    EXPECT_EQ(config.timeoutSeconds, 60);
}

TEST(MCPConfigTest, ClientConfigFromJsonStdio) {
    json server_json = {
        {"type", "stdio"},
        {"command", "python"},
        {"args", json::array({"server.py"})},
        {"env", {{"DEBUG", "true"}}}
    };

    auto config = ClientConfig::fromJson(server_json);
    
    EXPECT_EQ(config.transport, TransportType::STDIO);
    EXPECT_EQ(config.stdio.command, "python");
    ASSERT_EQ(config.stdio.args.size(), 1);
    EXPECT_EQ(config.stdio.args[0], "server.py");
    EXPECT_EQ(config.stdio.env["DEBUG"], "true");
}

TEST(MCPConfigTest, ClientConfigFromJsonHttp) {
    json server_json = {
        {"type", "http"},
        {"url", "http://localhost:3000/mcp"},
        {"headers", {{"Authorization", "Bearer xyz"}}}
    };

    auto config = ClientConfig::fromJson(server_json);
    
    EXPECT_EQ(config.transport, TransportType::HTTP);
    EXPECT_EQ(config.http.url, "http://localhost:3000/mcp");
    EXPECT_EQ(config.http.headers["Authorization"], "Bearer xyz");
}

TEST(MCPConfigTest, ServerConfigDefaults) {
    ServerConfig config;
    
    EXPECT_EQ(config.transport, TransportType::STDIO);
    EXPECT_EQ(config.port, 8080);
    EXPECT_EQ(config.endpoint, "/mcp");
    EXPECT_EQ(config.host, "127.0.0.1");
    EXPECT_EQ(config.responseMode, ServerConfig::ResponseMode::BATCH);
    EXPECT_EQ(config.maxMessageSize, 4 * 1024 * 1024);
    EXPECT_TRUE(config.session.enabled);
    EXPECT_FALSE(config.resumability.enabled);
    EXPECT_FALSE(config.auth.enabled);
}

TEST(MCPConfigTest, ServerConfigParseArgs) {
    ServerConfig config;
    
    const char* argv[] = {
        "program",
        "--transport", "http",
        "--port", "9000",
        "--endpoint", "/api/mcp",
        "--host", "0.0.0.0",
        "--response-mode", "stream",
        "--enable-resumability"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    config.parseArgs(argc, const_cast<char**>(argv));
    
    EXPECT_EQ(config.transport, TransportType::HTTP);
    EXPECT_EQ(config.port, 9000);
    EXPECT_EQ(config.endpoint, "/api/mcp");
    EXPECT_EQ(config.host, "0.0.0.0");
    EXPECT_EQ(config.responseMode, ServerConfig::ResponseMode::STREAM);
    EXPECT_TRUE(config.resumability.enabled);
}

TEST(MCPConfigTest, TransportTypes) {
    EXPECT_NE(TransportType::STDIO, TransportType::HTTP);
}

TEST(MCPConfigTest, ServerAuthConfig) {
    ServerConfig config;
    
    config.auth.enabled = true;
    config.auth.type = ServerConfig::AuthConfig::Type::BEARER;
    config.auth.secretOrPublicKey = "secret123";
    
    EXPECT_TRUE(config.auth.enabled);
    EXPECT_EQ(config.auth.type, ServerConfig::AuthConfig::Type::BEARER);
    EXPECT_TRUE(config.auth.secretOrPublicKey.has_value());
    EXPECT_EQ(config.auth.secretOrPublicKey.value(), "secret123");
}

TEST(MCPConfigTest, ServerCorsConfig) {
    ServerConfig config;
    
    EXPECT_EQ(config.cors.allowOrigin, "*");
    EXPECT_FALSE(config.cors.allowMethods.empty());
    EXPECT_FALSE(config.cors.allowHeaders.empty());
}

TEST(MCPConfigTest, ServerSessionConfig) {
    ServerConfig config;
    
    EXPECT_TRUE(config.session.enabled);
    EXPECT_EQ(config.session.headerName, "Mcp-Session-Id");
    EXPECT_TRUE(config.session.allowClientTermination);
    EXPECT_EQ(config.session.sessionTimeoutSeconds, 3600);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
