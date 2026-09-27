// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file log.h
/// @brief Runtime log hook (SocketsHpp::setLogHandler()) used by the LOG_* macros.
///
/// Unless the application defines its own LOG_* macros or HAVE_CONSOLE_LOG (see
/// macros.h), every LOG_TRACE / LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERROR in the
/// library checks one atomic level and, only when a handler is installed and the
/// level is enabled, formats the message and passes it to the handler. With no
/// handler installed a log statement costs one relaxed atomic load and its
/// arguments are not evaluated. Define SOCKETSHPP_NO_RUNTIME_LOG before including
/// SocketsHpp to compile the macros to nothing instead.
///
/// @code
///   SocketsHpp::setLogHandler([](SocketsHpp::LogLevel level, const char* msg) {
///       std::fprintf(stderr, "[%s] %s\n", SocketsHpp::logLevelName(level), msg);
///   });
///   SocketsHpp::setLogLevel(SocketsHpp::LogLevel::Warn);  // default: Info
/// @endcode

#include <SocketsHpp/config.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#if defined(__MINGW32__) && !defined(__clang__)
// MinGW-w64 libstdc++ uses the C99-conforming mingw stdio (%zu, %lld), not msvcrt's.
#  define SOCKETSHPP_PRINTF_FORMAT(fmtIndex, argIndex) __attribute__((format(gnu_printf, fmtIndex, argIndex)))
#elif defined(__GNUC__) || defined(__clang__)
/// @brief printf format checking for the log formatter (GCC / Clang).
#  define SOCKETSHPP_PRINTF_FORMAT(fmtIndex, argIndex) __attribute__((format(printf, fmtIndex, argIndex)))
#else
/// @brief printf format checking for the log formatter (no-op on this compiler).
#  define SOCKETSHPP_PRINTF_FORMAT(fmtIndex, argIndex)
#endif

SOCKETSHPP_NS_BEGIN

/// @brief Severity of a library log message, in increasing order.
enum class LogLevel : int
{
    Trace = 0,  ///< Very verbose tracing (every socket event).
    Debug = 1,  ///< Debugging details.
    Info = 2,   ///< Normal operation (listening, requests).
    Warn = 3,   ///< Recoverable problems (timeouts, malformed requests, refused connections).
    Error = 4,  ///< Failures (bind errors, exceptions in handlers).
    Off = 5     ///< Disables logging (only meaningful for setLogLevel()).
};

/// @brief Receives formatted log messages: level and a NUL-terminated message without
///        a trailing newline (valid only during the call).
/// @note Called from any library thread (reactor, thread-pool workers, the caller of
///       an API), possibly concurrently and possibly while library locks are held:
///       it must be thread-safe, fast, and must not call back into the object that
///       logs (e.g. HttpServer).
using LogHandler = std::function<void(LogLevel level, const char* message)>;

/// @brief Name of a level: "TRACE", "DEBUG", "INFO", "WARN", "ERROR" or "OFF".
/// @param level Level to name.
/// @return Static string.
inline const char* logLevelName(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    default:
        return "OFF";
    }
}

/// @brief Internals of the runtime log hook; use setLogHandler() / setLogLevel().
namespace log_detail
{
    /// @brief Lowest level passed to the handler; LogLevel::Off (5) while no handler is set.
    inline std::atomic<int> g_threshold{ static_cast<int>(LogLevel::Off) };
    /// @brief Level configured with setLogLevel() (applies while a handler is set).
    inline std::atomic<int> g_level{ static_cast<int>(LogLevel::Info) };
    /// @brief Guards g_handler (only taken when a message is actually logged).
    inline std::mutex g_mutex;
    /// @brief Installed handler, or null.
    inline std::shared_ptr<const LogHandler> g_handler;

    /// @brief Recompute g_threshold; call with g_mutex held.
    inline void updateThresholdLocked() noexcept
    {
        g_threshold.store(g_handler ? g_level.load() : static_cast<int>(LogLevel::Off),
            std::memory_order_relaxed);
    }

    /// @brief Whether a message of @p level would reach a handler (one relaxed load).
    inline bool enabled(LogLevel level) noexcept
    {
        return static_cast<int>(level) >= g_threshold.load(std::memory_order_relaxed);
    }

    /// @brief Format a printf-style message and pass it to the current handler.
    ///        Messages longer than 2 KiB are truncated. Exceptions thrown by the
    ///        handler are swallowed.
    /// @param level Message level.
    /// @param fmt printf format string.
    inline void write(LogLevel level, const char* fmt, ...) SOCKETSHPP_PRINTF_FORMAT(2, 3);

    inline void write(LogLevel level, const char* fmt, ...)
    {
        std::shared_ptr<const LogHandler> handler;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            handler = g_handler;
        }
        if (!handler || !*handler)
        {
            return;
        }
        char buffer[2048];
        va_list args;
        va_start(args, fmt);
        int n = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        if (n < 0)
        {
            buffer[0] = '\0';
        }
        try
        {
            (*handler)(level, buffer);
        }
        catch (...)
        {
            // A log handler must never break the library.
        }
    }
}  // namespace log_detail

/// @brief Install (or, with an empty function, remove) the process-wide log handler
///        that receives the library's LOG_* messages.
/// @param handler Handler; empty removes it (logging then costs one atomic load).
/// @note Thread-safe; may be called at any time. A message being logged concurrently
///       may still reach the previous handler.
inline void setLogHandler(LogHandler handler)
{
    std::shared_ptr<const LogHandler> next;
    if (handler)
    {
        next = std::make_shared<const LogHandler>(std::move(handler));
    }
    std::lock_guard<std::mutex> lock(log_detail::g_mutex);
    log_detail::g_handler = std::move(next);
    log_detail::updateThresholdLocked();
}

/// @brief Set the lowest level passed to the log handler (default LogLevel::Info;
///        LogLevel::Off disables logging without removing the handler).
/// @param level Minimum level.
/// @note Thread-safe.
inline void setLogLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(log_detail::g_mutex);
    log_detail::g_level.store(static_cast<int>(level));
    log_detail::updateThresholdLocked();
}

/// @brief Current minimum level set with setLogLevel().
inline LogLevel getLogLevel()
{
    return static_cast<LogLevel>(log_detail::g_level.load());
}

SOCKETSHPP_NS_END
