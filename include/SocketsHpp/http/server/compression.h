// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace server
    {
        /**
         * @brief Compression callback function type.
         * Takes input data and compression level, returns compressed data.
         */
        using CompressionCallback = std::function<std::vector<uint8_t>(
            const std::vector<uint8_t>& input, int level)>;

        /**
         * @brief Decompression callback function type.
         * Takes compressed data, returns decompressed data.
         */
        using DecompressionCallback = std::function<std::vector<uint8_t>(
            const std::vector<uint8_t>& input)>;

        /**
         * @brief Size-bounded decompression callback type.
         * Takes compressed data and the maximum number of bytes the output may
         * have; must throw (e.g. std::length_error) instead of producing more.
         * Preferred over DecompressionCallback: it stops a "decompression bomb"
         * before the memory is allocated rather than after.
         */
        using BoundedDecompressionCallback = std::function<std::vector<uint8_t>(
            const std::vector<uint8_t>& input, size_t maxOutputSize)>;

        /// @brief Lower-case an encoding token (content-codings are case-insensitive).
        inline std::string normalizeEncodingName(std::string name)
        {
            for (char& c : name)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return name;
        }

        /**
         * @brief Compression strategy with user-provided implementation.
         * 
         * This allows pluggable compression where users bring their own
         * compression libraries (zlib, brotli, zstd, etc.).
         * @note The callbacks may be invoked concurrently and must be thread-safe.
         */
        class CompressionStrategy
        {
        public:
            std::string name;  ///< Content-coding token, lower case: "gzip", "deflate", "br", "zstd", etc.
            CompressionCallback compress;      ///< Compressor; if empty, compressData() returns the input.
            DecompressionCallback decompress;  ///< Decompressor; if empty, decompressData() returns the input.
            BoundedDecompressionCallback decompressBounded;  ///< Optional size-bounded decompressor, preferred when set.

            /// @brief Create a strategy; @p strategyName is lower-cased.
            /// @param strategyName Content-coding token (e.g. "gzip").
            /// @param compressFunc Compressor (may be empty).
            /// @param decompressFunc Decompressor (may be empty).
            CompressionStrategy(
                std::string strategyName,
                CompressionCallback compressFunc,
                DecompressionCallback decompressFunc)
                : name(normalizeEncodingName(std::move(strategyName)))
                , compress(std::move(compressFunc))
                , decompress(std::move(decompressFunc))
            {
            }

            /**
             * @brief Compress data with the given level.
             * @param input Input data
             * @param level Compression level (1-9, where 9 is maximum); passed to the
             *        callback, which may ignore it
             * @return Compressed data, or @p input unchanged if no compressor is set
             */
            std::vector<uint8_t> compressData(const std::vector<uint8_t>& input, int level) const
            {
                if (compress)
                {
                    return compress(input, level);
                }
                return input; // No compression
            }

            /**
             * @brief Decompress data.
             * @param input Compressed data
             * @return Decompressed data, or @p input unchanged if no decompressor is set
             * @warning Unbounded: prefer the overload taking maxOutputSize for
             *          untrusted input.
             */
            std::vector<uint8_t> decompressData(const std::vector<uint8_t>& input) const
            {
                if (decompress)
                {
                    return decompress(input);
                }
                return input; // No decompression
            }

            /**
             * @brief Decompress data, refusing output larger than @p maxOutputSize.
             * @param input Compressed data
             * @param maxOutputSize Maximum decompressed size in bytes
             * @return Decompressed data
             * @throws std::length_error if the output would exceed the limit; anything
             *         the callback throws is propagated
             * @note Uses decompressBounded when available; otherwise the whole
             *       output is produced first and then checked.
             */
            std::vector<uint8_t> decompressData(const std::vector<uint8_t>& input, size_t maxOutputSize) const
            {
                std::vector<uint8_t> output;
                if (decompressBounded)
                {
                    output = decompressBounded(input, maxOutputSize);
                }
                else
                {
                    output = decompressData(input);
                }
                if (output.size() > maxOutputSize)
                {
                    throw std::length_error("Decompressed data exceeds size limit");
                }
                return output;
            }
        };

        /**
         * @brief Global registry for compression strategies, keyed by lower-case name.
         * 
         * Users register their compression implementations at startup;
         * CompressionMiddleware looks them up by Accept-Encoding / Content-Encoding
         * token. HttpServer itself does not compress or decompress anything.
         * @note Not synchronized: register strategies before any concurrent use.
         */
        class CompressionRegistry
        {
        private:
            std::unordered_map<std::string, std::shared_ptr<CompressionStrategy>> m_strategies;

            CompressionRegistry() = default;

        public:
            /**
             * @brief Get the singleton instance.
             */
            static CompressionRegistry& instance()
            {
                static CompressionRegistry registry;
                return registry;
            }

            /**
             * @brief Register a compression strategy, replacing any with the same name.
             * @param strategy Compression strategy to register (must not be null); its
             *        name is lower-cased
             */
            void registerStrategy(std::shared_ptr<CompressionStrategy> strategy)
            {
                strategy->name = normalizeEncodingName(strategy->name);
                m_strategies[strategy->name] = std::move(strategy);
            }

            /**
             * @brief Get a compression strategy by name.
             * @param name Compression algorithm name (e.g., "gzip", "br"); case-insensitive
             * @return Strategy pointer or nullptr if not found
             */
            std::shared_ptr<CompressionStrategy> get(const std::string& name) const
            {
                auto it = m_strategies.find(normalizeEncodingName(name));
                return (it != m_strategies.end()) ? it->second : nullptr;
            }

            /**
             * @brief Check if a compression algorithm is registered (case-insensitive).
             */
            bool isSupported(const std::string& name) const
            {
                return m_strategies.find(normalizeEncodingName(name)) != m_strategies.end();
            }

            /**
             * @brief Get the names of all registered strategies (unspecified order).
             */
            std::vector<std::string> supportedEncodings() const
            {
                std::vector<std::string> encodings;
                encodings.reserve(m_strategies.size());
                for (const auto& [name, _] : m_strategies)
                {
                    encodings.push_back(name);
                }
                return encodings;
            }

            /**
             * @brief Clear all registered strategies (useful for testing).
             */
            void clear()
            {
                m_strategies.clear();
            }
        };

        /**
         * @brief Encoding preference from Accept-Encoding header.
         */
        struct EncodingPreference
        {
            std::string encoding;  ///< Lower-case content-coding token (may be "*").
            float quality;         ///< q-value; 1.0 when absent.

            /// @brief Construct from a token and q-value.
            EncodingPreference(std::string enc, float q)
                : encoding(std::move(enc)), quality(q)
            {
            }

            /// @brief Orders by descending quality, so sorting puts the most preferred first.
            bool operator<(const EncodingPreference& other) const
            {
                return quality > other.quality; // Higher quality first
            }
        };

        /**
         * @brief Parse Accept-Encoding header.
         * 
         * Parses headers like:
         *   "gzip, deflate, br"
         *   "gzip;q=1.0, br;q=0.8, *;q=0.1"
         * 
         * @param header Accept-Encoding header value
         * @return Encoding preferences sorted by descending quality (the order of
         *         equal-quality entries is unspecified). Tokens are lower-cased;
         *         entries with q=0 are dropped; a missing or unparsable q counts as
         *         1.0; "*" is kept as a literal token and not expanded.
         */
        inline std::vector<EncodingPreference> parseAcceptEncoding(const std::string& header)
        {
            std::vector<EncodingPreference> preferences;

            size_t start = 0;
            while (start < header.length())
            {
                // Find next comma
                auto end = header.find(',', start);
                if (end == std::string::npos)
                {
                    end = header.length();
                }

                auto item = header.substr(start, end - start);

                // Trim whitespace
                item.erase(0, item.find_first_not_of(" \t"));
                item.erase(item.find_last_not_of(" \t") + 1);

                if (!item.empty())
                {
                    float quality = 1.0f;
                    std::string encoding = item;

                    // Extract quality value if present (handle whitespace around ; and q=)
                    auto qpos = item.find(';');
                    if (qpos != std::string::npos)
                    {
                        encoding = item.substr(0, qpos);
                        
                        // Find the q= part after semicolon
                        std::string qualityPart = item.substr(qpos + 1);
                        // Trim whitespace
                        qualityPart.erase(0, qualityPart.find_first_not_of(" \t"));
                        
                        if (qualityPart.size() >= 2 && (qualityPart[0] == 'q' || qualityPart[0] == 'Q') && qualityPart[1] == '=')
                        {
                            try
                            {
                                std::string qvalue = qualityPart.substr(2);
                                // Trim whitespace from quality value
                                qvalue.erase(0, qvalue.find_first_not_of(" \t"));
                                qvalue.erase(qvalue.find_last_not_of(" \t") + 1);
                                quality = std::stof(qvalue);
                            }
                            catch (...)
                            {
                                quality = 1.0f; // Default if parse fails
                            }
                        }
                    }

                    // Trim encoding name
                    encoding.erase(0, encoding.find_first_not_of(" \t"));
                    encoding.erase(encoding.find_last_not_of(" \t") + 1);

                    if (!encoding.empty() && quality > 0.0f)
                    {
                        preferences.emplace_back(normalizeEncodingName(encoding), quality);
                    }
                }

                start = end + 1;
            }

            // Sort by quality (highest first)
            std::sort(preferences.begin(), preferences.end());

            return preferences;
        }

        /**
         * @brief Compression helper for HTTP message bodies.
         * 
         * compressResponse() compresses a response body based on:
         * - Client's Accept-Encoding header
         * - Response size threshold
         * - Content type (avoid compressing images, etc.)
         *
         * decompressRequest() decodes a request body with a size limit. Neither is
         * invoked by HttpServer automatically: call them from your handlers and set
         * Content-Encoding (and Vary) yourself. Strategies come from
         * CompressionRegistry.
         * @note Configure before use; compressResponse()/decompressRequest() only
         *       read the settings.
         */
        class CompressionMiddleware
        {
        private:
            int m_compressionLevel;
            size_t m_minSizeToCompress;
            size_t m_maxDecompressedSize;
            std::vector<std::string> m_compressibleTypes;
            std::vector<std::string> m_excludedTypes;

        public:
            /// @brief Defaults: level 6, minimum size 1024 bytes, decompression limit
            ///        config::MAX_HTTP_BODY_SIZE, common text/JSON/XML/JavaScript types
            ///        compressible, and already-compressed image/video/audio/archive types
            ///        excluded.
            CompressionMiddleware()
                : m_compressionLevel(6)
                , m_minSizeToCompress(1024)
                , m_maxDecompressedSize(config::MAX_HTTP_BODY_SIZE)
            {
                // Default compressible types
                m_compressibleTypes = {
                    "text/html",
                    "text/plain",
                    "text/css",
                    "text/javascript",
                    "application/javascript",
                    "application/json",
                    "application/xml",
                    "text/xml",
                    "application/x-javascript"
                };

                // Default excluded types (already compressed)
                m_excludedTypes = {
                    "image/jpeg",
                    "image/png",
                    "image/gif",
                    "image/webp",
                    "video/",
                    "audio/",
                    "application/zip",
                    "application/gzip",
                    "application/x-gzip"
                };
            }

            /**
             * @brief Set compression level; values outside 1-9 are clamped.
             */
            void setLevel(int level)
            {
                m_compressionLevel = std::max(1, std::min(9, level));
            }

            /**
             * @brief Set minimum body size (bytes) to compress (default 1024).
             * Small responses don't benefit from compression.
             */
            void setMinSize(size_t size)
            {
                m_minSizeToCompress = size;
            }

            /**
             * @brief Set the maximum size of a decompressed request body.
             * Defaults to config::MAX_HTTP_BODY_SIZE; guards against decompression bombs.
             */
            void setMaxDecompressedSize(size_t size)
            {
                m_maxDecompressedSize = size;
            }

            /// @brief Get the maximum size of a decompressed request body.
            size_t getMaxDecompressedSize() const
            {
                return m_maxDecompressedSize;
            }

            /**
             * @brief Add a compressible content type (matched as a substring of the
             *        Content-Type value).
             */
            void addCompressibleType(const std::string& contentType)
            {
                m_compressibleTypes.push_back(contentType);
            }

            /**
             * @brief Add an excluded content type (substring match, e.g. "video/");
             *        exclusions take precedence over compressible types.
             */
            void addExcludedType(const std::string& contentType)
            {
                m_excludedTypes.push_back(contentType);
            }

            /**
             * @brief Check if a body should be compressed.
             * @param contentType Content-Type value (case-sensitive substring matching)
             * @param size Body size in bytes
             * @return true if @p size is at least the minimum size, @p contentType
             *         contains no excluded type, and it contains a compressible type.
             */
            bool shouldCompress(const std::string& contentType, size_t size) const
            {
                // Too small?
                if (size < m_minSizeToCompress)
                {
                    return false;
                }

                // Check excluded types first
                for (const auto& excluded : m_excludedTypes)
                {
                    if (contentType.find(excluded) != std::string::npos)
                    {
                        return false;
                    }
                }

                // Check if it's in compressible list
                for (const auto& compressible : m_compressibleTypes)
                {
                    if (contentType.find(compressible) != std::string::npos)
                    {
                        return true;
                    }
                }

                return false;
            }

            /**
             * @brief Compress response if applicable.
             * 
             * Tries the client's accepted encodings in preference order and uses the
             * first registered strategy that succeeds and produces a smaller body.
             * Compressor exceptions are swallowed (the next encoding is tried).
             * Headers are not modified: set Content-Encoding to @p outEncoding.
             * @param acceptEncoding Accept-Encoding header value
             * @param contentType Response Content-Type
             * @param body Response body (modified in place if compressed)
             * @param outEncoding Output parameter: encoding used (if any)
             * @return true if compression was applied
             */
            bool compressResponse(
                const std::string& acceptEncoding,
                const std::string& contentType,
                std::string& body,
                std::string& outEncoding)
            {
                outEncoding.clear();

                // Don't compress if not needed
                if (!shouldCompress(contentType, body.size()))
                {
                    return false;
                }

                // Parse Accept-Encoding
                auto preferences = parseAcceptEncoding(acceptEncoding);
                if (preferences.empty())
                {
                    return false;
                }

                auto& registry = CompressionRegistry::instance();

                // Try each encoding in preference order
                for (const auto& pref : preferences)
                {
                    if (pref.quality <= 0.0f)
                    {
                        continue; // Quality 0 means "don't use"
                    }

                    auto strategy = registry.get(pref.encoding);
                    if (!strategy)
                    {
                        continue; // Not supported
                    }

                    try
                    {
                        // Compress
                        std::vector<uint8_t> input(body.begin(), body.end());
                        auto compressed = strategy->compressData(input, m_compressionLevel);

                        // Only use if actually smaller
                        if (compressed.size() < body.size())
                        {
                            body.assign(compressed.begin(), compressed.end());
                            outEncoding = strategy->name;
                            return true;
                        }
                    }
                    catch (...)
                    {
                        // Compression failed, try next strategy
                        continue;
                    }
                }

                return false;
            }

            /**
             * @brief Decompress request body.
             * @param contentEncoding Content-Encoding header value; a single coding
             *        (lists such as "gzip, br" are not supported)
             * @param body Request body, replaced by the decoded data on success
             * @return true if the body was decompressed; false if it is not
             *         compressed, the encoding is unknown, decoding failed, or the
             *         output would exceed getMaxDecompressedSize() (body unchanged).
             */
            bool decompressRequest(
                const std::string& contentEncoding,
                std::string& body)
            {
                if (contentEncoding.empty())
                {
                    return false; // Not compressed
                }

                auto& registry = CompressionRegistry::instance();
                auto strategy = registry.get(contentEncoding);
                
                if (!strategy)
                {
                    return false; // Unknown encoding
                }

                try
                {
                    std::vector<uint8_t> input(body.begin(), body.end());
                    auto decompressed = strategy->decompressData(input, m_maxDecompressedSize);
                    body.assign(decompressed.begin(), decompressed.end());
                    return true;
                }
                catch (...)
                {
                    return false; // Decompression failed
                }
            }
        };

    } // namespace server
} // namespace http
SOCKETSHPP_NS_END
