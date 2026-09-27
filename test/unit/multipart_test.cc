// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the multipart/form-data parser (http/server/multipart.h):
// boundary extraction, valid and binary bodies, malformed input, limits and a
// small randomized fuzz (no crash, views stay inside the input).

#include <gtest/gtest.h>

#include <SocketsHpp/http/server/multipart.h>

#include <random>
#include <string>

using namespace SocketsHpp::http::server;
namespace mp = SocketsHpp::http::server::multipart;

namespace
{
    const std::string kBoundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";

    std::string field(const std::string& name, const std::string& value)
    {
        return "--" + kBoundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
    }

    std::string sampleBody()
    {
        return field("title", "Hello") +
            "--" + kBoundary + "\r\n"
            "Content-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n"
            "Content-Type: text/plain\r\n\r\n"
            "line1\r\nline2\r\n" +
            field("empty", "") +
            "--" + kBoundary + "--\r\n";
    }
}  // namespace

TEST(MultipartBoundaryTest, FromContentType)
{
    EXPECT_EQ(mp::boundaryFromContentType("multipart/form-data; boundary=abc"), "abc");
    EXPECT_EQ(mp::boundaryFromContentType("Multipart/Form-Data;boundary=\"a b;c\""), "a b;c");
    EXPECT_EQ(mp::boundaryFromContentType("multipart/mixed; charset=utf-8; BOUNDARY=x-y"), "x-y");
    EXPECT_EQ(mp::boundaryFromContentType("multipart/form-data; boundary=\"q\\\"d\""), "q\"d");
    EXPECT_FALSE(mp::boundaryFromContentType("text/plain; boundary=abc"));
    EXPECT_FALSE(mp::boundaryFromContentType("multipart/form-data"));
    EXPECT_FALSE(mp::boundaryFromContentType("multipart/form-data; boundary="));
    EXPECT_FALSE(mp::boundaryFromContentType("multipart/form-data; boundary=\"\""));
    EXPECT_FALSE(mp::boundaryFromContentType("multipart/form-data; boundary=" + std::string(71, 'a')));
    EXPECT_TRUE(mp::boundaryFromContentType("multipart/form-data; boundary=" + std::string(70, 'a')));
    EXPECT_FALSE(mp::boundaryFromContentType(""));
    EXPECT_FALSE(mp::boundaryFromContentType(";;;=;\"\""));
}

TEST(MultipartParseTest, FieldsAndFile)
{
    const std::string body = sampleBody();
    mp::Result r = mp::parse(body, kBoundary);
    ASSERT_TRUE(r.ok()) << mp::errorMessage(r.error);
    ASSERT_EQ(r.parts.size(), 3u);

    EXPECT_EQ(r.parts[0].name, "title");
    EXPECT_EQ(r.parts[0].data, "Hello");
    EXPECT_FALSE(r.parts[0].isFile());
    EXPECT_TRUE(r.parts[0].contentType.empty());

    EXPECT_EQ(r.parts[1].name, "file");
    EXPECT_TRUE(r.parts[1].isFile());
    EXPECT_EQ(r.parts[1].filename, "a.txt");
    EXPECT_EQ(r.parts[1].contentType, "text/plain");
    EXPECT_EQ(r.parts[1].header("content-type"), "text/plain");
    EXPECT_EQ(r.parts[1].body(), "line1\r\nline2");

    EXPECT_EQ(r.parts[2].name, "empty");
    EXPECT_EQ(r.parts[2].data, "");

    ASSERT_NE(r.find("file"), nullptr);
    EXPECT_EQ(r.find("file")->filename, "a.txt");
    EXPECT_EQ(r.find("nope"), nullptr);
}

TEST(MultipartParseTest, BinarySafe)
{
    std::string payload;
    for (int i = 0; i < 256; ++i)
    {
        payload.push_back(static_cast<char>(i));
    }
    payload += std::string("\r\n--notTheBoundary\r\n\0\0", 22);
    payload += "\r\n-" + kBoundary;  // almost a delimiter
    const std::string body = "--" + kBoundary + "\r\n"
        "Content-Disposition: form-data; name=\"bin\"; filename=\"x.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n" + payload + "\r\n"
        "--" + kBoundary + "--";
    mp::Result r = mp::parse(body, kBoundary);
    ASSERT_TRUE(r.ok()) << mp::errorMessage(r.error);
    ASSERT_EQ(r.parts.size(), 1u);
    EXPECT_EQ(r.parts[0].data.size(), payload.size());
    EXPECT_EQ(r.parts[0].body(), payload);
    EXPECT_EQ(r.parts[0].data.data(), body.data() + body.find(payload));  // a view, no copy
}

