// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <variant>

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace common
    {
        using json = nlohmann::json;

        /// @brief JSON-RPC 2.0 error object
        struct JsonRpcError
        {
            int code;
            std::string message;
            std::optional<json> data;

            json toJson() const
            {
                json j = {
                    {"code", code},
                    {"message", message}
                };
                if (data.has_value())
                {
                    j["data"] = data.value();
                }
                return j;
            }

            static JsonRpcError fromJson(const json& j)
            {
                JsonRpcError error;
                error.code = j.at("code").get<int>();
                error.message = j.at("message").get<std::string>();
                if (j.contains("data"))
                {
                    error.data = j["data"];
                }
                return error;
            }

            // Standard JSON-RPC 2.0 error codes
            static JsonRpcError parseError(const std::string& message = "Parse error")
            {
                return {-32700, message, std::nullopt};
            }

            static JsonRpcError invalidRequest(const std::string& message = "Invalid Request")
            {
                return {-32600, message, std::nullopt};
            }

            static JsonRpcError methodNotFound(const std::string& method)
            {
                return {-32601, "Method not found: " + method, std::nullopt};
            }

            static JsonRpcError invalidParams(const std::string& message = "Invalid params")
            {
                return {-32602, message, std::nullopt};
            }

            static JsonRpcError internalError(const std::string& message = "Internal error")
            {
                return {-32603, message, std::nullopt};
            }

            // MCP-specific error codes (-32000 to -32099 reserved for implementation-defined errors)
            static JsonRpcError serverError(int code, const std::string& message)
            {
                return {code, message, std::nullopt};
            }
        };

        /// @brief JSON-RPC 2.0 request
        struct JsonRpcRequest
        {
            std::string jsonrpc = "2.0";
            std::variant<std::string, int, std::nullptr_t> id; // Can be string, number, or null
            std::string method;
            std::optional<json> params;

            json toJson() const
            {
                json j = {
                    {"jsonrpc", jsonrpc},
                    {"method", method}
                };

                // Handle different ID types
                std::visit([&j](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::string>)
                    {
                        j["id"] = arg;
                    }
                    else if constexpr (std::is_same_v<T, int>)
                    {
                        j["id"] = arg;
                    }
                    else if constexpr (std::is_same_v<T, std::nullptr_t>)
                    {
                        j["id"] = nullptr;
                    }
                }, id);

                if (params.has_value())
                {
                    j["params"] = params.value();
                }

                return j;
            }

            std::string serialize() const
            {
                return toJson().dump();
            }

            static JsonRpcRequest parse(const std::string& jsonStr)
            {
                json j = json::parse(jsonStr);
                JsonRpcRequest req;

                req.jsonrpc = j.value("jsonrpc", "2.0");
                req.method = j.at("method").get<std::string>();

                // Parse ID
                if (j.contains("id"))
                {
                    if (j["id"].is_string())
                    {
                        req.id = j["id"].get<std::string>();
                    }
                    else if (j["id"].is_number_integer())
                    {
                        req.id = j["id"].get<int>();
                    }
                    else if (j["id"].is_null())
                    {
                        req.id = nullptr;
                    }
                }

                if (j.contains("params"))
                {
                    req.params = j["params"];
                }

                return req;
            }

            bool hasId() const
            {
                return !std::holds_alternative<std::nullptr_t>(id);
            }
        };

        /// @brief JSON-RPC 2.0 notification (request without ID)
        struct JsonRpcNotification
        {
            std::string jsonrpc = "2.0";
            std::string method;
            std::optional<json> params;

            json toJson() const
            {
                json j = {
                    {"jsonrpc", jsonrpc},
                    {"method", method}
                };

                if (params.has_value())
                {
                    j["params"] = params.value();
                }

                return j;
            }

            std::string serialize() const
            {
                return toJson().dump();
            }

            static JsonRpcNotification parse(const std::string& jsonStr)
            {
                json j = json::parse(jsonStr);
                JsonRpcNotification notif;

                notif.jsonrpc = j.value("jsonrpc", "2.0");
                notif.method = j.at("method").get<std::string>();

                if (j.contains("params"))
                {
                    notif.params = j["params"];
                }

                return notif;
            }
        };

        /// @brief JSON-RPC 2.0 response
        struct JsonRpcResponse
        {
            std::string jsonrpc = "2.0";
            std::variant<std::string, int, std::nullptr_t> id;
            std::optional<json> result;
            std::optional<JsonRpcError> error;

            json toJson() const
            {
                json j = {
                    {"jsonrpc", jsonrpc}
                };

                // Handle different ID types
                std::visit([&j](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::string>)
                    {
                        j["id"] = arg;
                    }
                    else if constexpr (std::is_same_v<T, int>)
                    {
                        j["id"] = arg;
                    }
                    else if constexpr (std::is_same_v<T, std::nullptr_t>)
                    {
                        j["id"] = nullptr;
                    }
                }, id);

                if (error.has_value())
                {
                    j["error"] = error->toJson();
                }
                else if (result.has_value())
                {
                    j["result"] = result.value();
                }
                else
                {
                    j["result"] = nullptr;
                }

                return j;
            }

            std::string serialize() const
            {
                return toJson().dump();
            }

            static JsonRpcResponse parse(const std::string& jsonStr)
            {
                json j = json::parse(jsonStr);
                JsonRpcResponse resp;

                resp.jsonrpc = j.value("jsonrpc", "2.0");

                // Parse ID
                if (j.contains("id"))
                {
                    if (j["id"].is_string())
                    {
                        resp.id = j["id"].get<std::string>();
                    }
                    else if (j["id"].is_number_integer())
                    {
                        resp.id = j["id"].get<int>();
                    }
                    else if (j["id"].is_null())
                    {
                        resp.id = nullptr;
                    }
                }

                if (j.contains("error"))
                {
                    resp.error = JsonRpcError::fromJson(j["error"]);
                }
                else if (j.contains("result"))
                {
                    resp.result = j["result"];
                }

                return resp;
            }

            static JsonRpcResponse success(const std::variant<std::string, int, std::nullptr_t>& id, const json& result)
            {
                JsonRpcResponse resp;
                resp.id = id;
                resp.result = result;
                return resp;
            }

            static JsonRpcResponse failure(const std::variant<std::string, int, std::nullptr_t>& id, const JsonRpcError& error)
            {
                JsonRpcResponse resp;
                resp.id = id;
                resp.error = error;
                return resp;
            }
        };

    } // namespace common
} // namespace http
SOCKETSHPP_NS_END
