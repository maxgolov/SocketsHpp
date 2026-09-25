// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file mcp_config.h
/// @brief Configuration structs for the MCP server (ServerConfig) and client (ClientConfig).

#include <SocketsHpp/config.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

SOCKETSHPP_NS_BEGIN
namespace mcp
{
    using json = nlohmann::json;  ///< nlohmann::json alias used throughout the MCP API.

    /// @brief MCP transport type
    enum class TransportType
    {
        STDIO,            ///< JSON-RPC over stdin/stdout. Server: caller-driven via MCPServer::processMessage() (no port is bound). Not supported by MCPClient.
        HTTP,             ///< HTTP POST + persistent SSE GET stream (MCP 2024-11-05).
        HTTP_STREAMABLE   ///< Streamable HTTP: POST returns JSON or SSE; GET stream optional (MCP 2025-03-26).
    };

    /// @brief Configuration for a stdio MCP server process (VS Code mcp.json "stdio" entry).
    /// @note Parsed and stored only: MCPClient does not implement the STDIO transport,
    ///       so no process is launched.
    struct StdioConfig
    {
        std::string command;                               ///< Executable to run, e.g. "npx", "node", "python".
        std::vector<std::string> args;                     ///< Command-line arguments (default: none).
        std::map<std::string, std::string> env;            ///< Extra environment variables (default: none).
        std::optional<std::string> envFile;                ///< Path to a .env file (default: unset).
        std::optional<std::string> cwd;                    ///< Working directory (default: unset = inherit).

        /// @brief Load from JSON (VS Code mcp.json format).
        /// @param j Object with required "command" and optional "args", "env", "envFile", "cwd".
        /// @return Parsed configuration.
        /// @throws nlohmann::json::exception if "command" is missing or a field has the wrong type.
        static StdioConfig fromJson(const json& j)
        {
            StdioConfig config;
            config.command = j.at("command").get<std::string>();
            
            if (j.contains("args"))
            {
                config.args = j["args"].get<std::vector<std::string>>();
            }
            
            if (j.contains("env"))
            {
                config.env = j["env"].get<std::map<std::string, std::string>>();
            }
            
            if (j.contains("envFile"))
            {
                config.envFile = j["envFile"].get<std::string>();
            }
            
            if (j.contains("cwd"))
            {
                config.cwd = j["cwd"].get<std::string>();
            }
            
            return config;
        }
    };

    /// @brief Client-side configuration for the HTTP and Streamable HTTP transports.
    struct HttpConfig
    {
        /// @brief Full URL of the server's MCP endpoint (e.g. "http://127.0.0.1:8080/mcp").
        ///        Used for POST requests, the GET notification stream and the DELETE on disconnect.
        std::string url;
        /// @brief Headers sent with every request, including the notification stream and
        ///        DELETE (e.g. {"Authorization", "Bearer ..."}). Default: none.
        std::map<std::string, std::string> headers;
        /// @brief HTTP read timeout in seconds (default 30; <= 0 keeps the HttpClient default).
        int timeoutSeconds = 30;
        /// @brief Open the server-to-client notification stream after initialize() even
        ///        without a registered notification handler (default false). The stream
        ///        auto-reconnects and resends Last-Event-ID.
        bool enableResumability = false;
        /// @brief Event history duration in milliseconds (default 300000).
        /// @note Currently not used by MCPClient.
        int historyDurationMs = 300000;

        /// @brief Load from JSON (VS Code mcp.json format).
        /// @param j Object with required "url" and optional "headers" (object) and
        ///          "timeout" (seconds). Other fields keep their defaults.
        /// @return Parsed configuration.
        /// @throws nlohmann::json::exception if "url" is missing or a field has the wrong type.
        static HttpConfig fromJson(const json& j)
        {
            HttpConfig config;
            config.url = j.at("url").get<std::string>();
            
            if (j.contains("headers"))
            {
                config.headers = j["headers"].get<std::map<std::string, std::string>>();
            }
            
            if (j.contains("timeout"))
            {
                config.timeoutSeconds = j["timeout"].get<int>();
            }
            
            return config;
        }
    };

