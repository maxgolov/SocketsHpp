// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>

SOCKETSHPP_NS_BEGIN
namespace http
{
    /// @brief String constants for HTTP header names, header values, media types,
    /// methods, protocol versions, CORS defaults and SSE field names.
    ///
    /// Header names use their canonical capitalization; HTTP field names are
    /// case-insensitive, so compare them case-insensitively.
    namespace constants
    {
        // ====================================================================
        // Standard HTTP Headers
        // ====================================================================
        
        static constexpr const char* CONTENT_TYPE = "Content-Type";  ///< Header name.
        static constexpr const char* CONTENT_LENGTH = "Content-Length";  ///< Header name.
        static constexpr const char* TRANSFER_ENCODING = "Transfer-Encoding";  ///< Header name.
        static constexpr const char* CONNECTION = "Connection";  ///< Header name.
        static constexpr const char* HOST = "Host";  ///< Header name.
        static constexpr const char* DATE = "Date";  ///< Header name.
        static constexpr const char* ACCEPT = "Accept";  ///< Header name.
        static constexpr const char* CACHE_CONTROL = "Cache-Control";  ///< Header name.
        static constexpr const char* EXPECT = "Expect";  ///< Header name.
        
        // ====================================================================
        // CORS Headers
        // ====================================================================
        
        static constexpr const char* ACCESS_CONTROL_ALLOW_ORIGIN = "Access-Control-Allow-Origin";  ///< CORS response header name.
        static constexpr const char* ACCESS_CONTROL_ALLOW_METHODS = "Access-Control-Allow-Methods";  ///< CORS response header name.
        static constexpr const char* ACCESS_CONTROL_ALLOW_HEADERS = "Access-Control-Allow-Headers";  ///< CORS response header name.
        static constexpr const char* ACCESS_CONTROL_EXPOSE_HEADERS = "Access-Control-Expose-Headers";  ///< CORS response header name.
        static constexpr const char* ACCESS_CONTROL_MAX_AGE = "Access-Control-Max-Age";  ///< CORS response header name.
        
        // ====================================================================
        // MCP (Model Context Protocol) Headers
        // ====================================================================
        
        /// @brief MCP Streamable HTTP session id header.
        static constexpr const char* MCP_SESSION_ID = "Mcp-Session-Id";
        /// @brief SSE resumption header (spelled "Last-Event-ID" by the SSE spec; names
        /// are case-insensitive).
        static constexpr const char* LAST_EVENT_ID = "Last-Event-Id";
        
        // ====================================================================
        // Server-Specific Headers
        // ====================================================================
        
        /// @brief nginx response header; "no" disables proxy buffering (used for SSE).
        static constexpr const char* X_ACCEL_BUFFERING = "X-Accel-Buffering";
        
        // ====================================================================
        // Content Types
        // ====================================================================
        
        static constexpr const char* CONTENT_TYPE_TEXT = "text/plain";  ///< Media type.
        static constexpr const char* CONTENT_TYPE_HTML = "text/html";  ///< Media type.
        static constexpr const char* CONTENT_TYPE_JSON = "application/json";  ///< Media type.
        static constexpr const char* CONTENT_TYPE_XML = "application/xml";  ///< Media type.
        static constexpr const char* CONTENT_TYPE_BINARY = "application/octet-stream";  ///< Media type.
        static constexpr const char* CONTENT_TYPE_SSE = "text/event-stream";  ///< Media type.
        static constexpr const char* CONTENT_TYPE_FORM_URLENCODED = "application/x-www-form-urlencoded";  ///< Media type.
        static constexpr const char* CONTENT_TYPE_MULTIPART = "multipart/form-data";  ///< Media type.
        
        // ====================================================================
        // HTTP Methods
        // ====================================================================
        
        static constexpr const char* METHOD_GET = "GET";  ///< Request method.
        static constexpr const char* METHOD_POST = "POST";  ///< Request method.
        static constexpr const char* METHOD_PUT = "PUT";  ///< Request method.
        static constexpr const char* METHOD_DELETE = "DELETE";  ///< Request method.
        static constexpr const char* METHOD_HEAD = "HEAD";  ///< Request method.
        static constexpr const char* METHOD_OPTIONS = "OPTIONS";  ///< Request method.
        static constexpr const char* METHOD_PATCH = "PATCH";  ///< Request method.
        static constexpr const char* METHOD_TRACE = "TRACE";  ///< Request method.
        static constexpr const char* METHOD_CONNECT = "CONNECT";  ///< Request method.
        
        // ====================================================================
        // HTTP Protocol Versions
        // ====================================================================
        
        static constexpr const char* HTTP_1_0 = "HTTP/1.0";  ///< Protocol version string.
        static constexpr const char* HTTP_1_1 = "HTTP/1.1";  ///< Protocol version string.
        /// @brief Version label only; the library speaks HTTP/1.x.
        static constexpr const char* HTTP_2_0 = "HTTP/2.0";
        
        // ====================================================================
        // Common Header Values
        // ====================================================================
        
        static constexpr const char* CONNECTION_KEEP_ALIVE = "keep-alive";  ///< Connection header value.
        static constexpr const char* CONNECTION_CLOSE = "close";  ///< Connection header value.
        static constexpr const char* TRANSFER_ENCODING_CHUNKED = "chunked";  ///< Transfer-Encoding value for chunked framing.
        static constexpr const char* CACHE_CONTROL_NO_CACHE = "no-cache";  ///< Cache-Control value.
        static constexpr const char* EXPECT_100_CONTINUE = "100-continue";  ///< Expect header value.
        
        // ====================================================================
        // MCP Default Values
        // ====================================================================
        
        /// @brief Access-Control-Allow-Origin value allowing any origin.
        static constexpr const char* CORS_ALLOW_ORIGIN_ALL = "*";
        static constexpr const char* CORS_DEFAULT_METHODS = "GET, POST, DELETE, OPTIONS";  ///< Default Access-Control-Allow-Methods value.
        static constexpr const char* CORS_DEFAULT_ALLOW_HEADERS = 
            "Content-Type, Accept, Authorization, x-api-key, Mcp-Session-Id, Last-Event-Id";  ///< Default Access-Control-Allow-Headers value.
        static constexpr const char* CORS_DEFAULT_EXPOSE_HEADERS = 
            "Content-Type, Authorization, x-api-key, Mcp-Session-Id";  ///< Default Access-Control-Expose-Headers value.
        /// @brief Default Access-Control-Max-Age in seconds (24 hours).
        static constexpr const char* CORS_DEFAULT_MAX_AGE = "86400";  // 24 hours
        
        // ====================================================================
        // SSE (Server-Sent Events) Field Names
        // ====================================================================
        
        static constexpr const char* SSE_FIELD_EVENT = "event";  ///< SSE field name.
        static constexpr const char* SSE_FIELD_DATA = "data";  ///< SSE field name.
        static constexpr const char* SSE_FIELD_ID = "id";  ///< SSE field name.
        static constexpr const char* SSE_FIELD_RETRY = "retry";  ///< SSE field name.
    }
}
SOCKETSHPP_NS_END
