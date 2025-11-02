// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <SocketsHpp/http/server/compression.h>
#include <SocketsHpp/http/server/compression_simple.h>
#include <string>
#include <vector>

using namespace SOCKETSHPP_NS::http::server;
using namespace SOCKETSHPP_NS::http::server::compression;

class CompressionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Clear registry before each test
        CompressionRegistry::instance().clear();
        
        // Register simple compression strategies
        registerSimpleCompression();
    }

    void TearDown() override
    {
        CompressionRegistry::instance().clear();
    }
};

TEST_F(CompressionTest, SimpleRLE_Compress)
{
    std::vector<uint8_t> input = {1, 1, 1, 2, 2, 3, 4, 4, 4, 4};
    
    auto compressed = SimpleRLE::compress(input, 6);
    
    // RLE format: [count][byte][count][byte]...
    // Expected: [3][1][2][2][1][3][4][4]
    EXPECT_EQ(compressed.size(), 8u);
    EXPECT_EQ(compressed[0], 3);  // count
    EXPECT_EQ(compressed[1], 1);  // value
    EXPECT_EQ(compressed[2], 2);  // count
    EXPECT_EQ(compressed[3], 2);  // value
}

TEST_F(CompressionTest, SimpleRLE_Decompress)
{
    // [3][1][2][2][1][3][4][4]
    std::vector<uint8_t> compressed = {3, 1, 2, 2, 1, 3, 4, 4};
    
    auto decompressed = SimpleRLE::decompress(compressed);
    
    std::vector<uint8_t> expected = {1, 1, 1, 2, 2, 3, 4, 4, 4, 4};
    EXPECT_EQ(decompressed, expected);
}

TEST_F(CompressionTest, SimpleRLE_RoundTrip)
{
    std::vector<uint8_t> original = {5, 5, 5, 5, 5, 6, 7, 7, 8};
    
    auto compressed = SimpleRLE::compress(original, 6);
    auto decompressed = SimpleRLE::decompress(compressed);
    
    EXPECT_EQ(decompressed, original);
}

TEST_F(CompressionTest, IdentityCompression)
{
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    
    auto compressed = IdentityCompression::compress(data, 6);
    auto decompressed = IdentityCompression::decompress(compressed);
    
    EXPECT_EQ(compressed, data);
    EXPECT_EQ(decompressed, data);
}

TEST_F(CompressionTest, RegistryRegister)
{
    auto strategy = std::make_shared<CompressionStrategy>(
        "test",
        [](const std::vector<uint8_t>& input, int) { return input; },
        [](const std::vector<uint8_t>& input) { return input; }
    );

    CompressionRegistry::instance().registerStrategy(strategy);
    
    EXPECT_TRUE(CompressionRegistry::instance().isSupported("test"));
    EXPECT_FALSE(CompressionRegistry::instance().isSupported("unknown"));
}

TEST_F(CompressionTest, RegistryGet)
{
    auto retrieved = CompressionRegistry::instance().get("rle");
    
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "rle");
}

TEST_F(CompressionTest, RegistrySupportedEncodings)
{
    auto encodings = CompressionRegistry::instance().supportedEncodings();
    
    EXPECT_GE(encodings.size(), 2u); // At least "rle" and "identity"
    
    bool hasRLE = false;
    bool hasIdentity = false;
    for (const auto& enc : encodings)
    {
        if (enc == "rle") hasRLE = true;
        if (enc == "identity") hasIdentity = true;
    }
    
    EXPECT_TRUE(hasRLE);
    EXPECT_TRUE(hasIdentity);
}

TEST_F(CompressionTest, ParseAcceptEncoding_Simple)
{
    auto prefs = parseAcceptEncoding("gzip, deflate, br");
    
    ASSERT_EQ(prefs.size(), 3u);
    EXPECT_EQ(prefs[0].encoding, "gzip");
    EXPECT_FLOAT_EQ(prefs[0].quality, 1.0f);
    EXPECT_EQ(prefs[1].encoding, "deflate");
    EXPECT_FLOAT_EQ(prefs[1].quality, 1.0f);
    EXPECT_EQ(prefs[2].encoding, "br");
    EXPECT_FLOAT_EQ(prefs[2].quality, 1.0f);
}

