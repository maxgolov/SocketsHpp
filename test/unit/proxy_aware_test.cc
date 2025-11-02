// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <SocketsHpp/http/server/proxy_aware.h>
#include <map>
#include <string>

using namespace SOCKETSHPP_NS::http::server;

// Mock request with headers
struct MockRequest
{
    std::map<std::string, std::string> headers;
    std::string client = "192.168.1.100";
};

TEST(ProxyAwareTest, TrustMode_None)
{
    TrustProxyConfig config(TrustProxyConfig::TrustMode::None);
    
    EXPECT_FALSE(config.isTrusted("127.0.0.1"));
    EXPECT_FALSE(config.isTrusted("192.168.1.1"));
    EXPECT_FALSE(config.isTrusted("10.0.0.1"));
}

TEST(ProxyAwareTest, TrustMode_TrustAll)
{
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    EXPECT_TRUE(config.isTrusted("127.0.0.1"));
    EXPECT_TRUE(config.isTrusted("192.168.1.1"));
    EXPECT_TRUE(config.isTrusted("10.0.0.1"));
}

TEST(ProxyAwareTest, TrustMode_TrustSpecific)
{
    std::vector<std::string> trustedProxies = {"192.168.1.1", "10.0.0.1"};
    TrustProxyConfig config(trustedProxies);
    
    EXPECT_TRUE(config.isTrusted("192.168.1.1"));
    EXPECT_TRUE(config.isTrusted("10.0.0.1"));
    EXPECT_FALSE(config.isTrusted("192.168.1.2"));
    EXPECT_FALSE(config.isTrusted("127.0.0.1"));
}

TEST(ProxyAwareTest, GetProtocol_NoTrust)
{
    MockRequest req;
    req.headers["X-Forwarded-Proto"] = "https";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::None);
    
    auto protocol = ProxyAwareHelpers::getProtocol(req.headers, req.client, config);
    EXPECT_EQ(protocol, "http"); // Don't trust, return default
}

TEST(ProxyAwareTest, GetProtocol_TrustAll_HTTPS)
{
    MockRequest req;
    req.headers["X-Forwarded-Proto"] = "https";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto protocol = ProxyAwareHelpers::getProtocol(req.headers, req.client, config);
    EXPECT_EQ(protocol, "https");
}

TEST(ProxyAwareTest, GetProtocol_TrustAll_HTTP)
{
    MockRequest req;
    req.headers["X-Forwarded-Proto"] = "http";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto protocol = ProxyAwareHelpers::getProtocol(req.headers, req.client, config);
    EXPECT_EQ(protocol, "http");
}

TEST(ProxyAwareTest, GetProtocol_XForwardedProtocol)
{
    MockRequest req;
    req.headers["X-Forwarded-Protocol"] = "https";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto protocol = ProxyAwareHelpers::getProtocol(req.headers, req.client, config);
    EXPECT_EQ(protocol, "https");
}

TEST(ProxyAwareTest, GetProtocol_XForwardedSsl)
{
    MockRequest req;
    req.headers["X-Forwarded-Ssl"] = "on";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto protocol = ProxyAwareHelpers::getProtocol(req.headers, req.client, config);
    EXPECT_EQ(protocol, "https");
}

TEST(ProxyAwareTest, GetProtocol_ForwardedHeader)
{
    MockRequest req;
    req.headers["Forwarded"] = "for=192.0.2.60;proto=https;host=example.com";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto protocol = ProxyAwareHelpers::getProtocol(req.headers, req.client, config);
    EXPECT_EQ(protocol, "https");
}

TEST(ProxyAwareTest, GetClientIP_NoTrust)
{
    MockRequest req;
    req.client = "192.168.1.100";
    req.headers["X-Forwarded-For"] = "203.0.113.195";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::None);
    
    auto clientIP = ProxyAwareHelpers::getClientIP(req.headers, req.client, config);
    EXPECT_EQ(clientIP, "192.168.1.100"); // Direct connection IP
}

TEST(ProxyAwareTest, GetClientIP_XForwardedFor_Single)
{
    MockRequest req;
    req.client = "192.168.1.1"; // Trusted proxy
    req.headers["X-Forwarded-For"] = "203.0.113.195";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto clientIP = ProxyAwareHelpers::getClientIP(req.headers, req.client, config);
    EXPECT_EQ(clientIP, "203.0.113.195");
}

