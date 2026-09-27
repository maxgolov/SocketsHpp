// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/http/server/compression.h>
#include <SocketsHpp/config.h>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <compressapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "Cabinet.lib")
#endif

namespace SOCKETSHPP_NS::http::server::compression {

namespace detail {
/// The Windows SDK declares Compress()/Decompress() input as LPCVOID while
/// MinGW declares PVOID; the API never writes through it, so drop const.
inline void* win_input(const void* p) noexcept { return const_cast<void*>(p); }
}  // namespace detail

/**
 * @brief Windows Compression API implementation (MSZIP/XPRESS/XPRESS_HUFF/LZMS).
 * 
 * Uses Windows built-in compression (Cabinet.dll) available since Windows
 * 8/Server 2012. Only available when _WIN32 is defined.
 * @note The output uses the Compression API's own buffered format; it is not
 *       interchangeable with gzip/deflate/br HTTP content-codings.
 */
class WindowsCompression
{
public:
    /// @brief Compression API algorithm.
    enum Algorithm
    {
        MSZIP = COMPRESS_ALGORITHM_MSZIP,           ///< DEFLATE variant
        XPRESS = COMPRESS_ALGORITHM_XPRESS,         ///< LZ77-based
        XPRESS_HUFF = COMPRESS_ALGORITHM_XPRESS_HUFF, ///< LZ77 + Huffman
        LZMS = COMPRESS_ALGORITHM_LZMS              ///< LZMA-based
    };

    /**
     * @brief Compress data using Windows Compression API.
     * @param input Data to compress
     * @param level Ignored (the API has no level setting)
     * @param algorithm Algorithm to use
     * @return Compressed data; empty for empty input
     * @throws std::runtime_error if the API fails
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
            detail::win_input(input.data()),
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
            detail::win_input(input.data()),
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
     * @param input Compressed data (from compress() with the same algorithm)
     * @param algorithm Algorithm the data was compressed with
     * @param maxOutputSize Maximum decompressed size; checked before allocating
     * @return Decompressed data; empty for empty input
     * @throws std::length_error if the output would exceed @p maxOutputSize
     * @throws std::runtime_error if the API fails
     */
    static std::vector<uint8_t> decompress(
        const std::vector<uint8_t>& input,
        Algorithm algorithm = MSZIP,
        size_t maxOutputSize = SOCKETSHPP_NS::config::MAX_HTTP_BODY_SIZE)
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
            detail::win_input(input.data()),
            input.size(),
            nullptr,
            0,
            &decompressedSize);

        if (!result && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            CloseDecompressor(decompressor);
            throw std::runtime_error("Failed to query decompressed size");
        }

        // The size comes from the (untrusted) compressed stream: never allocate
        // more than the caller allows.
        if (decompressedSize > maxOutputSize)
        {
            CloseDecompressor(decompressor);
            throw std::length_error("Windows decompression: output exceeds size limit");
        }

        // Allocate output buffer
        std::vector<uint8_t> output(decompressedSize);

        // Decompress
        result = Decompress(
            decompressor,
            detail::win_input(input.data()),
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
 * @brief Register Windows compression strategies with the registry: "mszip",
 *        "xpress" and "lzms", each with a bounded decompressor.
 * @note These names are not standard HTTP content-codings, so browsers will not
 *       request them. Not thread-safe (see CompressionRegistry).
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
    mszipStrategy->decompressBounded = [](const std::vector<uint8_t>& input, size_t maxOutputSize) {
        return WindowsCompression::decompress(input, WindowsCompression::MSZIP, maxOutputSize);
    };
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
    xpressStrategy->decompressBounded = [](const std::vector<uint8_t>& input, size_t maxOutputSize) {
        return WindowsCompression::decompress(input, WindowsCompression::XPRESS, maxOutputSize);
    };
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
    lzmsStrategy->decompressBounded = [](const std::vector<uint8_t>& input, size_t maxOutputSize) {
        return WindowsCompression::decompress(input, WindowsCompression::LZMS, maxOutputSize);
    };
    CompressionRegistry::instance().registerStrategy(lzmsStrategy);
}

} // namespace SOCKETSHPP_NS::http::server::compression

#endif // _WIN32