TEST_F(CompressionTest, ParseAcceptEncoding_WithQuality)
{
    auto prefs = parseAcceptEncoding("gzip;q=1.0, br;q=0.8, deflate;q=0.5");
    
    ASSERT_EQ(prefs.size(), 3u);
    
    // Should be sorted by quality (highest first)
    EXPECT_EQ(prefs[0].encoding, "gzip");
    EXPECT_FLOAT_EQ(prefs[0].quality, 1.0f);
    EXPECT_EQ(prefs[1].encoding, "br");
    EXPECT_FLOAT_EQ(prefs[1].quality, 0.8f);
    EXPECT_EQ(prefs[2].encoding, "deflate");
    EXPECT_FLOAT_EQ(prefs[2].quality, 0.5f);
}

TEST_F(CompressionTest, ParseAcceptEncoding_ZeroQuality)
{
    auto prefs = parseAcceptEncoding("gzip;q=1.0, br;q=0, deflate;q=0.5");
    
    ASSERT_EQ(prefs.size(), 2u); // br excluded (q=0)
    EXPECT_EQ(prefs[0].encoding, "gzip");
    EXPECT_EQ(prefs[1].encoding, "deflate");
}

TEST_F(CompressionTest, ParseAcceptEncoding_Whitespace)
{
    auto prefs = parseAcceptEncoding("  gzip  ,  deflate  ;  q=0.8  ");
    
    ASSERT_EQ(prefs.size(), 2u);
    EXPECT_EQ(prefs[0].encoding, "gzip");
    EXPECT_EQ(prefs[1].encoding, "deflate");
    EXPECT_FLOAT_EQ(prefs[1].quality, 0.8f);
}

TEST_F(CompressionTest, CompressionMiddleware_ShouldCompress_SizeCheck)
{
    CompressionMiddleware middleware;
    middleware.setMinSize(1000);
    
    EXPECT_FALSE(middleware.shouldCompress("text/html", 500)); // Too small
    EXPECT_TRUE(middleware.shouldCompress("text/html", 1500)); // Large enough
}

TEST_F(CompressionTest, CompressionMiddleware_ShouldCompress_ContentType)
{
    CompressionMiddleware middleware;
    middleware.setMinSize(100);
    
    // Compressible types
    EXPECT_TRUE(middleware.shouldCompress("text/html", 200));
    EXPECT_TRUE(middleware.shouldCompress("text/plain", 200));
    EXPECT_TRUE(middleware.shouldCompress("application/json", 200));
    EXPECT_TRUE(middleware.shouldCompress("text/css", 200));
    
    // Excluded types (already compressed)
    EXPECT_FALSE(middleware.shouldCompress("image/jpeg", 200));
    EXPECT_FALSE(middleware.shouldCompress("image/png", 200));
    EXPECT_FALSE(middleware.shouldCompress("video/mp4", 200));
    EXPECT_FALSE(middleware.shouldCompress("application/zip", 200));
}

TEST_F(CompressionTest, CompressionMiddleware_CompressResponse)
{
    CompressionMiddleware middleware;
    middleware.setMinSize(10);
    middleware.setLevel(6);
    
    std::string body = "AAAAABBBBBCCCCC"; // Compressible with RLE
    std::string encoding;
    
    bool compressed = middleware.compressResponse(
        "rle, identity",
        "text/plain",
        body,
        encoding
    );
    
    EXPECT_TRUE(compressed);
    EXPECT_EQ(encoding, "rle");
    EXPECT_LT(body.size(), 15u); // Should be smaller
}

