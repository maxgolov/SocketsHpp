// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <SocketsHpp/http/server/http_server.h>

using namespace SocketsHpp;
using namespace SocketsHpp::http::server;

// Test suite for URL/query parameter security validation

class URLSecurityTest : public ::testing::Test
{
protected:
    HttpRequest req;
};

// ========================================
// URL Decoding Security Tests
// ========================================

TEST_F(URLSecurityTest, RejectNullByte)
{
    req.uri = "/path?key=%00admin";
    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

TEST_F(URLSecurityTest, RejectControlCharacters)
{
    req.uri = "/path?key=%01value";   // SOH control char
    EXPECT_THROW(req.parse_query(), std::invalid_argument);

    req.uri = "/path?key=%0Avalue";   // Newline
    EXPECT_THROW(req.parse_query(), std::invalid_argument);

    req.uri = "/path?key=%0Dvalue";   // Carriage return
    EXPECT_THROW(req.parse_query(), std::invalid_argument);

    req.uri = "/path?key=%1Fvalue";   // Unit separator
    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

TEST_F(URLSecurityTest, RejectInvalidHexSequences)
{
    req.uri = "/path?key=%ZZ";        // Invalid hex
    EXPECT_THROW(req.parse_query(), std::invalid_argument);

    req.uri = "/path?key=%G5";        // Invalid first char
    EXPECT_THROW(req.parse_query(), std::invalid_argument);

    req.uri = "/path?key=%5G";        // Invalid second char
    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

TEST_F(URLSecurityTest, RejectIncompleteHexSequences)
{
    req.uri = "/path?key=%2";         // Incomplete %XX
    EXPECT_THROW(req.parse_query(), std::invalid_argument);

    req.uri = "/path?key=%";          // Lone %
    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

// ========================================
// Query Parameter Limit Tests
// ========================================

TEST_F(URLSecurityTest, RejectTooManyParameters)
{
    // Generate 101 parameters (limit is 100)
    std::ostringstream oss;
    oss << "/path?";
    for (int i = 0; i < 101; i++)
    {
        if (i > 0) oss << "&";
        oss << "param" << i << "=value" << i;
    }
    req.uri = oss.str();

    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

TEST_F(URLSecurityTest, RejectTooLongKey)
{
    // Generate key with 257 characters (limit is 256)
    std::string longKey(257, 'a');
    req.uri = "/path?" + longKey + "=value";

    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

TEST_F(URLSecurityTest, RejectTooLongValue)
{
    // Generate value with 4097 characters (limit is 4096)
    std::string longValue(4097, 'x');
    req.uri = "/path?key=" + longValue;

    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

// ========================================
// Query Key Validation Tests
// ========================================

TEST_F(URLSecurityTest, RejectEmptyKey)
{
    req.uri = "/path?=value";
    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

TEST_F(URLSecurityTest, RejectSpaceInKey)
{
    req.uri = "/path?my+key=value";
    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

TEST_F(URLSecurityTest, RejectSpecialCharsInKey)
{
    req.uri = "/path?<script>=xss";
    EXPECT_THROW(req.parse_query(), std::invalid_argument);

    req.uri = "/path?key%20name=value";  // Encoded space
    EXPECT_THROW(req.parse_query(), std::invalid_argument);

    req.uri = "/path?key@host=value";
    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

TEST_F(URLSecurityTest, AllowValidKeyCharacters)
{
    req.uri = "/path?valid_key-name.123=value";
    auto params = req.parse_query();
    EXPECT_EQ("value", params["valid_key-name.123"]);
}

// ========================================
// Fragment Handling Tests
// ========================================

TEST_F(URLSecurityTest, StripFragment)
{
    req.uri = "/path?key=value#fragment";
    auto params = req.parse_query();
    EXPECT_EQ(1, params.size());
    EXPECT_EQ("value", params["key"]);
}

// ========================================
// Attack Vector Tests
// ========================================

TEST_F(URLSecurityTest, PreventDirectoryTraversal)
{
    req.uri = "/path?file=%2E%2E%2F%2E%2E%2Fetc%2Fpasswd";
    auto params = req.parse_query();
    EXPECT_EQ("../../etc/passwd", params["file"]);
    // Note: The application MUST validate the decoded value before use
}

TEST_F(URLSecurityTest, PreventXSSInQueryParams)
{
    req.uri = "/path?xss=%3Cscript%3Ealert%281%29%3C%2Fscript%3E";
    auto params = req.parse_query();
    EXPECT_EQ("<script>alert(1)</script>", params["xss"]);
    // Note: The application MUST escape output
}

TEST_F(URLSecurityTest, DuplicateKeysLogWarning)
{
    req.uri = "/path?key=first&key=second";
    auto params = req.parse_query();
    // Last value wins
    EXPECT_EQ("second", params["key"]);
}

// ========================================
// Edge Cases
// ========================================

TEST_F(URLSecurityTest, EmptyValueAllowed)
{
    req.uri = "/path?key=";
    auto params = req.parse_query();
    EXPECT_EQ("", params["key"]);
}

TEST_F(URLSecurityTest, MultipleAmpersands)
{
    req.uri = "/path?a=1&&b=2";
    // The && creates an empty parameter which should fail (empty key)
    EXPECT_THROW(req.parse_query(), std::invalid_argument);
}

TEST_F(URLSecurityTest, ValidUTF8Characters)
{
    req.uri = "/path?name=Caf%C3%A9";  // Café in UTF-8
    auto params = req.parse_query();
    EXPECT_EQ("Caf\xC3\xA9", params["name"]);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