    /// @brief Server-side MCP configuration (see mcp::server::MCPServer).
    /// @note Copied into MCPServer at construction; later changes have no effect.
    struct ServerConfig
    {
        /// @brief Transport (default STDIO). STDIO servers never bind a port; drive them
        ///        with MCPServer::processMessage().
        TransportType transport = TransportType::STDIO;

        /// @brief Server name reported in the default initialize result (serverInfo) and
        ///        by GET /health (default "mcp-server"; empty also means "mcp-server").
        std::string serverName    = "mcp-server";
        /// @brief Server version reported alongside serverName (default "1.0.0"; empty also means "1.0.0").
        std::string serverVersion = "1.0.0";

        /// @brief TCP port for the HTTP transports (default 8080). 0 binds an ephemeral
        ///        port; read the actual one with MCPServer::port() after listen().
        int port = 8080;
        /// @brief URL path of the MCP endpoint (default "/mcp"). GET /health is always added.
        std::string endpoint = "/mcp";
        /// @brief Local address to bind in MCPServer::listen() (default "127.0.0.1").
        ///        Only this address is bound. Anything other than "127.0.0.1", "localhost",
        ///        "::1" or "[::1]" requires allowNonLoopback.
        std::string host = "127.0.0.1";

        /// @brief How an initialize response is sent on the HTTP (2024-11-05) transport.
        enum class ResponseMode
        {
            BATCH,    ///< Plain application/json response (default).
            STREAM    ///< As a single SSE event when the client Accepts text/event-stream.
        };
        /// @brief Response mode (default BATCH). Only affects initialize on TransportType::HTTP;
        ///        Streamable HTTP always negotiates JSON vs SSE from the Accept header.
        ResponseMode responseMode = ResponseMode::BATCH;

        /// @brief Maximum HTTP request body size in bytes (default 4 MiB); larger
        ///        requests are rejected by the HTTP server.
        size_t maxMessageSize = 4 * 1024 * 1024;  // 4MB default
        /// @brief Batch timeout in milliseconds (default 30000).
        /// @note Currently not used by MCPServer.
        int batchTimeoutMs = 30000;                // 30 seconds

        /// @brief CORS header values added to every MCP endpoint response and GET /health.
        struct CorsConfig
        {
            std::string allowOrigin = "*";  ///< Access-Control-Allow-Origin (default "*").
            std::string allowMethods = "GET, POST, DELETE, OPTIONS";  ///< Access-Control-Allow-Methods.
            std::string allowHeaders = "Content-Type, Accept, Authorization, x-api-key, Mcp-Session-Id, Last-Event-ID";  ///< Access-Control-Allow-Headers.
            std::string exposeHeaders = "Content-Type, Authorization, x-api-key, Mcp-Session-Id";  ///< Access-Control-Expose-Headers.
            std::string maxAge = "86400";  ///< Access-Control-Max-Age in seconds (default "86400" = 24 h).
        } cors;  ///< CORS settings.

        /// @brief MCP session management (Mcp-Session-Id).
        struct SessionConfig
        {
            /// @brief Issue a session id on a successful initialize and validate it on later
            ///        requests (default true). When false no ids are issued, so the GET
            ///        notification stream (which always requires a session) is unavailable.
            bool enabled = true;
            /// @brief Header carrying the session id in requests and the initialize response
            ///        (default "Mcp-Session-Id"; matched case-insensitively). MCPClient always
            ///        uses "Mcp-Session-Id".
            std::string headerName = "Mcp-Session-Id";
            /// @brief Allow clients to end their session with HTTP DELETE (default true;
            ///        false answers 405).
            bool allowClientTermination = true;
            /// @brief Idle timeout in seconds (default 3600). Each validated request
            ///        refreshes it; an expired session gets HTTP 404.
            int sessionTimeoutSeconds = 3600;  // 1 hour
        } session;  ///< Session settings.