TEST(ProxyAwareTest, GetClientIP_XForwardedFor_Multiple)
{
    MockRequest req;
    req.client = "192.168.1.1";
    req.headers["X-Forwarded-For"] = "203.0.113.195, 70.41.3.18, 150.172.238.178";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto clientIP = ProxyAwareHelpers::getClientIP(req.headers, req.client, config);
    EXPECT_EQ(clientIP, "203.0.113.195"); // First IP is original client
}

TEST(ProxyAwareTest, GetClientIP_XRealIP)
{
    MockRequest req;
    req.client = "192.168.1.1";
    req.headers["X-Real-IP"] = "203.0.113.195";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto clientIP = ProxyAwareHelpers::getClientIP(req.headers, req.client, config);
    EXPECT_EQ(clientIP, "203.0.113.195");
}

TEST(ProxyAwareTest, GetClientIP_ForwardedHeader)
{
    MockRequest req;
    req.client = "192.168.1.1";
    req.headers["Forwarded"] = "for=203.0.113.195;proto=https";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto clientIP = ProxyAwareHelpers::getClientIP(req.headers, req.client, config);
    EXPECT_EQ(clientIP, "203.0.113.195");
}

TEST(ProxyAwareTest, GetClientIP_ForwardedHeader_WithPort)
{
    MockRequest req;
    req.client = "192.168.1.1";
    req.headers["Forwarded"] = "for=\"203.0.113.195:12345\";proto=https";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto clientIP = ProxyAwareHelpers::getClientIP(req.headers, req.client, config);
    EXPECT_EQ(clientIP, "203.0.113.195"); // Port stripped
}

TEST(ProxyAwareTest, GetHost_NoTrust)
{
    MockRequest req;
    req.client = "192.168.1.100";
    req.headers["Host"] = "localhost:8080";
    req.headers["X-Forwarded-Host"] = "example.com";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::None);
    
    auto host = ProxyAwareHelpers::getHost(req.headers, req.client, config);
    EXPECT_EQ(host, "localhost:8080"); // Use Host header, not X-Forwarded-Host
}

TEST(ProxyAwareTest, GetHost_XForwardedHost)
{
    MockRequest req;
    req.client = "192.168.1.1";
    req.headers["Host"] = "localhost:8080";
    req.headers["X-Forwarded-Host"] = "example.com";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto host = ProxyAwareHelpers::getHost(req.headers, req.client, config);
    EXPECT_EQ(host, "example.com");
}

TEST(ProxyAwareTest, GetHost_ForwardedHeader)
{
    MockRequest req;
    req.client = "192.168.1.1";
    req.headers["Host"] = "localhost:8080";
    req.headers["Forwarded"] = "host=example.com;proto=https";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto host = ProxyAwareHelpers::getHost(req.headers, req.client, config);
    EXPECT_EQ(host, "example.com");
}

TEST(ProxyAwareTest, GetHost_Fallback)
{
    MockRequest req;
    req.client = "192.168.1.1";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    auto host = ProxyAwareHelpers::getHost(req.headers, req.client, config, "default.com");
    EXPECT_EQ(host, "default.com");
}

TEST(ProxyAwareTest, IsSecure_HTTPS)
{
    MockRequest req;
    req.headers["X-Forwarded-Proto"] = "https";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    EXPECT_TRUE(ProxyAwareHelpers::isSecure(req.headers, req.client, config));
}

TEST(ProxyAwareTest, IsSecure_HTTP)
{
    MockRequest req;
    req.headers["X-Forwarded-Proto"] = "http";
    
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    
    EXPECT_FALSE(ProxyAwareHelpers::isSecure(req.headers, req.client, config));
}

TEST(ProxyAwareTest, AddTrustedProxy)
{
    TrustProxyConfig config;
    
    EXPECT_FALSE(config.isTrusted("192.168.1.1"));
    
    config.addTrustedProxy("192.168.1.1");
    
    EXPECT_TRUE(config.isTrusted("192.168.1.1"));
    EXPECT_FALSE(config.isTrusted("192.168.1.2"));
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
