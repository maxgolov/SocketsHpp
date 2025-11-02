// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <functional>
#include <memory>
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
         * @brief Compression strategy with user-provided implementation.
         * 
         * This allows pluggable compression where users bring their own
         * compression libraries (zlib, brotli, zstd, etc.).
         */
        class CompressionStrategy
        {
        public:
            std::string name;  // "gzip", "deflate", "br", "zstd", etc.
            CompressionCallback compress;
            DecompressionCallback decompress;

            CompressionStrategy(
                std::string strategyName,
                CompressionCallback compressFunc,
                DecompressionCallback decompressFunc)
                : name(std::move(strategyName))
                , compress(std::move(compressFunc))
                , decompress(std::move(decompressFunc))
            {
            }

            /**
             * @brief Compress data with the given level.
             * @param input Input data
             * @param level Compression level (1-9, where 9 is maximum)
             * @return Compressed data
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
             * @return Decompressed data
             */
            std::vector<uint8_t> decompressData(const std::vector<uint8_t>& input) const
            {
                if (decompress)
                {
                    return decompress(input);
                }
                return input; // No decompression
            }
        };

        /**
         * @brief Global registry for compression strategies.
         * 
         * Users register their compression implementations at startup,
         * then the HTTP server uses them based on Accept-Encoding headers.
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
             * @brief Register a compression strategy.
             * @param strategy Compression strategy to register
             */
            void registerStrategy(std::shared_ptr<CompressionStrategy> strategy)
            {
                m_strategies[strategy->name] = std::move(strategy);
            }

            /**
             * @brief Get a compression strategy by name.
             * @param name Compression algorithm name (e.g., "gzip", "br")
             * @return Strategy pointer or nullptr if not found
             */
            std::shared_ptr<CompressionStrategy> get(const std::string& name) const
            {
                auto it = m_strategies.find(name);
                return (it != m_strategies.end()) ? it->second : nullptr;
            }

            /**
             * @brief Check if a compression algorithm is supported.
             */
            bool isSupported(const std::string& name) const
            {
                return m_strategies.find(name) != m_strategies.end();
            }

            /**
             * @brief Get list of all supported compression algorithms.
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
            std::string encoding;
            float quality;

            EncodingPreference(std::string enc, float q)
                : encoding(std::move(enc)), quality(q)
            {
            }

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
         * @return List of encoding preferences sorted by quality
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
                        
                        if (qualityPart.size() >= 2 && qualityPart[0] == 'q' && qualityPart[1] == '=')
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
                        preferences.emplace_back(encoding, quality);
                    }
                }

                start = end + 1;
            }

            // Sort by quality (highest first)
            std::sort(preferences.begin(), preferences.end());

            return preferences;
        }

        /**
         * @brief Compression middleware for HTTP responses.
         * 
         * Automatically compresses responses based on:
         * - Client's Accept-Encoding header
         * - Response size threshold
         * - Content type (avoid compressing images, etc.)
         */
        class CompressionMiddleware
        {
        private:
            int m_compressionLevel;
            size_t m_minSizeToCompress;
            std::vector<std::string> m_compressibleTypes;
            std::vector<std::string> m_excludedTypes;

        public:
            CompressionMiddleware()
                : m_compressionLevel(6)
                , m_minSizeToCompress(1024)
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
             * @brief Set compression level (1-9).
             */
            void setLevel(int level)
            {
                m_compressionLevel = std::max(1, std::min(9, level));
            }

            /**
             * @brief Set minimum size to compress.
             * Small responses don't benefit from compression.
             */
            void setMinSize(size_t size)
            {
                m_minSizeToCompress = size;
            }

            /**
             * @brief Add a compressible content type.
             */
            void addCompressibleType(const std::string& contentType)
            {
                m_compressibleTypes.push_back(contentType);
            }

            /**
             * @brief Add an excluded content type.
             */
            void addExcludedType(const std::string& contentType)
            {
                m_excludedTypes.push_back(contentType);
            }

            /**
             * @brief Check if a content type should be compressed.
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
                    auto decompressed = strategy->decompressData(input);
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
