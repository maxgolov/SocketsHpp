// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <stdexcept>

namespace SocketsHpp {
namespace utils {

/**
 * @brief Base64 encoding and decoding utilities
 * 
 * Provides RFC 4648 compliant base64 encoding and decoding.
 * All functions are header-only and have no external dependencies.
 */
class Base64
{
private:
    static constexpr const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    static inline bool is_base64(unsigned char c)
    {
        return (isalnum(c) || (c == '+') || (c == '/'));
    }

public:
    /**
     * @brief Encode binary data to base64 string
     * 
     * @param data Pointer to binary data to encode
     * @param len Length of data in bytes
     * @return std::string Base64-encoded string
     * 
     * Example:
     * @code
     * const char* data = "Hello, World!";
     * std::string encoded = Base64::encode(reinterpret_cast<const unsigned char*>(data), strlen(data));
     * // encoded = "SGVsbG8sIFdvcmxkIQ=="
     * @endcode
     */
    static std::string encode(const unsigned char* data, size_t len)
    {
        std::string ret;
        int i = 0;
        int j = 0;
        unsigned char char_array_3[3];
        unsigned char char_array_4[4];

        while (len--)
        {
            char_array_3[i++] = *(data++);
            if (i == 3)
            {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for (i = 0; i < 4; i++)
                    ret += base64_chars[char_array_4[i]];
                i = 0;
            }
        }

        if (i)
        {
            for (j = i; j < 3; j++)
                char_array_3[j] = '\0';

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

            for (j = 0; (j < i + 1); j++)
                ret += base64_chars[char_array_4[j]];

            while ((i++ < 3))
                ret += '=';
        }

        return ret;
    }

    /**
     * @brief Encode a string to base64
     * 
     * @param data String containing binary data to encode
     * @return std::string Base64-encoded string
     * 
     * Example:
     * @code
     * std::string encoded = Base64::encode("Hello, World!");
     * // encoded = "SGVsbG8sIFdvcmxkIQ=="
     * @endcode
     */
    static std::string encode(const std::string& data)
    {
        return encode(reinterpret_cast<const unsigned char*>(data.c_str()), data.length());
    }

    /**
     * @brief Decode base64 string to binary data
     * 
     * @param encoded_string Base64-encoded string to decode
     * @return std::string Decoded binary data as string
     * @throws std::invalid_argument if input contains invalid base64 characters
     * 
     * Example:
     * @code
     * std::string decoded = Base64::decode("SGVsbG8sIFdvcmxkIQ==");
     * // decoded = "Hello, World!"
     * @endcode
     */
    static std::string decode(const std::string& encoded_string)
    {
        size_t in_len = encoded_string.size();
        int i = 0;
        int j = 0;
        int in_ = 0;
        unsigned char char_array_4[4], char_array_3[3];
        std::string ret;

        while (in_len-- && (encoded_string[in_] != '='))
        {
            // Check for invalid characters
            if (!is_base64(encoded_string[in_]))
            {
                throw std::invalid_argument("Invalid base64 character");
            }
            
            char_array_4[i++] = encoded_string[in_];
            in_++;
            if (i == 4)
            {
                for (i = 0; i < 4; i++)
                {
                    // Find position in base64_chars
                    const char* pos = base64_chars;
                    int idx = 0;
                    while (*pos && *pos != char_array_4[i])
                    {
                        pos++;
                        idx++;
                    }
                    if (!*pos)
                    {
                        throw std::invalid_argument("Invalid base64 character");
                    }
                    char_array_4[i] = static_cast<unsigned char>(idx);
                }

                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

                for (i = 0; (i < 3); i++)
                    ret += char_array_3[i];
                i = 0;
            }
        }

        if (i)
        {
            for (j = 0; j < i; j++)
            {
                // Find position in base64_chars
                const char* pos = base64_chars;
                int idx = 0;
                while (*pos && *pos != char_array_4[j])
                {
                    pos++;
                    idx++;
                }
                if (!*pos)
                {
                    throw std::invalid_argument("Invalid base64 character");
                }
                char_array_4[j] = static_cast<unsigned char>(idx);
            }

            for (j = i; j < 4; j++)
                char_array_4[j] = 0;

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (j = 0; (j < i - 1); j++)
                ret += char_array_3[j];
        }

        return ret;
    }

    /**
     * @brief Validate if a string is valid base64
     * 
     * @param str String to validate
     * @return true if string is valid base64, false otherwise
     */
    static bool is_valid(const std::string& str)
    {
        if (str.empty())
            return true;

        // Check if length is multiple of 4 (with padding)
        size_t len = str.length();
        
        // Count padding
        size_t padding = 0;
        if (len > 0 && str[len - 1] == '=')
            padding++;
        if (len > 1 && str[len - 2] == '=')
            padding++;

        // Check all non-padding characters
        for (size_t i = 0; i < len - padding; i++)
        {
            if (!is_base64(static_cast<unsigned char>(str[i])))
                return false;
        }

        return true;
    }
};

// Convenience functions with alternative naming
namespace base64 {

/**
 * @brief Encode binary data to base64 string
 * @param data Pointer to binary data
 * @param len Length of data in bytes
 * @return Base64-encoded string
 */
inline std::string encode(const unsigned char* data, size_t len)
{
    return Base64::encode(data, len);
}

/**
 * @brief Encode string to base64
 * @param data String to encode
 * @return Base64-encoded string
 */
inline std::string encode(const std::string& data)
{
    return Base64::encode(data);
}

/**
 * @brief Decode base64 string
 * @param encoded_string Base64-encoded string
 * @return Decoded binary data as string
 */
inline std::string decode(const std::string& encoded_string)
{
    return Base64::decode(encoded_string);
}

/**
 * @brief Validate base64 string
 * @param str String to validate
 * @return true if valid base64
 */
inline bool is_valid(const std::string& str)
{
    return Base64::is_valid(str);
}

} // namespace base64

} // namespace utils
} // namespace SocketsHpp
