// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>
#include "SocketsHpp/http/server/http_server.h"
#include <map>
#include <string>
#include <vector>

using namespace SocketsHpp;
using namespace SocketsHpp::http::server;

// Test query parameter parsing
TEST(HttpMethodsTest, QueryParameterParsing)
{
    HttpRequest req;
    req.uri = "/search?q=hello&page=2&filter=active";
    
    auto params = req.parse_query();
    
    EXPECT_EQ(3, params.size());
    EXPECT_EQ("hello", params["q"]);
    EXPECT_EQ("2", params["page"]);
    EXPECT_EQ("active", params["filter"]);
}

// Test URL decoding in query parameters
TEST(HttpMethodsTest, QueryParameterURLDecoding)
{
    HttpRequest req;
    req.uri = "/test?name=John+Doe&email=user%40example.com&msg=Hello%20World";
    
    auto params = req.parse_query();
    
    EXPECT_EQ("John Doe", params["name"]);
    EXPECT_EQ("user@example.com", params["email"]);
    EXPECT_EQ("Hello World", params["msg"]);
}

// Test Accept header parsing
TEST(HttpMethodsTest, AcceptHeaderParsing)
{
    HttpRequest req;
    
    // Test basic Accept header
    req.headers["Accept"] = "application/json, text/html";
    auto types = req.get_accepted_types();
    EXPECT_EQ(2, types.size());
    EXPECT_EQ("application/json", types[0]);
    EXPECT_EQ("text/html", types[1]);
    
    // Test with quality factors (should be stripped)
    req.headers["Accept"] = "text/html;q=0.9, application/json;q=1.0";
    types = req.get_accepted_types();
    EXPECT_EQ(2, types.size());
    EXPECT_EQ("text/html", types[0]);
    EXPECT_EQ("application/json", types[1]);
    
    // Test wildcard
    req.headers["Accept"] = "*/*";
    types = req.get_accepted_types();
    EXPECT_EQ(1, types.size());
    EXPECT_EQ("*/*", types[0]);
}

// Test accepts() method
TEST(HttpMethodsTest, AcceptsMethod)
{
    HttpRequest req;
    
    // No Accept header = accept all
    EXPECT_TRUE(req.accepts("application/json"));
    EXPECT_TRUE(req.accepts("text/html"));
    
    // Exact match
    req.headers["Accept"] = "application/json, text/html";
    EXPECT_TRUE(req.accepts("application/json"));
    EXPECT_TRUE(req.accepts("text/html"));
    EXPECT_FALSE(req.accepts("application/xml"));
    
    // Wildcard match
    req.headers["Accept"] = "*/*";
    EXPECT_TRUE(req.accepts("application/json"));
    EXPECT_TRUE(req.accepts("anything/really"));
    
    // Type wildcard
    req.headers["Accept"] = "application/*";
    EXPECT_TRUE(req.accepts("application/json"));
    EXPECT_TRUE(req.accepts("application/xml"));
    EXPECT_FALSE(req.accepts("text/html"));
}

// Test query parsing with no query string
TEST(HttpMethodsTest, NoQueryString)
{
    HttpRequest req;
    req.uri = "/simple/path";
    
    auto params = req.parse_query();
    EXPECT_TRUE(params.empty());
}

// Test query parsing with empty value
TEST(HttpMethodsTest, EmptyQueryValue)
{
    HttpRequest req;
    req.uri = "/test?key=&another=value";
    
    auto params = req.parse_query();
    EXPECT_EQ("", params["key"]);
    EXPECT_EQ("value", params["another"]);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// Test multiple query parameters
TEST(HttpMethodsTest, MultipleQueryParameters)
{
    HttpRequest req;
    req.uri = "/api?session=abc123&format=json&pretty=1";
    
    auto params = req.parse_query();
    EXPECT_EQ(3, params.size());
    EXPECT_EQ("abc123", params["session"]);
    EXPECT_EQ("json", params["format"]);
    EXPECT_EQ("1", params["pretty"]);
}

// Test special URL characters
TEST(HttpMethodsTest, SpecialURLCharacters)
{
    HttpRequest req;
    req.uri = "/search?q=C%2B%2B&topic=100%25+coverage";
    
    auto params = req.parse_query();
    EXPECT_EQ("C++", params["q"]);
    EXPECT_EQ("100% coverage", params["topic"]);
}

// Test Accept header with no whitespace
TEST(HttpMethodsTest, AcceptNoWhitespace)
{
    HttpRequest req;
    req.headers["Accept"] = "application/json,text/html";
    
    auto types = req.get_accepted_types();
    EXPECT_EQ(2, types.size());
    EXPECT_EQ("application/json", types[0]);
    EXPECT_EQ("text/html", types[1]);
}

// Test Accept header edge cases
TEST(HttpMethodsTest, AcceptEdgeCases)
{
    HttpRequest req;
    
    // Empty Accept header
    req.headers["Accept"] = "";
    auto types = req.get_accepted_types();
    EXPECT_TRUE(types.empty());
    
    // Single type
    req.headers["Accept"] = "application/json";
    types = req.get_accepted_types();
    EXPECT_EQ(1, types.size());
    EXPECT_EQ("application/json", types[0]);
}
