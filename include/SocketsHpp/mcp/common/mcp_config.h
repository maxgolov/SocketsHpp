// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

SOCKETSHPP_NS_BEGIN
namespace mcp
{
    using json = nlohmann::json;

    /// @brief MCP transport type
    enum class TransportType
    {
        STDIO,  // Standard input/output
        HTTP    // HTTP/HTTPS with SSE
    };

    /// @brief Configuration for stdio transport
    struct StdioConfig
    {
        std::string command;                               // e.g., "npx", "node", "python"
        std::vector<std::string> args;                     // Command arguments
        std::map<std::string, std::string> env;            // Environment variables
        std::optional<std::string> envFile;                // Path to .env file
        std::optional<std::string> cwd;                    // Working directory

        /// @brief Load from JSON (matches VS Code mcp.json format)
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

    /// @brief Configuration for HTTP transport
    struct HttpConfig
    {
        std::string url;                                   // Server URL
        std::map<std::string, std::string> headers;        // Custom headers (e.g., Authorization)
        int timeoutSeconds = 30;                           // Request timeout
        bool enableResumability = false;                   // Enable Last-Event-ID support
        int historyDurationMs = 300000;                    // Event history duration (5 min default)

        /// @brief Load from JSON (matches VS Code mcp.json format)
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

    /// @brief Server-side MCP configuration
    struct ServerConfig
    {
        TransportType transport = TransportType::STDIO;
        
        // HTTP transport options
        int port = 8080;
        std::string endpoint = "/mcp";
        std::string host = "127.0.0.1";
        
        // Response mode
        enum class ResponseMode
        {
            BATCH,    // Collect all responses, send as single JSON
            STREAM    // Stream responses via SSE
        };
        ResponseMode responseMode = ResponseMode::BATCH;
        
        // Limits
        size_t maxMessageSize = 4 * 1024 * 1024;  // 4MB default
        int batchTimeoutMs = 30000;                // 30 seconds
        
        // CORS
        struct CorsConfig
        {
            std::string allowOrigin = "*";
            std::string allowMethods = "GET, POST, DELETE, OPTIONS";
            std::string allowHeaders = "Content-Type, Accept, Authorization, x-api-key, Mcp-Session-Id, Last-Event-ID";
            std::string exposeHeaders = "Content-Type, Authorization, x-api-key, Mcp-Session-Id";
            std::string maxAge = "86400";
        } cors;
        
        // Session management
        struct SessionConfig
        {
            bool enabled = true;
            std::string headerName = "Mcp-Session-Id";
            bool allowClientTermination = true;
            int sessionTimeoutSeconds = 3600;  // 1 hour
        } session;
        
        // Resumability (Last-Event-ID)
        struct ResumabilityConfig
        {
            bool enabled = false;
            int historyDurationMs = 300000;  // 5 minutes
            size_t maxHistorySize = 1000;    // Max events to keep
        } resumability;
        
        // Authentication (optional)
        struct AuthConfig
        {
            bool enabled = false;
            enum class Type
            {
                NONE,
                BEARER,    // Bearer token (JWT or opaque)
                API_KEY    // x-api-key header
            } type = Type::NONE;
            
            std::string headerName = "Authorization";  // For Bearer: "Authorization", for API key: "x-api-key"
            std::optional<std::string> secretOrPublicKey;  // For validation
            
            // Custom validator function
            std::function<bool(const std::string& token)> validator;
        } auth;

        /// @brief Set transport from command-line arguments
        void parseArgs(int argc, char** argv)
        {
            for (int i = 1; i < argc; ++i)
            {
                std::string arg = argv[i];
                
                if (arg == "--transport" && i + 1 < argc)
                {
                    std::string t = argv[++i];
                    transport = (t == "http") ? TransportType::HTTP : TransportType::STDIO;
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

        /// @brief Load from environment variables
        void parseEnv()
        {
            auto getEnv = [](const char* name) -> std::optional<std::string> {
                const char* val = std::getenv(name);
                return val ? std::optional<std::string>(val) : std::nullopt;
            };

            if (auto val = getEnv("MCP_TRANSPORT"))
            {
                transport = (*val == "http") ? TransportType::HTTP : TransportType::STDIO;
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
            }
            if (auto val = getEnv("MCP_AUTH_SECRET"))
            {
                auth.secretOrPublicKey = *val;
            }
        }
    };

    /// @brief Client-side MCP configuration
    struct ClientConfig
    {
        TransportType transport;
        StdioConfig stdio;   // Used if transport == STDIO
        HttpConfig http;     // Used if transport == HTTP
        
        // Retry settings
        int maxRetries = 3;
        int retryBackoffMs = 1000;
        
        // Timeouts
        int connectTimeoutSeconds = 10;
        int readTimeoutSeconds = 30;

        /// @brief Load from VS Code mcp.json server configuration
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
            
            return config;
        }
    };

} // namespace mcp
SOCKETSHPP_NS_END
