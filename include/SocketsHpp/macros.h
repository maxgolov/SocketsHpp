// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <mutex>

#ifndef TOKENPASTE
#  define TOKENPASTE(x, y) x##y
#endif

#ifndef TOKENPASTE2
#  define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#endif

#ifndef LOCKGUARD
#  define LOCKGUARD(macro_mutex) \
    std::lock_guard<decltype(macro_mutex)> TOKENPASTE2(__guard_, __LINE__)(macro_mutex)
#endif

// Logging macros take a printf-style format string followed by optional
// arguments. They are declared as pure variadic macros so that calls with a
// format string only (no arguments) are valid ISO C++ under -Wpedantic.
#if defined(HAVE_CONSOLE_LOG) && !defined(LOG_DEBUG)
// Log to console if there's no standard log facility defined
#  include <cstdio>
#  define SOCKETSHPP_CONSOLE_LOG(...) (std::printf(" " __VA_ARGS__), std::printf("\n"))
#  define LOG_DEBUG(...) SOCKETSHPP_CONSOLE_LOG(__VA_ARGS__)
#  define LOG_TRACE(...) SOCKETSHPP_CONSOLE_LOG(__VA_ARGS__)
#  define LOG_INFO(...) SOCKETSHPP_CONSOLE_LOG(__VA_ARGS__)
#  define LOG_WARN(...) SOCKETSHPP_CONSOLE_LOG(__VA_ARGS__)
#  define LOG_ERROR(...) SOCKETSHPP_CONSOLE_LOG(__VA_ARGS__)
#endif

#ifndef LOG_DEBUG
// Don't log anything if there's no standard log facility defined
#  define LOG_DEBUG(...) ((void)0)
#  define LOG_TRACE(...) ((void)0)
#  define LOG_INFO(...) ((void)0)
#  define LOG_WARN(...) ((void)0)
#  define LOG_ERROR(...) ((void)0)
#endif

// SAL macro
#ifndef _Out_cap_
#  define _Out_cap_(size)
#endif
