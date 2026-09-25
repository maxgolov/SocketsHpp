// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file config.h
/// @brief Library-wide namespace macros and tunable limits (SocketsHpp::config).
///
/// Define HAVE_HTTP_DEBUG before including to route LOG_TRACE to printf
/// (see macros.h for the other logging macros).

#include "./macros.h"

#include <cstddef>
#include <cstdio>

#ifndef SOCKETSHPP_NS
/// @brief Name of the library's root namespace (default SocketsHpp; may be predefined).
#define SOCKETSHPP_NS SocketsHpp
#endif

#ifndef SOCKETSHPP_NS_BEGIN
/// @brief Opens the library's root namespace (may be predefined together with SOCKETSHPP_NS_END).
#define SOCKETSHPP_NS_BEGIN namespace SocketsHpp {
/// @brief Closes the namespace opened by SOCKETSHPP_NS_BEGIN.
#define SOCKETSHPP_NS_END }
#endif

#ifndef SOCKETSHPP_SERVER_NS_BEGIN
/// @brief Opens namespace SocketsHpp::Server (may be predefined).
#define SOCKETSHPP_SERVER_NS_BEGIN namespace SocketsHpp { namespace Server {
/// @brief Closes the namespace opened by SOCKETSHPP_SERVER_NS_BEGIN.
#define SOCKETSHPP_SERVER_NS_END } }
#endif

#ifdef HAVE_HTTP_DEBUG
#  ifdef LOG_TRACE
#    undef LOG_TRACE
#    define LOG_TRACE(...) (std::printf(__VA_ARGS__), std::printf("\n"))
#  endif
#endif

// Socket and HTTP server configuration constants
SOCKETSHPP_NS_BEGIN
/// @brief Compile-time defaults and limits for sockets, the Reactor and the HTTP server.
namespace config
{
    /// @brief Bytes read per recv() call by HttpServer (stack buffer, 2 KiB).
    constexpr size_t HTTP_RECV_BUFFER_SIZE = 2048;

    /// @brief listen() backlog (max pending connections) for HttpServer listening sockets.
    constexpr int SOCKET_LISTEN_BACKLOG = 10;

    /// @brief Maximum wait per Reactor poll iteration, in milliseconds (epoll_wait /
    ///        kevent / WSAWaitForMultipleEvents / UDP poll). Also bounds Reactor::stop() latency.
    constexpr unsigned REACTOR_POLL_TIMEOUT_MS = 500;

    /// @brief Maximum kevents fetched per Reactor iteration on Apple platforms (kqueue).
    constexpr int KQUEUE_DEFAULT_SIZE = 32;

    /// @brief Default HttpServer limit for request headers, in bytes (8 KiB).
    constexpr size_t MAX_HTTP_HEADER_SIZE = 8192;

    /// @brief Default HttpServer limit for request bodies, in bytes (2 MiB); also the
    ///        default limit for decompressed request bodies. Larger bodies get HTTP 413.
    constexpr size_t MAX_HTTP_BODY_SIZE = 2 * 1024 * 1024;

    /// @brief Default idle timeout of HTTP sessions, in seconds (1 hour).
    constexpr int DEFAULT_SESSION_TIMEOUT_SECONDS = 3600;

    /// @brief Default SSE resumability history duration, in milliseconds (5 minutes).
    /// @note Stored by the session manager but not currently enforced.
    constexpr int DEFAULT_HISTORY_DURATION_MS = 300000;

    /// @brief Default maximum SSE events kept per session for resumability (oldest dropped).
    constexpr size_t DEFAULT_MAX_HISTORY_SIZE = 1000;

    /// @brief Default maximum number of concurrent HTTP sessions; creating more fails
    ///        once expired sessions have been purged.
    constexpr size_t DEFAULT_MAX_SESSIONS = 10000;

    // HTTP security limits

    /// @brief Maximum request-target (URI) length in bytes (8 KiB; RFC 7230 recommends
    ///        at least 8000 octets). Longer requests get HTTP 414.
    constexpr size_t MAX_URI_LENGTH = 8192;

    /// @brief Maximum HTTP method length in bytes; longer requests get HTTP 400.
    constexpr size_t MAX_METHOD_LENGTH = 16;

    /// @brief Maximum HTTP protocol string length in bytes.
    /// @note Currently not referenced by the library.
    constexpr size_t MAX_PROTOCOL_LENGTH = 16;

    /// @brief Maximum query parameter key length in bytes (HttpRequest::parse_query() throws beyond it).
    constexpr size_t MAX_QUERY_KEY_LENGTH = 256;

    /// @brief Maximum query parameter value length in bytes (HttpRequest::parse_query() throws beyond it).
    constexpr size_t MAX_QUERY_VALUE_LENGTH = 4096;

    /// @brief Maximum number of query parameters (HttpRequest::parse_query() throws beyond it).
    constexpr size_t MAX_QUERY_PARAMS = 100;

    /// @brief Maximum request header name length in bytes; longer headers get HTTP 431.
    constexpr size_t MAX_HEADER_NAME_LENGTH = 256;

    /// @brief Maximum request header value length in bytes; longer headers get HTTP 431.
    constexpr size_t MAX_HEADER_VALUE_LENGTH = 8192;
}
SOCKETSHPP_NS_END