TEST(MultipartParseTest, PreambleEpiloguePaddingAndLenientLf)
{
    const std::string body = "This is the preamble.\r\n"
        "--" + kBoundary + "  \t\r\n"                  // transport padding
        "content-disposition: form-data; name=a\r\n"   // lower-case name, token value
        "X-Folded: one\r\n two\r\n\r\n"
        "1\r\n"
        "--" + kBoundary + "\n"                         // bare LF after the delimiter
        "\r\n"                                          // no headers at all
        "2\r\n"
        "--" + kBoundary + "--\r\nepilogue ignored\r\n--" + kBoundary + "\r\n";
    mp::Result r = mp::parse(body, kBoundary);
    ASSERT_TRUE(r.ok()) << mp::errorMessage(r.error);
    ASSERT_EQ(r.parts.size(), 2u);
    EXPECT_EQ(r.parts[0].name, "a");
    EXPECT_EQ(r.parts[0].header("x-folded"), "one two");
    EXPECT_EQ(r.parts[0].data, "1");
    EXPECT_TRUE(r.parts[1].headers.empty());
    EXPECT_EQ(r.parts[1].data, "2");
}

TEST(MultipartParseTest, FilenameVariants)
{
    const std::string body = "--" + kBoundary + "\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"fallback.txt\"; filename*=UTF-8''%E2%82%AC%20rates.txt\r\n\r\n"
        "x\r\n"
        "--" + kBoundary + "\r\n"
        "Content-Disposition: form-data; name=\"g\"; filename=\"C:\\\\dir\\\\q\\\"uote.txt\"\r\n\r\n"
        "y\r\n"
        "--" + kBoundary + "\r\n"
        "Content-Disposition: form-data; name=\"h\"; filename=\"\"\r\n\r\n"
        "\r\n"
        "--" + kBoundary + "\r\n"
        "Content-Disposition: form-data; name=\"i\"; filename*=bad%zz\r\n\r\n"
        "z\r\n"
        "--" + kBoundary + "--";
    mp::Result r = mp::parse(body, kBoundary);
    ASSERT_TRUE(r.ok()) << mp::errorMessage(r.error);
    ASSERT_EQ(r.parts.size(), 4u);
    EXPECT_EQ(r.parts[0].filename, "\xE2\x82\xAC rates.txt");
    EXPECT_EQ(r.parts[1].filename, "C:\\dir\\q\"uote.txt");
    EXPECT_TRUE(r.parts[2].isFile());
    EXPECT_EQ(r.parts[2].filename, "");
    EXPECT_FALSE(r.parts[3].isFile());  // malformed filename* ignored
}

TEST(MultipartParseTest, MalformedInputIsRejected)
{
    const std::string d = "--" + kBoundary;
    auto parse = [](const std::string& body) { return mp::parse(body, kBoundary).error; };

    EXPECT_EQ(parse(""), mp::Error::NoOpeningDelimiter);
    EXPECT_EQ(parse("no delimiter here"), mp::Error::NoOpeningDelimiter);
    EXPECT_EQ(parse(d), mp::Error::Truncated);
    EXPECT_EQ(parse(d + "\r"), mp::Error::Truncated);
    EXPECT_EQ(parse(d + "X\r\n\r\nbody\r\n" + d + "--"), mp::Error::MalformedDelimiter);
    EXPECT_EQ(parse(d + "\r\nContent-Disposition: form-data\r\n\r\nno end"), mp::Error::Truncated);
    EXPECT_EQ(parse(d + "\r\nContent-Disposition: form-data"), mp::Error::Truncated);
    EXPECT_EQ(parse(d + "\r\nNoColonHere\r\n\r\nx\r\n" + d + "--"), mp::Error::MalformedHeader);
    EXPECT_EQ(parse(d + "\r\n: empty name\r\n\r\nx\r\n" + d + "--"), mp::Error::MalformedHeader);
    EXPECT_EQ(parse(d + "\r\nBad Name: v\r\n\r\nx\r\n" + d + "--"), mp::Error::MalformedHeader);
    EXPECT_EQ(parse(d + "\r\n folded-first\r\n\r\nx\r\n" + d + "--"), mp::Error::MalformedHeader);
    EXPECT_EQ(parse(d + "\r\n\r\nx\r\n" + d + "\r\n\r\ny\r\n" + d + "junk"), mp::Error::MalformedDelimiter);

    EXPECT_EQ(mp::parse("x", "").error, mp::Error::InvalidBoundary);
    EXPECT_EQ(mp::parse("x", std::string(71, 'b')).error, mp::Error::InvalidBoundary);
    EXPECT_EQ(mp::parse("x", "a\r\nb").error, mp::Error::InvalidBoundary);

    mp::Result r = mp::parse(d + "\r\n\r\nx\r\n" + d + "junk", kBoundary);
    EXPECT_FALSE(r);
    EXPECT_TRUE(r.parts.empty());  // no partial results on error
    EXPECT_STRNE(mp::errorMessage(r.error), "ok");
}

