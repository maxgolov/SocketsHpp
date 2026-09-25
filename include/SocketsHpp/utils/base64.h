// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace SocketsHpp {
namespace utils {

/**
 * @brief Base64 encoding and decoding utilities
 * 
 * Provides RFC 4648 (section 4) base64 encoding and decoding with the standard
 * alphabet and '=' padding (no URL-safe variant, no line wrapping).
 * All functions are header-only, stateless (safe to call from any thread) and
 * have no external dependencies.
 */
class Base64
{
private:
    static constexpr const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    /// Map a base64 alphabet character to its 6-bit value, or -1 if it is not in
    /// the standard alphabet (locale independent; '=' is not an alphabet char).
    static inline int decode_char(unsigned char c) noexcept
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    static inline bool is_base64(unsigned char c) noexcept
    {
        return decode_char(c) >= 0;
    }

    /// Strict validation of standard, padded base64 (RFC 4648 section 4).
    /// On failure returns false and sets `error`.
    static bool validate(const std::string& s, const char*& error) noexcept
    {
        const size_t len = s.size();
        if (len % 4 != 0)
        {
            error = "Invalid base64 length (must be a multiple of 4)";
            return false;
        }
        for (size_t i = 0; i < len; i++)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '=')
            {
                // Padding is only allowed as the last one or two characters of the input.
                const size_t pad = len - i;
                if (pad > 2 || (pad == 2 && s[len - 1] != '='))
                {
                    error = "Invalid base64 padding";
                    return false;
                }
                // Canonical encoding: unused bits before the padding must be zero.
                const int last = decode_char(static_cast<unsigned char>(s[i - 1]));
                if ((pad == 2 && (last & 0x0f) != 0) || (pad == 1 && (last & 0x03) != 0))
                {
                    error = "Invalid base64 padding (non-zero trailing bits)";
                    return false;
                }
                return true;
            }
            if (!is_base64(c))
            {
                error = "Invalid base64 character";
                return false;
            }
        }
        return true;
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
     * @throws std::invalid_argument if the input is not strictly valid, padded
     *         standard base64: length must be a multiple of 4, only the standard
     *         alphabet is allowed, '=' may appear only as the final one or two
     *         characters, and the unused bits before padding must be zero.
     *         Whitespace is not accepted.
     * 
     * Example:
     * @code
     * std::string decoded = Base64::decode("SGVsbG8sIFdvcmxkIQ==");
     * // decoded = "Hello, World!"
     * @endcode
     */
    static std::string decode(const std::string& encoded_string)
    {
        const char* error = nullptr;
        if (!validate(encoded_string, error))
        {
            throw std::invalid_argument(error);
        }

        std::string ret;
        ret.reserve(encoded_string.size() / 4 * 3);
        for (size_t i = 0; i < encoded_string.size(); i += 4)
        {
            int v[4];
            int valid = 0;
            for (int k = 0; k < 4; k++)
            {
                const unsigned char c = static_cast<unsigned char>(encoded_string[i + static_cast<size_t>(k)]);
                if (c == '=')
                {
                    v[k] = 0;
                }
                else
                {
                    v[k] = decode_char(c);
                    valid++;
                }
            }
            const unsigned int triple = (static_cast<unsigned int>(v[0]) << 18) |
                                        (static_cast<unsigned int>(v[1]) << 12) |
                                        (static_cast<unsigned int>(v[2]) << 6) |
                                        static_cast<unsigned int>(v[3]);
            ret += static_cast<char>((triple >> 16) & 0xff);
            if (valid > 2)
                ret += static_cast<char>((triple >> 8) & 0xff);
            if (valid > 3)
                ret += static_cast<char>(triple & 0xff);
        }
        return ret;
    }

    /**
     * @brief Validate if a string is valid base64
     *
     * Uses the same strict rules as decode(): is_valid(s) is true exactly when
     * decode(s) does not throw.
     * 
     * @param str String to validate
     * @return true if string is valid base64, false otherwise
     */
    static bool is_valid(const std::string& str)
    {
        const char* error = nullptr;
        return validate(str, error);
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
 * @brief Decode base64 string (strict; see Base64::decode())
 * @param encoded_string Base64-encoded string
 * @return Decoded binary data as string
 * @throws std::invalid_argument if the input is not strictly valid, padded base64
 */
inline std::string decode(const std::string& encoded_string)
{
    return Base64::decode(encoded_string);
}

/**
 * @brief Validate base64 string (same strict rules as decode())
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
