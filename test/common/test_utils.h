// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

#include <cstdlib>

/**
 * @brief Obtain path to temporary directory
 * @return Temporary Directory
 */
static inline std::string GetTempDirectory()
{
    std::string result;
#ifdef _WIN32
    // NOTE: this is not very robust in case if TEMPDIR path is longer than 260 chars
    char temp_path_buffer[MAX_PATH + 1] = { 0 };
    ::GetTempPathA(MAX_PATH, temp_path_buffer);
    result = temp_path_buffer;
#else  // Unix
    char* tmp = getenv("TMPDIR");
    if (tmp != NULL)
    {
        result = tmp;
    }
    else
    {
#  ifdef P_tmpdir
        if (P_tmpdir != NULL)
            result = P_tmpdir;
#  endif
    }
#  ifdef _PATH_TMP
    if (result.empty())
    {
        result = _PATH_TMP;
    }
#  endif
    if (result.empty())
    {
        result = "/tmp";
    }
    result += "/";
#endif
    return result;
}

/**
 * @brief Generate a large test string with predictable content
 * @param maxLength Maximum length of the string
 * @return Generated test string
 */
static inline std::string GenerateBigString(size_t maxLength = 60000)
{
    std::string result;
    result.reserve(maxLength);
    for (size_t i = 0; i < maxLength; i++)
    {
        result.push_back(static_cast<char>(i % 255));
    }
    return result;
}

/**
 * @brief Generate a random port number in the ephemeral range
 * @return Random port number
 */
static inline int GetRandomEphemeralPort()
{
    // Ephemeral port range: 49152-65535
    return 49152 + (rand() % (65535 - 49152 + 1));
}

/**
 * @brief Generate unique socket name for testing
 * @param prefix Prefix for the socket name
 * @return Unique socket name path
 */
static inline std::string GetUniqueSocketName(const std::string& prefix = "test")
{
    auto temp_dir = GetTempDirectory();
    auto unique_id = std::to_string(rand());
    return temp_dir + prefix + "_" + unique_id + ".sock";
}

/**
 * @brief Test data generator for various payload sizes
 */
class TestDataGenerator
{
public:
    static std::string Small() { return "Hello, World!"; }
    
    static std::string Medium() 
    { 
        return std::string(1024, 'A'); // 1KB
    }
    
    static std::string Large() 
    { 
        return GenerateBigString(60000); // ~60KB
    }
    
    static std::vector<std::string> GetAllSizes()
    {
        return { Small(), Medium(), Large() };
    }
};

/**
 * @brief Common test addresses for various scenarios
 */
class TestAddresses
{
public:
    static std::string IPv4Loopback(int port = 3000)
    {
        return "127.0.0.1:" + std::to_string(port);
    }
    
    static std::string IPv6Loopback(int port = 3000)
    {
        return "[::1]:" + std::to_string(port);
    }
    
    static std::string IPv4Any(int port = 0)
    {
        return "0.0.0.0:" + std::to_string(port);
    }
    
    static std::string IPv6Any(int port = 0)
    {
        return "[::]:" + std::to_string(port);
    }
};
