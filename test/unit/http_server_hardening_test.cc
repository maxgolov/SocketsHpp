// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// Unit tests for security/robustness fixes in the HTTP server helpers:
// session ids, SSE formatting, query parsing, proxy-aware client IP
// resolution, authentication scheme handling, decompression limits and
// static file path containment.

#include <gtest/gtest.h>

#include <SocketsHpp/http/server/authentication.h>
#include <SocketsHpp/http/server/compression.h>
#include <SocketsHpp/http/server/compression_simple.h>
#include <SocketsHpp/http/server/http_file_server.h>
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/server/proxy_aware.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace SocketsHpp::http::server;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Session IDs
// ---------------------------------------------------------------------------

TEST(SessionIdTest, FormatIs128BitHex)
{
    SessionManager manager;
    const std::string id = manager.createSession();
    const std::string prefix = "session-";
    ASSERT_EQ(id.compare(0, prefix.size(), prefix), 0) << id;
    const std::string hex = id.substr(prefix.size());
    EXPECT_EQ(hex.size(), 32u) << id;
    for (char c : hex)
    {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << id;
    }
    EXPECT_TRUE(manager.validateSession(id));
}

TEST(SessionIdTest, IdsAreUnique)
{
    SessionManager manager;
    std::set<std::string> ids;
    for (int i = 0; i < 2000; ++i)
    {
        ids.insert(manager.createSession());
    }
    EXPECT_EQ(ids.size(), 2000u);
    EXPECT_EQ(manager.getSessionCount(), 2000u);
}

TEST(SessionIdTest, ResumabilityToggleIsHonored)
{
    SessionManager manager;
    const std::string id = manager.createSession();
    manager.addEvent(id, "1", "a");
    EXPECT_TRUE(manager.getEventsSince(id, "").empty());  // disabled by default

    manager.enableResumability(true);
    manager.addEvent(id, "1", "a");
    manager.addEvent(id, "2", "b");
    auto events = manager.getEventsSince(id, "1");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], "b");
}

// ---------------------------------------------------------------------------
// SSE formatting
// ---------------------------------------------------------------------------

TEST(SSEFormatTest, SplitsDataOnAllLineTerminators)
{
    SSEEvent evt;
    evt.data = "a\r\nb\rc\nd";
    EXPECT_EQ(evt.format(), "data: a\ndata: b\ndata: c\ndata: d\n\n");
}

TEST(SSEFormatTest, TrailingNewlineProducesEmptyDataLine)
{
    SSEEvent evt;
    evt.data = "x\n";
    EXPECT_EQ(evt.format(), "data: x\ndata: \n\n");
}

TEST(SSEFormatTest, StripsLineBreaksFromIdAndEvent)
{
    SSEEvent evt;
    evt.id = "1\r\ndata: injected";
    evt.event = "msg\nretry: 1";
    evt.data = "ok";
    EXPECT_EQ(evt.format(), "id: 1data: injected\nevent: msgretry: 1\ndata: ok\n\n");
}

// ---------------------------------------------------------------------------
// Query parsing / URL decoding
// ---------------------------------------------------------------------------

TEST(QueryParseTest, BareFlagWithoutValue)
{
    HttpRequest req;
    req.uri = "/p?flag&x=1";
    auto params = req.parse_query();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params["flag"], "");
    EXPECT_EQ(params["x"], "1");
}

TEST(QueryParseTest, TrailingAmpersandTolerated)
{
    HttpRequest req;
    req.uri = "/p?x=1&";
    auto params = req.parse_query();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params["x"], "1");
}

