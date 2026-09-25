// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
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

        /// @brief JSON-RPC 2.0 message id.
        ///
        /// - std::monostate : the "id" member is absent (a notification, or a
        ///                    response whose id could not be determined)
        /// - std::string    : string id
        /// - std::int64_t   : integer id (full 64-bit range, no narrowing)
        /// - std::nullptr_t : explicit "id": null
        using JsonRpcId = std::variant<std::monostate, std::string, std::int64_t, std::nullptr_t>;

        /// @brief Returns true if the id holds a value (string, integer or explicit null).
        inline bool jsonRpcIdPresent(const JsonRpcId& id)
        {
            return !std::holds_alternative<std::monostate>(id);
        }

        /// @brief Convert a JSON value to a JsonRpcId.
        /// @return false if the value is not a valid JSON-RPC id (string, integer, null).
        inline bool jsonRpcIdFromJson(const json& j, JsonRpcId& out)
        {
            if (j.is_string())
            {
                out = j.get<std::string>();
                return true;
            }
            if (j.is_number_unsigned())
            {
                auto u = j.get<std::uint64_t>();
                if (u > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
                {
                    return false;
                }
                out = static_cast<std::int64_t>(u);
                return true;
            }
            if (j.is_number_integer())
            {
                out = j.get<std::int64_t>();
                return true;
            }
            if (j.is_null())
            {
                out = nullptr;
                return true;
            }
            return false;
        }

        /// @brief Convert a JsonRpcId to JSON. An absent id (monostate) maps to null.
        inline json jsonRpcIdToJson(const JsonRpcId& id)
        {
            if (auto s = std::get_if<std::string>(&id))
            {
                return *s;
            }
            if (auto n = std::get_if<std::int64_t>(&id))
            {
                return *n;
            }
            return nullptr;
        }

        /// @brief JSON-RPC 2.0 request
        struct JsonRpcRequest
        {
            std::string jsonrpc = "2.0";
            JsonRpcId id; // absent (monostate), string, integer, or null
            std::string method;
            std::optional<json> params;

            json toJson() const
            {
                json j = {
                    {"jsonrpc", jsonrpc},
                    {"method", method}
                };

                // Absent id is omitted (serializes as a notification)
                if (jsonRpcIdPresent(id))
                {
                    j["id"] = jsonRpcIdToJson(id);
                }

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

                // Parse ID (absent stays std::monostate)
                if (j.contains("id"))
                {
                    if (!jsonRpcIdFromJson(j["id"], req.id))
                    {
                        throw std::invalid_argument("JSON-RPC id must be a string, integer or null");
                    }
                }

                if (j.contains("params"))
                {
                    req.params = j["params"];
                }

                return req;
            }

            /// @brief True if the message carried an "id" member (including "id": null).
            /// A request without an id is a notification.
            bool hasId() const
            {
                return jsonRpcIdPresent(id);
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
            JsonRpcId id;
            std::optional<json> result;
            std::optional<JsonRpcError> error;

            json toJson() const
            {
                json j = {
                    {"jsonrpc", jsonrpc}
                };

                // A response always carries an id; unknown/absent id is null
                j["id"] = jsonRpcIdToJson(id);

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
                    if (!jsonRpcIdFromJson(j["id"], resp.id))
                    {
                        throw std::invalid_argument("JSON-RPC id must be a string, integer or null");
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

            static JsonRpcResponse success(const JsonRpcId& id, const json& result)
            {
                JsonRpcResponse resp;
                resp.id = id;
                resp.result = result;
                return resp;
            }

            static JsonRpcResponse failure(const JsonRpcId& id, const JsonRpcError& error)
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