        /// @brief SSE resumability (Last-Event-ID).
        struct ResumabilityConfig
        {
            /// @brief When true (default false), events pushed with MCPServer::push_event()
            ///        get an "id:" and are kept in per-session history; a GET stream opened
            ///        with Last-Event-ID replays the events after that id.
            bool enabled = false;
            /// @brief History retention in milliseconds (default 300000 = 5 min).
            /// Older events are dropped and can no longer be replayed.
            int historyDurationMs = 300000;  // 5 minutes
            /// @brief Maximum events kept per session (default 1000); oldest are dropped first.
            size_t maxHistorySize = 1000;    // Max events to keep
        } resumability;  ///< Resumability settings.

        /// @brief Request authentication for the HTTP transports (POST, GET and DELETE on
        ///        the MCP endpoint; not OPTIONS or /health).
        ///
        /// Order of checks: #validator if set; otherwise for BEARER an HS256 JWT check
        /// (only when built with SOCKETSHPP_HAS_JWT_CPP) and for API_KEY /
        /// CAPABILITY_TOKEN a constant-time comparison with #secretOrPublicKey.
        /// A missing/invalid credential gets 401; when no usable validator or non-empty
        /// secret is configured every request gets 500 (fails closed).
        struct AuthConfig
        {
            /// @brief Enable authentication (default false).
            bool enabled = false;
            /// @brief Credential kind.
            enum class Type
            {
                NONE,             ///< Generic: token is the raw #headerName value; needs a #validator.
                BEARER,           ///< Bearer token in #headerName ("Bearer " prefix stripped, case-insensitive); JWT or opaque.
                API_KEY,          ///< API key in #headerName (set it to e.g. "x-api-key").
                CAPABILITY_TOKEN  ///< Token in the X-MCP-Capability-Token header (#headerName is ignored).
            } type = Type::NONE;  ///< Credential kind (default NONE).

            /// @brief Request header holding the credential for NONE/BEARER/API_KEY, matched
            ///        case-insensitively (default "Authorization"). parseEnv() sets "x-api-key"
            ///        for MCP_AUTH_TYPE=api-key; set it yourself when configuring API_KEY in code.
            std::string headerName = "Authorization";
            /// @brief Shared secret: the HS256 JWT key for BEARER (requires
            ///        SOCKETSHPP_HAS_JWT_CPP), or the expected value for API_KEY /
            ///        CAPABILITY_TOKEN. Unset or empty counts as not configured.
            std::optional<std::string> secretOrPublicKey;

            /// @brief Custom token check; takes precedence over #secretOrPublicKey.
            ///        Receives the token (without "Bearer " for BEARER) and returns true to accept.
            /// @note Called concurrently from HTTP worker threads; must be thread-safe.
            std::function<bool(const std::string& token)> validator;
        } auth;  ///< Authentication settings.

        /// @brief Allow MCPServer::listen() to bind a non-loopback #host (default false).
        ///        This guard only inspects #host; the loopback names are "127.0.0.1",
        ///        "localhost", "::1" and "[::1]".
        bool allowNonLoopback = false;
        /// @brief Maximum requests per client per fixed 60-second window (default 0 = disabled).
        ///        Applies to POST and GET on the MCP endpoint; excess requests get HTTP 429
        ///        with Retry-After: 60. Clients are keyed by TCP peer address (without
        ///        port) unless #trustProxyHeaders is set.
        int maxRequestsPerMinute = 0;
        /// @brief Key rate limiting on the last X-Forwarded-For hop instead of the peer
        ///        address (default false).
        /// @warning Enable ONLY when the server is reachable exclusively through a reverse
        ///          proxy that overwrites/appends X-Forwarded-For; otherwise clients can spoof it.
        bool trustProxyHeaders = false;
        /// @brief Interval in seconds between ": keepalive" comments on an idle SSE
        ///        notification stream (default 30; <= 0 means 30). Keepalives also refresh
        ///        the idle time used by MCPServer::cleanupStaleSessions().
        /// @note Independently of this value, an idle stream writes an empty SSE comment
        ///       about every 200 ms.
        int sseWriteDeadlineSeconds = 30;