TEST(QueryParseTest, Utf8BytesInC1RangeAccepted)
{
    // U+2014 EM DASH = E2 80 94 (0x80 used to be rejected as a "control character")
    EXPECT_EQ(urlDecode("%E2%80%94"), "\xE2\x80\x94");
    EXPECT_EQ(urlDecode("%C2%9F"), "\xC2\x9F");
    EXPECT_THROW(urlDecode("%7F"), std::invalid_argument);
    EXPECT_THROW(urlDecode("%00"), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Proxy-aware helpers
// ---------------------------------------------------------------------------

TEST(ProxyAwareHardeningTest, StripPort)
{
    EXPECT_EQ(TrustProxyConfig::stripPort("10.0.0.1:5555"), "10.0.0.1");
    EXPECT_EQ(TrustProxyConfig::stripPort("10.0.0.1"), "10.0.0.1");
    EXPECT_EQ(TrustProxyConfig::stripPort("[::1]:8080"), "::1");
    EXPECT_EQ(TrustProxyConfig::stripPort("[::1]"), "::1");
    EXPECT_EQ(TrustProxyConfig::stripPort("::1"), "::1");
    EXPECT_EQ(TrustProxyConfig::stripPort("2001:db8::1"), "2001:db8::1");
    EXPECT_EQ(TrustProxyConfig::stripPort("\"[2001:db8::1]:4711\""), "2001:db8::1");
    EXPECT_EQ(TrustProxyConfig::stripPort(" 1.2.3.4 "), "1.2.3.4");
}

TEST(ProxyAwareHardeningTest, TrustSpecificMatchesClientWithPort)
{
    // HttpRequest::client is "ip:port" - it must still match a trusted IP.
    TrustProxyConfig config(std::vector<std::string>{"10.0.0.1", "::1"});
    EXPECT_TRUE(config.isTrusted("10.0.0.1:43210"));
    EXPECT_TRUE(config.isTrusted("[::1]:43210"));
    EXPECT_FALSE(config.isTrusted("10.0.0.2:43210"));
    EXPECT_FALSE(config.isTrusted("10.0.0.10:1"));
}

TEST(ProxyAwareHardeningTest, XffWalksRightToLeft)
{
    TrustProxyConfig config(std::vector<std::string>{"10.0.0.1", "10.0.0.2"});
    std::map<std::string, std::string> headers;
    // Client forged "6.6.6.6"; the real client is 203.0.113.7 as recorded by our proxy.
    headers["X-Forwarded-For"] = "6.6.6.6, 203.0.113.7, 10.0.0.2";
    EXPECT_EQ(ProxyAwareHelpers::getClientIP(headers, "10.0.0.1:5000", config), "203.0.113.7");
}

TEST(ProxyAwareHardeningTest, XffIgnoredFromUntrustedPeer)
{
    TrustProxyConfig config(std::vector<std::string>{"10.0.0.1"});
    std::map<std::string, std::string> headers;
    headers["X-Forwarded-For"] = "1.1.1.1";
    EXPECT_EQ(ProxyAwareHelpers::getClientIP(headers, "198.51.100.9:5000", config), "198.51.100.9");
}

TEST(ProxyAwareHardeningTest, XffAllTrustedReturnsLeftmost)
{
    TrustProxyConfig config(std::vector<std::string>{"10.0.0.1", "10.0.0.2"});
    std::map<std::string, std::string> headers;
    headers["X-Forwarded-For"] = "10.0.0.2";
    EXPECT_EQ(ProxyAwareHelpers::getClientIP(headers, "10.0.0.1:1", config), "10.0.0.2");
}

TEST(ProxyAwareHardeningTest, ForwardedHeaderRightToLeft)
{
    TrustProxyConfig config(std::vector<std::string>{"10.0.0.1"});
    std::map<std::string, std::string> headers;
    headers["Forwarded"] = "for=6.6.6.6, For=\"[2001:db8::5]:4711\";proto=https";
    EXPECT_EQ(ProxyAwareHelpers::getClientIP(headers, "10.0.0.1:1", config), "2001:db8::5");
}

TEST(ProxyAwareHardeningTest, NormalizedHeaderNamesAreFound)
{
    // HttpRequest stores "X-Real-IP" as "X-Real-Ip".
    TrustProxyConfig config(TrustProxyConfig::TrustMode::TrustAll);
    HttpRequest req;
    req.client = "10.0.0.1:1234";
    req.headers[HttpRequest::normalize_header_name("X-Real-IP")] = "203.0.113.9";
    EXPECT_EQ(ProxyAwareHelpers::getClientIP(req.headers, req.client, config), "203.0.113.9");
}

// ---------------------------------------------------------------------------
// Authentication
// ---------------------------------------------------------------------------

TEST(AuthHardeningTest, BearerSchemeIsCaseInsensitive)
{
    BearerTokenAuth<HttpRequest> auth([](const std::string& token) {
        return token == "abc" ? AuthResult::success("u") : AuthResult::failure("bad");
    });
    HttpRequest req;
    req.headers["Authorization"] = "bearer   abc  ";
    EXPECT_TRUE(auth.authenticate(req).authenticated);
    req.headers["Authorization"] = "BEARER abc";
    EXPECT_TRUE(auth.authenticate(req).authenticated);
    req.headers["Authorization"] = "Bearerabc";
    EXPECT_FALSE(auth.authenticate(req).authenticated);
}

TEST(AuthHardeningTest, BasicSchemeIsCaseInsensitive)
{
    BasicAuth<HttpRequest> auth([](const std::string& user, const std::string& pass) {
        return (user == "user" && pass == "pass") ? AuthResult::success(user) : AuthResult::failure("bad");
    });
    HttpRequest req;
    req.headers["Authorization"] = "basic dXNlcjpwYXNz";  // user:pass
    EXPECT_TRUE(auth.authenticate(req).authenticated);
}

TEST(AuthHardeningTest, AllChallengesAreReported)
{
    AuthenticationMiddleware<HttpRequest, HttpResponse> middleware;
    middleware.addStrategy(std::make_shared<BearerTokenAuth<HttpRequest>>(
        [](const std::string&) { return AuthResult::failure("no"); }, "api"));
    middleware.addStrategy(std::make_shared<BasicAuth<HttpRequest>>(
        [](const std::string&, const std::string&) { return AuthResult::failure("no"); }, "site"));
    HttpRequest req;
    HttpResponse res;
    EXPECT_FALSE(middleware.authenticate(req, res));
    EXPECT_EQ(res.headers["Www-Authenticate"], "Bearer realm=\"api\", Basic realm=\"site\"");
}

// ---------------------------------------------------------------------------
// Compression
// ---------------------------------------------------------------------------

TEST(CompressionHardeningTest, DecompressionOutputIsBounded)
{
    CompressionRegistry::instance().clear();
    compression::registerSimpleCompression();

    // 4 input bytes expand to 510 output bytes.
    std::string body = std::string("\xFF" "A" "\xFF" "B", 4);
    CompressionMiddleware middleware;
    middleware.setMaxDecompressedSize(100);
    std::string copy = body;
    EXPECT_FALSE(middleware.decompressRequest("rle", copy));
    EXPECT_EQ(copy, body);  // unchanged on failure

    middleware.setMaxDecompressedSize(510);
    EXPECT_TRUE(middleware.decompressRequest("rle", copy));
    EXPECT_EQ(copy.size(), 510u);
    CompressionRegistry::instance().clear();
}

TEST(CompressionHardeningTest, UnboundedStrategyIsCheckedAfterDecompression)
{
    CompressionRegistry::instance().clear();
    CompressionRegistry::instance().registerStrategy(std::make_shared<CompressionStrategy>(
        "expand",
        [](const std::vector<uint8_t>& in, int) { return in; },
        [](const std::vector<uint8_t>&) { return std::vector<uint8_t>(1000, 'x'); }));
    CompressionMiddleware middleware;
    middleware.setMaxDecompressedSize(999);
    std::string body = "z";
    EXPECT_FALSE(middleware.decompressRequest("expand", body));
    EXPECT_EQ(body, "z");
    CompressionRegistry::instance().clear();
}

TEST(CompressionHardeningTest, EncodingNamesAreCaseInsensitive)
{
    CompressionRegistry::instance().clear();
    compression::registerSimpleCompression();
    EXPECT_TRUE(CompressionRegistry::instance().isSupported("RLE"));
    EXPECT_NE(CompressionRegistry::instance().get("Identity"), nullptr);
    auto prefs = parseAcceptEncoding("GZIP;Q=0.5, Rle");
    ASSERT_EQ(prefs.size(), 2u);
    EXPECT_EQ(prefs[0].encoding, "rle");
    EXPECT_EQ(prefs[1].encoding, "gzip");
    EXPECT_FLOAT_EQ(prefs[1].quality, 0.5f);
    CompressionRegistry::instance().clear();
}

// ---------------------------------------------------------------------------
// Static file server containment
// ---------------------------------------------------------------------------

namespace
{
    class TestFileServer : public HttpFileServer
    {
    public:
        explicit TestFileServer(const std::string& root) : HttpFileServer("127.0.0.1", 0, root) {}

        using HttpFileServer::decodeUriPath;
        using HttpFileServer::isPathWithinRoot;

        bool resolve(const std::string& path)
        {
            fs::path out;
            return validateFilePath(path, out);
        }
    };

    void writeFile(const fs::path& p, const std::string& content)
    {
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << content;
    }
}

class FileServerContainmentTest : public ::testing::Test
{
protected:
    fs::path base;
    fs::path root;

    void SetUp() override
    {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        base = fs::temp_directory_path() / ("socketshpp_fs_test_" + std::to_string(stamp));
        root = base / "www";
        writeFile(root / "index.html", "<html></html>");
        writeFile(root / "sub" / "a.txt", "a");
        writeFile(base / "www-private" / "secret.txt", "secret");
        writeFile(base / "outside.txt", "outside");
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(base, ec);
    }
};

TEST_F(FileServerContainmentTest, ServesFilesInsideRoot)
{
    TestFileServer server(root.string());
    EXPECT_TRUE(server.resolve("index.html"));
    EXPECT_TRUE(server.resolve("sub/a.txt"));
    EXPECT_TRUE(server.resolve("/sub/a.txt"));
    EXPECT_TRUE(server.resolve("sub/../index.html"));
}

TEST_F(FileServerContainmentTest, SiblingDirectoryWithSharedPrefixIsRejected)
{
    TestFileServer server(root.string());
    EXPECT_FALSE(server.resolve("../www-private/secret.txt"));
    EXPECT_FALSE(server.resolve("../outside.txt"));
    EXPECT_FALSE(server.resolve("//" + (base / "outside.txt").generic_string()));
}

TEST_F(FileServerContainmentTest, DirectoriesAndMissingFilesAreRejected)
{
    TestFileServer server(root.string());
    EXPECT_FALSE(server.resolve("sub"));
    EXPECT_FALSE(server.resolve("missing.txt"));
    EXPECT_FALSE(server.resolve(std::string("index.html\0.txt", 15)));
}

#ifndef _WIN32
TEST_F(FileServerContainmentTest, CaseIsNotFoldedOnPosix)
{
    TestFileServer server(root.string());
    // "/…/WWW" is a different directory on a case-sensitive file system.
    EXPECT_FALSE(TestFileServer::isPathWithinRoot(base / "WWW" / "x", root));
    EXPECT_TRUE(TestFileServer::isPathWithinRoot(root / "x", root));
    EXPECT_FALSE(TestFileServer::isPathWithinRoot(base / "www-private" / "x", root));
}

TEST_F(FileServerContainmentTest, SymlinkEscapingRootIsRejected)
{
    std::error_code ec;
    fs::create_symlink(base / "outside.txt", root / "link.txt", ec);
    if (ec)
    {
        GTEST_SKIP() << "cannot create symlink: " << ec.message();
    }
    TestFileServer server(root.string());
    EXPECT_FALSE(server.resolve("link.txt"));
}

TEST_F(FileServerContainmentTest, NonRegularFilesAreRejected)
{
    TestFileServer server(root.string());
    EXPECT_FALSE(server.resolve("../../../dev/null"));
    TestFileServer devServer("/dev");
    EXPECT_FALSE(devServer.resolve("null"));  // character device inside root
}
#endif

TEST(FileServerDecodeTest, PercentDecoding)
{
    std::string out;
    EXPECT_TRUE(TestFileServer::decodeUriPath("/a%20b/%2e%2E/c+d", out));
    EXPECT_EQ(out, "/a b/../c+d");  // '+' is literal in paths
    EXPECT_FALSE(TestFileServer::decodeUriPath("/a%00b", out));
    EXPECT_FALSE(TestFileServer::decodeUriPath("/a%zz", out));
    EXPECT_FALSE(TestFileServer::decodeUriPath("/a%2", out));
    EXPECT_FALSE(TestFileServer::decodeUriPath("/a%", out));
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
