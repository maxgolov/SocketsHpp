// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "SocketsHpp/http/common/url_parser.h"

using namespace SOCKETSHPP_NS::http::common;

namespace testing
{
    class UrlParserTest : public ::testing::Test
    {
    protected:
        void SetUp() override {}
        void TearDown() override {}
    };

    // Basic URL parsing tests
    TEST_F(UrlParserTest, BasicHttpUrl)
    {
        UrlParser parser("http://example.com:8080/path");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "http");
        EXPECT_EQ(parser.host_, "example.com");
        EXPECT_EQ(parser.port_, 8080);
        EXPECT_EQ(parser.path_, "/path");
    }

    TEST_F(UrlParserTest, HttpsUrlWithDefaultPort)
    {
        UrlParser parser("https://secure.example.com/api/v1");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "https");
        EXPECT_EQ(parser.host_, "secure.example.com");
        EXPECT_EQ(parser.port_, 443);
        EXPECT_EQ(parser.path_, "/api/v1");
    }

    TEST_F(UrlParserTest, HttpUrlWithDefaultPort)
    {
        UrlParser parser("http://example.com/path");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "http");
        EXPECT_EQ(parser.host_, "example.com");
        EXPECT_EQ(parser.port_, 80);
        EXPECT_EQ(parser.path_, "/path");
    }

    TEST_F(UrlParserTest, UrlWithoutScheme)
    {
        UrlParser parser("example.com:3000/path");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "http");
        EXPECT_EQ(parser.host_, "example.com");
        EXPECT_EQ(parser.port_, 3000);
        EXPECT_EQ(parser.path_, "/path");
    }

    TEST_F(UrlParserTest, UrlWithoutPath)
    {
        UrlParser parser("http://example.com:8080");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "http");
        EXPECT_EQ(parser.host_, "example.com");
        EXPECT_EQ(parser.port_, 8080);
        EXPECT_EQ(parser.path_, "/");
    }

    TEST_F(UrlParserTest, UrlWithQueryString)
    {
        UrlParser parser("http://example.com:8080/path?key1=val1&key2=val2");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "http");
        EXPECT_EQ(parser.host_, "example.com");
        EXPECT_EQ(parser.port_, 8080);
        EXPECT_EQ(parser.path_, "/path");
        EXPECT_EQ(parser.query_, "key1=val1&key2=val2");
    }

    TEST_F(UrlParserTest, MinimalUrlHostAndPort)
    {
        UrlParser parser("localhost:3000");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "http");
        EXPECT_EQ(parser.host_, "localhost");
        EXPECT_EQ(parser.port_, 3000);
        EXPECT_EQ(parser.path_, "/");
    }

    TEST_F(UrlParserTest, IPv4Address)
    {
        UrlParser parser("http://127.0.0.1:8080/api");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "http");
        EXPECT_EQ(parser.host_, "127.0.0.1");
        EXPECT_EQ(parser.port_, 8080);
        EXPECT_EQ(parser.path_, "/api");
    }

    TEST_F(UrlParserTest, ComplexPath)
    {
        UrlParser parser("http://example.com:8080/path1/path2/path3");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.path_, "/path1/path2/path3");
    }

    TEST_F(UrlParserTest, EmptyUrl)
    {
        // An empty string has no host to connect to: parsing must fail
        // (previously success_ was unconditionally true).
        UrlParser parser("");
        EXPECT_FALSE(parser.success_);
        EXPECT_EQ(parser.url_, "");
    }

    TEST_F(UrlParserTest, UrlWithOnlyQueryString)
    {
        UrlParser parser("example.com:8080?key1=val1");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.host_, "example.com");
        EXPECT_EQ(parser.port_, 8080);
        EXPECT_EQ(parser.query_, "key1=val1");
    }

    TEST_F(UrlParserTest, LongComplexUrl)
    {
        UrlParser parser("https://api.example.com:9443/v2/resources/items?filter=active&sort=name&limit=100");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "https");
        EXPECT_EQ(parser.host_, "api.example.com");
        EXPECT_EQ(parser.port_, 9443);
        EXPECT_EQ(parser.path_, "/v2/resources/items");
        EXPECT_EQ(parser.query_, "filter=active&sort=name&limit=100");
    }

    TEST_F(UrlParserTest, CustomScheme)
    {
        UrlParser parser("ftp://files.example.com:21/documents");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "ftp");
        EXPECT_EQ(parser.host_, "files.example.com");
        EXPECT_EQ(parser.port_, 21);
        EXPECT_EQ(parser.path_, "/documents");
    }

    TEST_F(UrlParserTest, UrlWithTrailingSlash)
    {
        UrlParser parser("http://example.com:8080/");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.path_, "/");
    }

    TEST_F(UrlParserTest, UrlWithMultipleSlashesInPath)
    {
        UrlParser parser("http://example.com:8080//path//to//resource");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.path_, "//path//to//resource");
    }

    TEST_F(UrlParserTest, StandardPorts)
    {
        {
            UrlParser parser("http://example.com/path");
            EXPECT_EQ(parser.port_, 80);
        }
        {
            UrlParser parser("https://example.com/path");
            EXPECT_EQ(parser.port_, 443);
        }
    }

    // ---------------------------------------------------------------------
    // Regression tests for authority parsing
    // ---------------------------------------------------------------------

    TEST_F(UrlParserTest, AtSignInQueryDoesNotChangeHost)
    {
        UrlParser parser("http://a.com/x?e=u@evil.com");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.host_, "a.com");
        EXPECT_EQ(parser.port_, 80);
        EXPECT_EQ(parser.path_, "/x");
        EXPECT_EQ(parser.query_, "e=u@evil.com");
    }

    TEST_F(UrlParserTest, AtSignInPathDoesNotChangeHost)
    {
        UrlParser parser("https://good.example/@evil.com/x");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.host_, "good.example");
        EXPECT_EQ(parser.port_, 443);
        EXPECT_EQ(parser.path_, "/@evil.com/x");
    }

    TEST_F(UrlParserTest, UserInfoIsSkipped)
    {
        UrlParser parser("http://user:p@ss@host.example:8081/p");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.host_, "host.example");
        EXPECT_EQ(parser.port_, 8081);
        EXPECT_EQ(parser.path_, "/p");
    }

    TEST_F(UrlParserTest, ColonInPathIsNotAPort)
    {
        UrlParser parser("http://h/p:1");  // used to throw from std::stoi
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.host_, "h");
        EXPECT_EQ(parser.port_, 80);
        EXPECT_EQ(parser.path_, "/p:1");
    }

    TEST_F(UrlParserTest, SchemeInQueryIsNotAScheme)
    {
        UrlParser parser("example.com/redirect?to=http://evil.com");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "http");
        EXPECT_EQ(parser.host_, "example.com");
        EXPECT_EQ(parser.path_, "/redirect");
        EXPECT_EQ(parser.query_, "to=http://evil.com");
    }

    TEST_F(UrlParserTest, NonHttpSchemeWithoutPortHasZeroPort)
    {
        UrlParser parser("ftp://files.example.com/doc");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "ftp");
        EXPECT_EQ(parser.port_, 0);
    }

    TEST_F(UrlParserTest, SchemeIsCaseInsensitive)
    {
        UrlParser parser("HTTPS://Example.com/");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.scheme_, "https");
        EXPECT_EQ(parser.port_, 443);
    }

    TEST_F(UrlParserTest, PortOutOfRangeFails)
    {
        UrlParser parser("http://example.com:65616/");  // used to wrap to 80
        EXPECT_FALSE(parser.success_);
    }

    TEST_F(UrlParserTest, NonNumericPortFails)
    {
        EXPECT_FALSE(UrlParser("http://example.com:80abc/").success_);
        EXPECT_FALSE(UrlParser("http://example.com:-1/").success_);
        EXPECT_FALSE(UrlParser("http://example.com:+80/").success_);
    }

    TEST_F(UrlParserTest, EmptyPortUsesDefault)
    {
        UrlParser parser("http://example.com:/x");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.port_, 80);
    }

    TEST_F(UrlParserTest, MaxPort)
    {
        UrlParser parser("http://example.com:65535");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.port_, 65535);
    }

    TEST_F(UrlParserTest, EmptyHostFails)
    {
        EXPECT_FALSE(UrlParser("http:///path").success_);
        EXPECT_FALSE(UrlParser("http://:8080/path").success_);
        EXPECT_FALSE(UrlParser("http://user@/path").success_);
    }

    TEST_F(UrlParserTest, IPv6LiteralWithPort)
    {
        UrlParser parser("http://[::1]:8080/api?x=1");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.host_, "::1");
        EXPECT_EQ(parser.port_, 8080);
        EXPECT_EQ(parser.path_, "/api");
        EXPECT_EQ(parser.query_, "x=1");
    }

    TEST_F(UrlParserTest, IPv6LiteralWithoutPort)
    {
        UrlParser parser("https://[2001:db8::7]/");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.host_, "2001:db8::7");
        EXPECT_EQ(parser.port_, 443);
        EXPECT_EQ(parser.path_, "/");
    }

    TEST_F(UrlParserTest, MalformedIPv6LiteralFails)
    {
        EXPECT_FALSE(UrlParser("http://[::1/").success_);
        EXPECT_FALSE(UrlParser("http://[::1]x/").success_);
        EXPECT_FALSE(UrlParser("http://[]:80/").success_);
        EXPECT_FALSE(UrlParser("http://[evil.com]/").success_);
    }

    TEST_F(UrlParserTest, FragmentIsStripped)
    {
        UrlParser parser("http://example.com/p?q=1#frag@evil.com");
        EXPECT_TRUE(parser.success_);
        EXPECT_EQ(parser.host_, "example.com");
        EXPECT_EQ(parser.path_, "/p");
        EXPECT_EQ(parser.query_, "q=1");
    }

    TEST_F(UrlParserTest, InvalidSchemeFails)
    {
        EXPECT_FALSE(UrlParser("1http://example.com/").success_);
        EXPECT_FALSE(UrlParser("://example.com/").success_);
    }

}  // namespace testing


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
