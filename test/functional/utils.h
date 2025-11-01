// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

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