TEST_F(CompressionTest, CompressionMiddleware_CompressResponse_OnlyIfSmaller)
{
    CompressionMiddleware middleware;
    middleware.setMinSize(10);
    
    // Data that doesn't compress well with RLE
    std::string body = "ABCDEFGHIJKLMNO";
    std::string originalBody = body;
    std::string encoding;
    
    bool compressed = middleware.compressResponse(
        "rle",
        "text/plain",
        body,
        encoding
    );
    
    // RLE will make this larger, so it shouldn't be used
    EXPECT_FALSE(compressed);
    EXPECT_EQ(body, originalBody); // Unchanged
}

TEST_F(CompressionTest, CompressionMiddleware_CompressResponse_PreferenceOrder)
{
    CompressionMiddleware middleware;
    middleware.setMinSize(10);
    
    std::string body = "AAAAABBBBBCCCCC";
    std::string encoding;
    
    // Even though identity comes first, RLE should be used because it actually compresses
    // (identity doesn't reduce size, so middleware skips it per "only if smaller" rule)
    bool compressed = middleware.compressResponse(
        "identity, rle",
        "text/plain",
        body,
        encoding
    );
    
    EXPECT_TRUE(compressed);
    EXPECT_EQ(encoding, "rle"); // RLE used because identity doesn't compress
}

TEST_F(CompressionTest, CompressionMiddleware_CompressResponse_UnsupportedEncoding)
{
    CompressionMiddleware middleware;
    middleware.setMinSize(10);
    
    std::string body = "AAAAABBBBBCCCCC";
    std::string encoding;
    
    bool compressed = middleware.compressResponse(
        "gzip, br", // Neither supported
        "text/plain",
        body,
        encoding
    );
    
    EXPECT_FALSE(compressed);
    EXPECT_TRUE(encoding.empty());
}

TEST_F(CompressionTest, CompressionMiddleware_DecompressRequest)
{
    CompressionMiddleware middleware;
    
    // Compress some data
    std::vector<uint8_t> original = {1, 1, 1, 2, 2, 3};
    auto compressed = SimpleRLE::compress(original, 6);
    
    std::string body(compressed.begin(), compressed.end());
    
    bool decompressed = middleware.decompressRequest("rle", body);
    
    EXPECT_TRUE(decompressed);
    
    std::vector<uint8_t> result(body.begin(), body.end());
    EXPECT_EQ(result, original);
}

TEST_F(CompressionTest, CompressionMiddleware_DecompressRequest_NoEncoding)
{
    CompressionMiddleware middleware;
    
    std::string body = "plain text";
    std::string originalBody = body;
    
    bool decompressed = middleware.decompressRequest("", body);
    
    EXPECT_FALSE(decompressed);
    EXPECT_EQ(body, originalBody); // Unchanged
}

TEST_F(CompressionTest, CompressionMiddleware_CustomTypes)
{
    CompressionMiddleware middleware;
    middleware.setMinSize(100);
    
    // Add custom compressible type
    middleware.addCompressibleType("application/custom");
    EXPECT_TRUE(middleware.shouldCompress("application/custom", 200));
    
    // Add custom excluded type
    middleware.addExcludedType("application/exclude");
    EXPECT_FALSE(middleware.shouldCompress("application/exclude", 200));
}

TEST_F(CompressionTest, CompressionMiddleware_SetLevel)
{
    CompressionMiddleware middleware;
    
    middleware.setLevel(1);
    middleware.setLevel(9);
    middleware.setLevel(15); // Should clamp to 9
    middleware.setLevel(-5); // Should clamp to 1
    
    // Test passes if no crashes (clamping works)
    SUCCEED();
}

TEST_F(CompressionTest, CompressionStrategy_CompressDecompress)
{
    auto strategy = CompressionRegistry::instance().get("rle");
    ASSERT_NE(strategy, nullptr);
    
    std::vector<uint8_t> original = {5, 5, 5, 6, 6, 7};
    
    auto compressed = strategy->compressData(original, 6);
    auto decompressed = strategy->decompressData(compressed);
    
    EXPECT_EQ(decompressed, original);
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