        /// @brief Override settings from command-line arguments. Unknown arguments are ignored.
        ///
        /// Recognized: `--transport http|streamable|http-streamable` (any other value = STDIO),
        /// `--port N`, `--endpoint PATH`, `--host ADDR`, `--response-mode stream|batch`,
        /// `--max-message-size BYTES`, `--enable-resumability`, `--cors-origin ORIGIN`.
        /// @param argc Argument count (argv[0] is skipped).
        /// @param argv Argument vector.
        /// @throws std::invalid_argument / std::out_of_range for a non-numeric port or size.
        void parseArgs(int argc, char** argv)
        {
            for (int i = 1; i < argc; ++i)
            {
                std::string arg = argv[i];
                
                if (arg == "--transport" && i + 1 < argc)
                {
                    std::string t = argv[++i];
                    if (t == "http")              transport = TransportType::HTTP;
                    else if (t == "streamable" ||
                             t == "http-streamable") transport = TransportType::HTTP_STREAMABLE;
                    else                          transport = TransportType::STDIO;
                }
                else if (arg == "--port" && i + 1 < argc)
                {
                    port = std::stoi(argv[++i]);
                }
                else if (arg == "--endpoint" && i + 1 < argc)
                {
                    endpoint = argv[++i];
                }
                else if (arg == "--host" && i + 1 < argc)
                {
                    host = argv[++i];
                }
                else if (arg == "--response-mode" && i + 1 < argc)
                {
                    std::string mode = argv[++i];
                    responseMode = (mode == "stream") ? ResponseMode::STREAM : ResponseMode::BATCH;
                }
                else if (arg == "--max-message-size" && i + 1 < argc)
                {
                    maxMessageSize = std::stoull(argv[++i]);
                }
                else if (arg == "--enable-resumability")
                {
                    resumability.enabled = true;
                }
                else if (arg == "--cors-origin" && i + 1 < argc)
                {
                    cors.allowOrigin = argv[++i];
                }
            }
        }