TEST(MultipartParseTest, LimitsAreEnforced)
{
    const std::string d = "--" + kBoundary;
    std::string many;
    for (int i = 0; i < 5; ++i)
    {
        many += field("f" + std::to_string(i), "v");
    }
    many += d + "--";
    mp::Limits limits;
    limits.maxParts = 5;
    EXPECT_TRUE(mp::parse(many, kBoundary, limits).ok());
    limits.maxParts = 4;
    EXPECT_EQ(mp::parse(many, kBoundary, limits).error, mp::Error::TooManyParts);

    limits = mp::Limits();
    limits.maxHeaderSize = 64;
    const std::string bigHeader = d + "\r\nX-Big: " + std::string(100, 'h') + "\r\n\r\nx\r\n" + d + "--";
    EXPECT_EQ(mp::parse(bigHeader, kBoundary, limits).error, mp::Error::HeaderTooLarge);
    const std::string unterminated = d + "\r\nX-Big: " + std::string(1000, 'h');
    EXPECT_EQ(mp::parse(unterminated, kBoundary, limits).error, mp::Error::HeaderTooLarge);

    limits = mp::Limits();
    limits.maxHeadersPerPart = 2;
    const std::string threeHeaders = d + "\r\nA: 1\r\nB: 2\r\nC: 3\r\n\r\nx\r\n" + d + "--";
    EXPECT_EQ(mp::parse(threeHeaders, kBoundary, limits).error, mp::Error::TooManyHeaders);
    limits.maxHeadersPerPart = 3;
    EXPECT_TRUE(mp::parse(threeHeaders, kBoundary, limits).ok());
}

TEST(MultipartParseTest, FromRequest)
{
    HttpRequest req;
    req.content = sampleBody();
    EXPECT_EQ(mp::parse(req).error, mp::Error::NotMultipart);
    req.headers["Content-Type"] = "application/json";
    EXPECT_EQ(mp::parse(req).error, mp::Error::NotMultipart);
    req.headers["Content-Type"] = "multipart/form-data";
    EXPECT_EQ(mp::parse(req).error, mp::Error::MissingBoundary);
    req.headers["Content-Type"] = "multipart/form-data; boundary=\"\"";
    EXPECT_EQ(mp::parse(req).error, mp::Error::InvalidBoundary);
    req.headers["Content-Type"] = "multipart/form-data; boundary=" + kBoundary;
    mp::Result r = mp::parse(req);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.parts.size(), 3u);
}

TEST(MultipartFuzzTest, RandomInputNeverCrashes)
{
    std::mt19937 rng(12345);
    const std::string base = sampleBody();
    const std::string alphabet = std::string("-\r\n:;=\" ab") + kBoundary.substr(0, 6);
    size_t accepted = 0;
    for (int iter = 0; iter < 20000; ++iter)
    {
        std::string input;
        const int mode = iter % 4;
        if (mode == 0)
        {
            // Random bytes
            input.resize(rng() % 300);
            for (char& c : input)
            {
                c = static_cast<char>(rng() & 0xFF);
            }
        }
        else if (mode == 1)
        {
            // Random tokens from an alphabet that often forms delimiters and headers
            const size_t n = rng() % 200;
            for (size_t i = 0; i < n; ++i)
            {
                if (rng() % 8 == 0)
                {
                    input += "--" + kBoundary;
                }
                else
                {
                    input += alphabet[rng() % alphabet.size()];
                }
            }
        }
        else
        {
            // Mutations of a valid body: flips, truncation, insertions, deletions
            input = base;
            const int edits = 1 + static_cast<int>(rng() % 6);
            for (int e = 0; e < edits && !input.empty(); ++e)
            {
                const size_t at = rng() % input.size();
                switch (rng() % 4)
                {
                case 0:
                    input[at] = static_cast<char>(rng() & 0xFF);
                    break;
                case 1:
                    input.resize(at);
                    break;
                case 2:
                    input.insert(at, 1, alphabet[rng() % alphabet.size()]);
                    break;
                default:
                    input.erase(at, 1 + rng() % 8);
                    break;
                }
            }
        }

        mp::Limits limits;
        limits.maxParts = 1 + rng() % 5;
        limits.maxHeaderSize = 16 + rng() % 200;
        mp::Result r = mp::parse(input, kBoundary, limits);
        if (r.ok())
        {
            ++accepted;
            EXPECT_LE(r.parts.size(), limits.maxParts);
            for (const auto& part : r.parts)
            {
                // Every view lies inside the input.
                ASSERT_GE(part.data.data(), input.data());
                ASSERT_LE(part.data.data() + part.data.size(), input.data() + input.size());
            }
        }
        else
        {
            EXPECT_TRUE(r.parts.empty());
        }
        (void)mp::boundaryFromContentType(input);
    }
    EXPECT_GT(accepted, 0u);  // some mutations remain valid
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
