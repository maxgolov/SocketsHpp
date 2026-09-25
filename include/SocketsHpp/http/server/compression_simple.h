// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/http/server/compression.h>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace SOCKETSHPP_NS::http::server::compression {

/**
 * @brief Simple Run-Length Encoding compression for testing.
 * 
 * This is NOT a production-quality compression algorithm!
 * It's only used for testing the compression framework without
 * requiring external libraries like zlib.
 * 
 * RLE Format: [count][byte][count][byte]...
 * Where count is 1 byte (max 255 repetitions)
 */
class SimpleRLE
{
public:
    /**
     * @brief Compress using Run-Length Encoding.
     */
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input, int /*level*/)
    {
        if (input.empty())
        {
            return {};
        }

        std::vector<uint8_t> output;
        output.reserve(input.size()); // Worst case: doubles in size

        size_t i = 0;
        while (i < input.size())
        {
            uint8_t current = input[i];
            uint8_t count = 1;

            // Count repetitions (max 255)
            while (i + count < input.size() && 
                   input[i + count] == current && 
                   count < 255)
            {
                count++;
            }

            // Write count and byte
            output.push_back(count);
            output.push_back(current);

            i += count;
        }

        return output;
    }

    /**
     * @brief Decompress RLE data.
     */
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input)
    {
        return decompress(input, (std::numeric_limits<size_t>::max)());
    }

    /**
     * @brief Decompress RLE data, refusing to produce more than maxOutputSize bytes.
     * @throws std::length_error if the output would exceed maxOutputSize
     */
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input, size_t maxOutputSize)
    {
        if (input.empty() || input.size() % 2 != 0)
        {
            throw std::runtime_error("Invalid RLE compressed data");
        }

        std::vector<uint8_t> output;
        output.reserve((std::min)(input.size() * 2, maxOutputSize)); // Estimate

        for (size_t i = 0; i + 1 < input.size(); i += 2)
        {
            uint8_t count = input[i];
            uint8_t value = input[i + 1];

            if (count > maxOutputSize - output.size())
            {
                throw std::length_error("RLE output exceeds size limit");
            }

            for (uint8_t j = 0; j < count; j++)
            {
                output.push_back(value);
            }
        }

        return output;
    }
};

/**
 * @brief Mock compression that just copies data (for testing).
 */
class IdentityCompression
{
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input, int /*level*/)
    {
        return input; // No compression
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input)
    {
        return input; // No decompression
    }
};

/**
 * @brief Register simple compression strategies for testing.
 */
inline void registerSimpleCompression()
{
    using namespace SOCKETSHPP_NS::http::server;

    // Simple RLE
    auto rleStrategy = std::make_shared<CompressionStrategy>(
        "rle",
        [](const std::vector<uint8_t>& input, int level) {
            return SimpleRLE::compress(input, level);
        },
        [](const std::vector<uint8_t>& input) {
            return SimpleRLE::decompress(input);
        }
    );
    rleStrategy->decompressBounded = [](const std::vector<uint8_t>& input, size_t maxOutputSize) {
        return SimpleRLE::decompress(input, maxOutputSize);
    };
    CompressionRegistry::instance().registerStrategy(rleStrategy);

    // Identity (no compression) - useful for testing
    auto identityStrategy = std::make_shared<CompressionStrategy>(
        "identity",
        [](const std::vector<uint8_t>& input, int level) {
            return IdentityCompression::compress(input, level);
        },
        [](const std::vector<uint8_t>& input) {
            return IdentityCompression::decompress(input);
        }
    );
    CompressionRegistry::instance().registerStrategy(identityStrategy);
}

} // namespace SOCKETSHPP_NS::http::server::compression