        /// @brief Override settings from environment variables (unset variables are ignored).
        ///
        /// MCP_TRANSPORT (as --transport), MCP_PORT, MCP_ENDPOINT, MCP_HOST,
        /// MCP_RESPONSE_MODE (stream|batch), MCP_MAX_MESSAGE_SIZE,
        /// MCP_ENABLE_RESUMABILITY ("true"/"1"; anything else disables), MCP_CORS_ORIGIN,
        /// MCP_AUTH_TYPE (enables auth; "bearer" = BEARER, "api-key" = API_KEY with
        /// header "x-api-key"), MCP_AUTH_SECRET.
        /// @throws std::invalid_argument / std::out_of_range for a non-numeric port or
        ///         size, or an unknown MCP_AUTH_TYPE.
        void parseEnv()
        {
            auto getEnv = [](const char* name) -> std::optional<std::string> {
                const char* val = std::getenv(name);
                return val ? std::optional<std::string>(val) : std::nullopt;
            };

            if (auto val = getEnv("MCP_TRANSPORT"))
            {
                if (*val == "http")              transport = TransportType::HTTP;
                else if (*val == "streamable" ||
                         *val == "http-streamable") transport = TransportType::HTTP_STREAMABLE;
                else                             transport = TransportType::STDIO;
            }
            if (auto val = getEnv("MCP_PORT"))
            {
                port = std::stoi(*val);
            }
            if (auto val = getEnv("MCP_ENDPOINT"))
            {
                endpoint = *val;
            }
            if (auto val = getEnv("MCP_HOST"))
            {
                host = *val;
            }
            if (auto val = getEnv("MCP_RESPONSE_MODE"))
            {
                responseMode = (*val == "stream") ? ResponseMode::STREAM : ResponseMode::BATCH;
            }
            if (auto val = getEnv("MCP_MAX_MESSAGE_SIZE"))
            {
                maxMessageSize = std::stoull(*val);
            }
            if (auto val = getEnv("MCP_ENABLE_RESUMABILITY"))
            {
                resumability.enabled = (*val == "true" || *val == "1");
            }
            if (auto val = getEnv("MCP_CORS_ORIGIN"))
            {
                cors.allowOrigin = *val;
            }
            if (auto val = getEnv("MCP_AUTH_TYPE"))
            {
                auth.enabled = true;
                if (*val == "bearer")
                {
                    auth.type = AuthConfig::Type::BEARER;
                }
                else if (*val == "api-key")
                {
                    auth.type = AuthConfig::Type::API_KEY;
                    auth.headerName = "x-api-key";
                }
                else
                {
                    // Fail at startup rather than rejecting every request later.
                    throw std::invalid_argument("MCP_AUTH_TYPE must be \"bearer\" or \"api-key\", got \"" +
                                                *val + "\"");
                }
            }
            if (auto val = getEnv("MCP_AUTH_SECRET"))
            {
                auth.secretOrPublicKey = *val;
            }
        }
    };

    /// @brief Client-side MCP configuration (see mcp::client::MCPClient).
    struct ClientConfig
    {
        /// @brief Transport (default STDIO, which MCPClient does not support: set HTTP or
        ///        HTTP_STREAMABLE).
        TransportType transport = TransportType::STDIO;
        StdioConfig stdio;   ///< Used if transport == STDIO (parsed only; not supported by MCPClient).
        HttpConfig http;     ///< Used if transport == HTTP or HTTP_STREAMABLE.

        /// @brief Maximum request retries (default 3).
        /// @note Currently not used by MCPClient (requests are not retried).
        int maxRetries = 3;
        /// @brief Delay between retries in milliseconds (default 1000).
        /// @note Currently not used by MCPClient.
        int retryBackoffMs = 1000;

        /// @brief TCP connect timeout in seconds (default 10; <= 0 keeps the HttpClient default).
        int connectTimeoutSeconds = 10;
        /// @brief Read timeout in seconds (default 30).
        /// @note Currently not used; the read timeout comes from HttpConfig::timeoutSeconds.
        int readTimeoutSeconds = 30;

        /// @brief Load from a VS Code mcp.json server entry.
        /// @param j Object with "type": "stdio", "http" / "sse" (TransportType::HTTP) or
        ///          "streamable" / "http-streamable" (TransportType::HTTP_STREAMABLE), plus
        ///          the fields read by StdioConfig::fromJson() or HttpConfig::fromJson().
        /// @return Parsed configuration.
        /// @throws std::invalid_argument for an unknown "type"; nlohmann::json::exception
        ///         for missing/mistyped fields.
        static ClientConfig fromJson(const json& j)
        {
            ClientConfig config;
            
            std::string type = j.at("type").get<std::string>();
            if (type == "stdio")
            {
                config.transport = TransportType::STDIO;
                config.stdio = StdioConfig::fromJson(j);
            }
            else if (type == "http" || type == "sse")
            {
                config.transport = TransportType::HTTP;
                config.http = HttpConfig::fromJson(j);
            }
            else if (type == "streamable" || type == "http-streamable")
            {
                config.transport = TransportType::HTTP_STREAMABLE;
                config.http = HttpConfig::fromJson(j);
            }
            else
            {
                throw std::invalid_argument("Unknown MCP transport type: " + type);
            }
            
            return config;
        }
    };

} // namespace mcp
SOCKETSHPP_NS_END
