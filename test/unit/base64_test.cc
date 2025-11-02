// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <SocketsHpp/utils/base64.h>

using namespace SocketsHpp::utils;

// Test basic encoding
TEST(Base64Test, BasicEncoding)
{
    EXPECT_EQ(Base64::encode(""), "");
    EXPECT_EQ(Base64::encode("f"), "Zg==");
    EXPECT_EQ(Base64::encode("fo"), "Zm8=");
    EXPECT_EQ(Base64::encode("foo"), "Zm9v");
    EXPECT_EQ(Base64::encode("foob"), "Zm9vYg==");
    EXPECT_EQ(Base64::encode("fooba"), "Zm9vYmE=");
    EXPECT_EQ(Base64::encode("foobar"), "Zm9vYmFy");
}

// Test standard test vectors from RFC 4648
TEST(Base64Test, RFC4648TestVectors)
{
    // From RFC 4648 Section 10
    EXPECT_EQ(Base64::encode(""), "");
    EXPECT_EQ(Base64::encode("f"), "Zg==");
    EXPECT_EQ(Base64::encode("fo"), "Zm8=");
    EXPECT_EQ(Base64::encode("foo"), "Zm9v");
    EXPECT_EQ(Base64::encode("foob"), "Zm9vYg==");
    EXPECT_EQ(Base64::encode("fooba"), "Zm9vYmE=");
    EXPECT_EQ(Base64::encode("foobar"), "Zm9vYmFy");
}

// Test encoding with binary pointer interface
TEST(Base64Test, BinaryPointerEncoding)
{
    const char* data = "Hello, World!";
    std::string encoded = Base64::encode(
        reinterpret_cast<const unsigned char*>(data), 
        strlen(data)
    );
    EXPECT_EQ(encoded, "SGVsbG8sIFdvcmxkIQ==");
}

// Test encoding with actual binary data
TEST(Base64Test, BinaryDataEncoding)
{
    unsigned char binary_data[] = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD};
    std::string encoded = Base64::encode(binary_data, sizeof(binary_data));
    EXPECT_EQ(encoded, "AAECA//+/Q==");
}

// Test basic decoding
TEST(Base64Test, BasicDecoding)
{
    EXPECT_EQ(Base64::decode(""), "");
    EXPECT_EQ(Base64::decode("Zg=="), "f");
    EXPECT_EQ(Base64::decode("Zm8="), "fo");
    EXPECT_EQ(Base64::decode("Zm9v"), "foo");
    EXPECT_EQ(Base64::decode("Zm9vYg=="), "foob");
    EXPECT_EQ(Base64::decode("Zm9vYmE="), "fooba");
    EXPECT_EQ(Base64::decode("Zm9vYmFy"), "foobar");
}

// Test decoding RFC 4648 test vectors
TEST(Base64Test, RFC4648Decoding)
{
    EXPECT_EQ(Base64::decode(""), "");
    EXPECT_EQ(Base64::decode("Zg=="), "f");
    EXPECT_EQ(Base64::decode("Zm8="), "fo");
    EXPECT_EQ(Base64::decode("Zm9v"), "foo");
    EXPECT_EQ(Base64::decode("Zm9vYg=="), "foob");
    EXPECT_EQ(Base64::decode("Zm9vYmE="), "fooba");
    EXPECT_EQ(Base64::decode("Zm9vYmFy"), "foobar");
}

// Test round-trip encoding and decoding
TEST(Base64Test, RoundTrip)
{
    std::string original = "The quick brown fox jumps over the lazy dog";
    std::string encoded = Base64::encode(original);
    std::string decoded = Base64::decode(encoded);
    EXPECT_EQ(original, decoded);
}

// Test round-trip with binary data
TEST(Base64Test, BinaryRoundTrip)
{
    unsigned char original[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8
    };
    
    std::string encoded = Base64::encode(original, sizeof(original));
    std::string decoded = Base64::decode(encoded);
    
    ASSERT_EQ(decoded.size(), sizeof(original));
    EXPECT_EQ(memcmp(decoded.c_str(), original, sizeof(original)), 0);
}

// Test encoding with special characters
TEST(Base64Test, SpecialCharacters)
{
    std::string original = "!@#$%^&*()_+-={}[]|\\:;\"'<>,.?/~`";
    std::string encoded = Base64::encode(original);
    std::string decoded = Base64::decode(encoded);
    EXPECT_EQ(original, decoded);
}

// Test encoding with unicode (UTF-8)
TEST(Base64Test, UTF8Encoding)
{
    std::string original = "Hello 世界 مرحبا мир";
    std::string encoded = Base64::encode(original);
    std::string decoded = Base64::decode(encoded);
    EXPECT_EQ(original, decoded);
}

// Test encoding with all printable ASCII characters
TEST(Base64Test, AllPrintableASCII)
{
    std::string original;
    for (int i = 32; i < 127; i++)
    {
        original += static_cast<char>(i);
    }
    std::string encoded = Base64::encode(original);
    std::string decoded = Base64::decode(encoded);
    EXPECT_EQ(original, decoded);
}

// Test encoding with all possible byte values
TEST(Base64Test, AllByteValues)
{
    unsigned char original[256];
    for (int i = 0; i < 256; i++)
    {
        original[i] = static_cast<unsigned char>(i);
    }
    
    std::string encoded = Base64::encode(original, 256);
    std::string decoded = Base64::decode(encoded);
    
    ASSERT_EQ(decoded.size(), 256u);
    EXPECT_EQ(memcmp(decoded.c_str(), original, 256), 0);
}

