// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file macros.h
/// @brief Helper macros: LOCKGUARD and the LOG_* logging macros.
///
/// Logging: LOG_DEBUG / LOG_TRACE / LOG_INFO / LOG_WARN / LOG_ERROR(fmt, ...) take a
/// printf-style format string followed by optional arguments. Choose a backend by
/// defining the macros yourself before including SocketsHpp (define at least
/// LOG_DEBUG, which marks them as provided), or define HAVE_CONSOLE_LOG to print each
/// message to stdout via printf with a leading space and a trailing newline.
/// Otherwise they route to the runtime hook SocketsHpp::setLogHandler() (see
/// utils/log.h): while no handler is installed a log statement is one relaxed atomic
/// load and its arguments are not evaluated. Define SOCKETSHPP_NO_RUNTIME_LOG to make
/// them compile to no-ops instead.
/// Defining HAVE_HTTP_DEBUG (see config.h) sends LOG_TRACE to printf regardless.

#include <mutex>

#ifndef TOKENPASTE
/// @brief Paste two tokens (no macro expansion of the arguments).
#  define TOKENPASTE(x, y) x##y
#endif

#ifndef TOKENPASTE2
/// @brief Paste two tokens after macro-expanding them (e.g. with __LINE__).
#  define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#endif

#ifndef LOCKGUARD
/// @brief Lock @p macro_mutex with a std::lock_guard until the end of the enclosing scope.
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

#if !defined(LOG_DEBUG) && defined(SOCKETSHPP_NO_RUNTIME_LOG)
// Don't log anything: no log facility defined and the runtime hook is disabled.
#  define LOG_DEBUG(...) ((void)0)
#  define LOG_TRACE(...) ((void)0)
#  define LOG_INFO(...) ((void)0)
#  define LOG_WARN(...) ((void)0)
#  define LOG_ERROR(...) ((void)0)
#endif

#ifndef LOG_DEBUG
// Default: route to the runtime log hook (SocketsHpp::setLogHandler(), utils/log.h,
// included by config.h). The level check comes first, so the arguments are only
// evaluated - and the message only formatted - when a handler wants the message.
/// @brief Route one message of level @p level (a SocketsHpp::LogLevel enumerator
///        name) to the runtime log handler, if one is installed and the level enabled.
#  define SOCKETSHPP_RUNTIME_LOG(level, ...)                                                   \
    (::SOCKETSHPP_NS::log_detail::enabled(::SOCKETSHPP_NS::LogLevel::level)                    \
         ? ::SOCKETSHPP_NS::log_detail::write(::SOCKETSHPP_NS::LogLevel::level, __VA_ARGS__) \
         : (void)0)
/// @brief Debug-level log: LOG_DEBUG(fmt, ...) with printf-style arguments.
#  define LOG_DEBUG(...) SOCKETSHPP_RUNTIME_LOG(Debug, __VA_ARGS__)
/// @brief Trace-level log: LOG_TRACE(fmt, ...) with printf-style arguments.
#  define LOG_TRACE(...) SOCKETSHPP_RUNTIME_LOG(Trace, __VA_ARGS__)
/// @brief Info-level log: LOG_INFO(fmt, ...) with printf-style arguments.
#  define LOG_INFO(...) SOCKETSHPP_RUNTIME_LOG(Info, __VA_ARGS__)
/// @brief Warning-level log: LOG_WARN(fmt, ...) with printf-style arguments.
#  define LOG_WARN(...) SOCKETSHPP_RUNTIME_LOG(Warn, __VA_ARGS__)
/// @brief Error-level log: LOG_ERROR(fmt, ...) with printf-style arguments.
#  define LOG_ERROR(...) SOCKETSHPP_RUNTIME_LOG(Error, __VA_ARGS__)
#endif

// SAL macro
#ifndef _Out_cap_
/// @brief Microsoft SAL annotation placeholder (expands to nothing when SAL is unavailable).
#  define _Out_cap_(size)
#endif
