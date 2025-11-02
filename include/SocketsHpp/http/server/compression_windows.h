// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/http/server/compression.h>
#include <vector>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <compressapi.h>
#pragma comment(lib, "Cabinet.lib")

namespace SOCKETSHPP_NS::http::server::compression {

/**
 * @brief Windows Compression API implementation (MSZIP/LZMS).
 * 
 * Uses Windows built-in compression available since Windows 8/Server 2012.
 * Supports MSZIP (DEFLATE variant), XPRESS, XPRESS_HUFF, and LZMS.
 */
class WindowsCompression
{
public:
    enum Algorithm
    {
        MSZIP = COMPRESS_ALGORITHM_MSZIP,           // DEFLATE variant
        XPRESS = COMPRESS_ALGORITHM_XPRESS,         // LZ77-based
        XPRESS_HUFF = COMPRESS_ALGORITHM_XPRESS_HUFF, // LZ77 + Huffman
        LZMS = COMPRESS_ALGORITHM_LZMS              // LZMA-based
    };

    /**
     * @brief Compress data using Windows Compression API.
     */
    static std::vector<uint8_t> compress(
        const std::vector<uint8_t>& input,
        int level,
        Algorithm algorithm = MSZIP)
    {
        if (input.empty())
        {
            return {};
        }

        COMPRESSOR_HANDLE compressor = nullptr;
        
        // Create compressor
        if (!CreateCompressor(algorithm, nullptr, &compressor))
        {
            throw std::runtime_error("Failed to create Windows compressor");
        }

        // Query compressed size
        SIZE_T compressedSize = 0;
        BOOL result = Compress(
            compressor,
            input.data(),
            input.size(),
            nullptr,
            0,
            &compressedSize);

        if (!result && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            CloseCompressor(compressor);
            throw std::runtime_error("Failed to query compressed size");
        }

        // Allocate output buffer
        std::vector<uint8_t> output(compressedSize);

        // Compress
        result = Compress(
            compressor,
            input.data(),
            input.size(),
            output.data(),
            output.size(),
            &compressedSize);

        CloseCompressor(compressor);

        if (!result)
        {
            throw std::runtime_error("Windows compression failed");
        }

        output.resize(compressedSize);
        return output;
    }

    /**
     * @brief Decompress data using Windows Compression API.
     */
    static std::vector<uint8_t> decompress(
        const std::vector<uint8_t>& input,
        Algorithm algorithm = MSZIP)
    {
        if (input.empty())
        {
            return {};
        }

        DECOMPRESSOR_HANDLE decompressor = nullptr;

        // Create decompressor
        if (!CreateDecompressor(algorithm, nullptr, &decompressor))
        {
            throw std::runtime_error("Failed to create Windows decompressor");
        }

        // Query decompressed size
        SIZE_T decompressedSize = 0;
        BOOL result = Decompress(
            decompressor,
            input.data(),
            input.size(),
            nullptr,
            0,
            &decompressedSize);

        if (!result && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            CloseDecompressor(decompressor);
            throw std::runtime_error("Failed to query decompressed size");
        }

        // Allocate output buffer
        std::vector<uint8_t> output(decompressedSize);

        // Decompress
        result = Decompress(
            decompressor,
            input.data(),
            input.size(),
            output.data(),
            output.size(),
            &decompressedSize);

        CloseDecompressor(decompressor);

        if (!result)
        {
            throw std::runtime_error("Windows decompression failed");
        }

        output.resize(decompressedSize);
        return output;
    }
};

/**
 * @brief Register Windows compression strategies with the registry.
 */
inline void registerWindowsCompression()
{
    using namespace SOCKETSHPP_NS::http::server;

    // MSZIP (similar to gzip/deflate)
    auto mszipStrategy = std::make_shared<CompressionStrategy>(
        "mszip",
        [](const std::vector<uint8_t>& input, int level) {
            return WindowsCompression::compress(input, level, WindowsCompression::MSZIP);
        },
        [](const std::vector<uint8_t>& input) {
            return WindowsCompression::decompress(input, WindowsCompression::MSZIP);
        }
    );
    CompressionRegistry::instance().registerStrategy(mszipStrategy);

    // XPRESS
    auto xpressStrategy = std::make_shared<CompressionStrategy>(
        "xpress",
        [](const std::vector<uint8_t>& input, int level) {
            return WindowsCompression::compress(input, level, WindowsCompression::XPRESS);
        },
        [](const std::vector<uint8_t>& input) {
            return WindowsCompression::decompress(input, WindowsCompression::XPRESS);
        }
    );
    CompressionRegistry::instance().registerStrategy(xpressStrategy);

    // LZMS
    auto lzmsStrategy = std::make_shared<CompressionStrategy>(
        "lzms",
        [](const std::vector<uint8_t>& input, int level) {
            return WindowsCompression::compress(input, level, WindowsCompression::LZMS);
        },
        [](const std::vector<uint8_t>& input) {
            return WindowsCompression::decompress(input, WindowsCompression::LZMS);
        }
    );
    CompressionRegistry::instance().registerStrategy(lzmsStrategy);
}

} // namespace SOCKETSHPP_NS::http::server::compression

#endif // _WIN32
