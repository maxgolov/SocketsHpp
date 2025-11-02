// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "./macros.h"

#ifndef SOCKETSHPP_NS
#define SOCKETSHPP_NS SocketsHpp
#endif

#ifndef SOCKETSHPP_NS_BEGIN
#define SOCKETSHPP_NS_BEGIN namespace SocketsHpp {
#define SOCKETSHPP_NS_END }
#endif

#ifndef SOCKETSHPP_SERVER_NS_BEGIN
#define SOCKETSHPP_SERVER_NS_BEGIN namespace SocketsHpp { namespace Server {
#define SOCKETSHPP_SERVER_NS_END } }
#endif

#ifdef HAVE_HTTP_DEBUG
#  ifdef LOG_TRACE
#    undef LOG_TRACE
#    define LOG_TRACE(x, ...) printf(x "\n", __VA_ARGS__)
#  endif
#endif

// Socket and HTTP server configuration constants
SOCKETSHPP_NS_BEGIN
namespace config
{
    /// Socket buffer size for reading HTTP requests (2KB)
    constexpr size_t HTTP_RECV_BUFFER_SIZE = 2048;

    /// Default backlog for socket listen() - max pending connections
    constexpr int SOCKET_LISTEN_BACKLOG = 10;

    /// Reactor polling timeout in milliseconds
    constexpr unsigned REACTOR_POLL_TIMEOUT_MS = 500;

    /// Default kqueue size for macOS
    constexpr int KQUEUE_DEFAULT_SIZE = 32;

    /// Default max HTTP request header size (8KB)
    constexpr size_t MAX_HTTP_HEADER_SIZE = 8192;

    /// Default max HTTP request body size (2MB)
    constexpr size_t MAX_HTTP_BODY_SIZE = 2 * 1024 * 1024;

    /// Default session timeout (1 hour)
    constexpr int DEFAULT_SESSION_TIMEOUT_SECONDS = 3600;

    /// Default session history duration for SSE resumability (5 minutes)
    constexpr int DEFAULT_HISTORY_DURATION_MS = 300000;

    /// Default max session history size (events)
    constexpr size_t DEFAULT_MAX_HISTORY_SIZE = 1000;

    /// Default max sessions allowed
    constexpr size_t DEFAULT_MAX_SESSIONS = 10000;

    /// HTTP Security Limits
    /// Maximum URI length (8KB) - RFC 7230 recommends at least 8000 octets
    constexpr size_t MAX_URI_LENGTH = 8192;

    /// Maximum HTTP method length
    constexpr size_t MAX_METHOD_LENGTH = 16;

    /// Maximum HTTP protocol string length
    constexpr size_t MAX_PROTOCOL_LENGTH = 16;

    /// Maximum query parameter key length
    constexpr size_t MAX_QUERY_KEY_LENGTH = 256;

    /// Maximum query parameter value length
    constexpr size_t MAX_QUERY_VALUE_LENGTH = 4096;

    /// Maximum number of query parameters
    constexpr size_t MAX_QUERY_PARAMS = 100;

    /// Maximum header name length
    constexpr size_t MAX_HEADER_NAME_LENGTH = 256;

    /// Maximum header value length
    constexpr size_t MAX_HEADER_VALUE_LENGTH = 8192;
}
SOCKETSHPP_NS_END
