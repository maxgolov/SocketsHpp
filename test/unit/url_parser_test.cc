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
        UrlParser parser("");
        EXPECT_TRUE(parser.success_);
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

}  // namespace testing


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