// Test validation function
TEST(Base64Test, Validation)
{
    EXPECT_TRUE(Base64::is_valid(""));
    EXPECT_TRUE(Base64::is_valid("Zg=="));
    EXPECT_TRUE(Base64::is_valid("Zm8="));
    EXPECT_TRUE(Base64::is_valid("Zm9v"));
    EXPECT_TRUE(Base64::is_valid("SGVsbG8sIFdvcmxkIQ=="));
    
    // Invalid characters
    EXPECT_FALSE(Base64::is_valid("Zg@="));
    EXPECT_FALSE(Base64::is_valid("Zm#8="));
    EXPECT_FALSE(Base64::is_valid("Hello!"));
}

// Test invalid base64 decoding
TEST(Base64Test, InvalidDecoding)
{
    // Invalid characters should throw
    EXPECT_THROW(Base64::decode("Zg@="), std::invalid_argument);
    EXPECT_THROW(Base64::decode("Zm#8="), std::invalid_argument);
}

// Test namespace convenience functions
TEST(Base64Test, NamespaceFunctions)
{
    using namespace SocketsHpp::utils::base64;
    
    std::string original = "namespace test";
    std::string encoded = encode(original);
    std::string decoded = decode(encoded);
    
    EXPECT_EQ(original, decoded);
    EXPECT_TRUE(is_valid(encoded));
    EXPECT_FALSE(is_valid("invalid@base64"));
}

// Test known examples
TEST(Base64Test, KnownExamples)
{
    // Wikipedia example
    EXPECT_EQ(Base64::encode("Man"), "TWFu");
    EXPECT_EQ(Base64::decode("TWFu"), "Man");
    
    // Common phrases
    EXPECT_EQ(Base64::encode("Hello, World!"), "SGVsbG8sIFdvcmxkIQ==");
    EXPECT_EQ(Base64::decode("SGVsbG8sIFdvcmxkIQ=="), "Hello, World!");
    
    // The quick brown fox
    EXPECT_EQ(
        Base64::encode("The quick brown fox jumps over the lazy dog"),
        "VGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZw=="
    );
}

// Test large data encoding/decoding
TEST(Base64Test, LargeData)
{
    // Create 10KB of data
    std::string original(10240, '\0');
    for (size_t i = 0; i < original.size(); i++)
    {
        original[i] = static_cast<char>(i % 256);
    }
    
    std::string encoded = Base64::encode(original);
    std::string decoded = Base64::decode(encoded);
    
    EXPECT_EQ(original, decoded);
}

// Test padding variations
TEST(Base64Test, PaddingVariations)
{
    // No padding needed (length % 3 == 0)
    std::string test1 = "abc";  // 3 bytes
    std::string enc1 = Base64::encode(test1);
    EXPECT_EQ(enc1, "YWJj");  // Should be 4 characters, no padding
    EXPECT_NE(enc1.back(), '=');  // Should not end with =
    EXPECT_EQ(Base64::decode(enc1), test1);
    
    // One padding character (length % 3 == 2)
    std::string test2 = "ab";  // 2 bytes
    std::string enc2 = Base64::encode(test2);
    EXPECT_EQ(enc2, "YWI=");
    EXPECT_EQ(enc2.back(), '=');  // Should end with one =
    EXPECT_EQ(Base64::decode(enc2), test2);
    
    // Two padding characters (length % 3 == 1)
    std::string test3 = "a";  // 1 byte
    std::string enc3 = Base64::encode(test3);
    EXPECT_EQ(enc3, "YQ==");
    EXPECT_EQ(enc3.substr(enc3.size() - 2), "==");  // Should end with ==
    EXPECT_EQ(Base64::decode(enc3), test3);
}

// Test empty and single character
TEST(Base64Test, EdgeCases)
{
    EXPECT_EQ(Base64::encode(""), "");
    EXPECT_EQ(Base64::decode(""), "");
    
    EXPECT_EQ(Base64::encode("a"), "YQ==");
    EXPECT_EQ(Base64::decode("YQ=="), "a");
}

// Test Base64 alphabet coverage
TEST(Base64Test, AlphabetCoverage)
{
    // Encode data that will produce all 64 base64 characters
    // The base64 alphabet is: A-Z, a-z, 0-9, +, /
    
    // This specific byte sequence produces all characters
    unsigned char data[] = {
        0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20, 0x92, 0x8b, 0x30, 0xd3, 0x8f,
        0x41, 0x14, 0x93, 0x51, 0x55, 0x97, 0x61, 0x96, 0x9b, 0x71, 0xd7, 0x9f,
        0x82, 0x18, 0xa3, 0x92, 0x59, 0xa7, 0xa2, 0x9a, 0xab, 0xb2, 0xdb, 0xaf,
        0xc3, 0x1c, 0xb3, 0xd3, 0x5d, 0xb7, 0xe3, 0x9e, 0xbb, 0xf3, 0xdf, 0xbf
    };
    
    std::string encoded = Base64::encode(data, sizeof(data));
    std::string decoded = Base64::decode(encoded);
    
    ASSERT_EQ(decoded.size(), sizeof(data));
    EXPECT_EQ(memcmp(decoded.c_str(), data, sizeof(data)), 0);
}

// Test with binary pointer and zero-length
TEST(Base64Test, ZeroLengthBinary)
{
    const unsigned char* data = nullptr;
    std::string encoded = Base64::encode(data, 0);
    EXPECT_EQ(encoded, "");
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
