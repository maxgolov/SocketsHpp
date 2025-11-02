// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace constants
    {
        // ====================================================================
        // Standard HTTP Headers
        // ====================================================================
        
        static constexpr const char* CONTENT_TYPE = "Content-Type";
        static constexpr const char* CONTENT_LENGTH = "Content-Length";
        static constexpr const char* TRANSFER_ENCODING = "Transfer-Encoding";
        static constexpr const char* CONNECTION = "Connection";
        static constexpr const char* HOST = "Host";
        static constexpr const char* DATE = "Date";
        static constexpr const char* ACCEPT = "Accept";
        static constexpr const char* CACHE_CONTROL = "Cache-Control";
        static constexpr const char* EXPECT = "Expect";
        
        // ====================================================================
        // CORS Headers
        // ====================================================================
        
        static constexpr const char* ACCESS_CONTROL_ALLOW_ORIGIN = "Access-Control-Allow-Origin";
        static constexpr const char* ACCESS_CONTROL_ALLOW_METHODS = "Access-Control-Allow-Methods";
        static constexpr const char* ACCESS_CONTROL_ALLOW_HEADERS = "Access-Control-Allow-Headers";
        static constexpr const char* ACCESS_CONTROL_EXPOSE_HEADERS = "Access-Control-Expose-Headers";
        static constexpr const char* ACCESS_CONTROL_MAX_AGE = "Access-Control-Max-Age";
        
        // ====================================================================
        // MCP (Model Context Protocol) Headers
        // ====================================================================
        
        static constexpr const char* MCP_SESSION_ID = "Mcp-Session-Id";
        static constexpr const char* LAST_EVENT_ID = "Last-Event-Id";
        
        // ====================================================================
        // Server-Specific Headers
        // ====================================================================
        
        static constexpr const char* X_ACCEL_BUFFERING = "X-Accel-Buffering";
        
        // ====================================================================
        // Content Types
        // ====================================================================
        
        static constexpr const char* CONTENT_TYPE_TEXT = "text/plain";
        static constexpr const char* CONTENT_TYPE_HTML = "text/html";
        static constexpr const char* CONTENT_TYPE_JSON = "application/json";
        static constexpr const char* CONTENT_TYPE_XML = "application/xml";
        static constexpr const char* CONTENT_TYPE_BINARY = "application/octet-stream";
        static constexpr const char* CONTENT_TYPE_SSE = "text/event-stream";
        static constexpr const char* CONTENT_TYPE_FORM_URLENCODED = "application/x-www-form-urlencoded";
        static constexpr const char* CONTENT_TYPE_MULTIPART = "multipart/form-data";
        
        // ====================================================================
        // HTTP Methods
        // ====================================================================
        
        static constexpr const char* METHOD_GET = "GET";
        static constexpr const char* METHOD_POST = "POST";
        static constexpr const char* METHOD_PUT = "PUT";
        static constexpr const char* METHOD_DELETE = "DELETE";
        static constexpr const char* METHOD_HEAD = "HEAD";
        static constexpr const char* METHOD_OPTIONS = "OPTIONS";
        static constexpr const char* METHOD_PATCH = "PATCH";
        static constexpr const char* METHOD_TRACE = "TRACE";
        static constexpr const char* METHOD_CONNECT = "CONNECT";
        
        // ====================================================================
        // HTTP Protocol Versions
        // ====================================================================
        
        static constexpr const char* HTTP_1_0 = "HTTP/1.0";
        static constexpr const char* HTTP_1_1 = "HTTP/1.1";
        static constexpr const char* HTTP_2_0 = "HTTP/2.0";
        
        // ====================================================================
        // Common Header Values
        // ====================================================================
        
        static constexpr const char* CONNECTION_KEEP_ALIVE = "keep-alive";
        static constexpr const char* CONNECTION_CLOSE = "close";
        static constexpr const char* TRANSFER_ENCODING_CHUNKED = "chunked";
        static constexpr const char* CACHE_CONTROL_NO_CACHE = "no-cache";
        static constexpr const char* EXPECT_100_CONTINUE = "100-continue";
        
        // ====================================================================
        // MCP Default Values
        // ====================================================================
        
        static constexpr const char* CORS_ALLOW_ORIGIN_ALL = "*";
        static constexpr const char* CORS_DEFAULT_METHODS = "GET, POST, DELETE, OPTIONS";
        static constexpr const char* CORS_DEFAULT_ALLOW_HEADERS = 
            "Content-Type, Accept, Authorization, x-api-key, Mcp-Session-Id, Last-Event-Id";
        static constexpr const char* CORS_DEFAULT_EXPOSE_HEADERS = 
            "Content-Type, Authorization, x-api-key, Mcp-Session-Id";
        static constexpr const char* CORS_DEFAULT_MAX_AGE = "86400";  // 24 hours
        
        // ====================================================================
        // SSE (Server-Sent Events) Field Names
        // ====================================================================
        
        static constexpr const char* SSE_FIELD_EVENT = "event";
        static constexpr const char* SSE_FIELD_DATA = "data";
        static constexpr const char* SSE_FIELD_ID = "id";
        static constexpr const char* SSE_FIELD_RETRY = "retry";
    }
}
SOCKETSHPP_NS_END
