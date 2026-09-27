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

TEST(ProxyAwareTest, IsIPAddress)
{
    for (const char* ok : {"192.0.2.1", "0.0.0.0", "255.255.255.255", "::1", "::", "2001:db8::1",
                           "fe80::a:b:c:d", "1:2:3:4:5:6:7:8", "::ffff:192.0.2.1", "1::"})
        EXPECT_TRUE(TrustProxyConfig::isIPAddress(ok)) << ok;
    for (const char* bad : {"", "unknown", "_hidden", "256.1.1.1", "1.2.3", "01.2.3.4", "1.2.3.4.5",
                            "1:2:3:4:5:6:7:8:9", "1::2::3", "12345::1", "::g", "localhost",
                            "<script>alert(1)</script>", "1.2.3.4 ", "[::1]", "fe80::1%eth0"})
        EXPECT_FALSE(TrustProxyConfig::isIPAddress(bad)) << bad;
}

TEST(ProxyAwareTest, GetClientIP_IgnoresForwardedValuesThatAreNotAddresses)
{
    TrustProxyConfig config;
    config.addTrustedProxy("10.0.0.1");

    MockRequest forged;
    forged.client = "10.0.0.1:5000";
    forged.headers["X-Forwarded-For"] = "<script>alert(1)</script>";
    EXPECT_EQ(ProxyAwareHelpers::getClientIP(forged.headers, forged.client, config), "10.0.0.1");

    // A garbage entry left of the proxy-appended address does not matter.
    MockRequest mixed;
    mixed.client = "10.0.0.1:5000";
    mixed.headers["X-Forwarded-For"] = "junk, 203.0.113.9";
    EXPECT_EQ(ProxyAwareHelpers::getClientIP(mixed.headers, mixed.client, config), "203.0.113.9");

    // An invalid X-Forwarded-For falls through to a valid X-Real-IP.
    MockRequest realIp;
    realIp.client = "10.0.0.1:5000";
    realIp.headers["X-Forwarded-For"] = "unknown";
    realIp.headers["X-Real-IP"] = "198.51.100.7";
    EXPECT_EQ(ProxyAwareHelpers::getClientIP(realIp.headers, realIp.client, config), "198.51.100.7");

    MockRequest forwarded;
    forwarded.client = "10.0.0.1:5000";
    forwarded.headers["Forwarded"] = "for=unknown";
    EXPECT_EQ(ProxyAwareHelpers::getClientIP(forwarded.headers, forwarded.client, config), "10.0.0.1");
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
