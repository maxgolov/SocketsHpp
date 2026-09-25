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
            /// @brief Error code (see the factory functions for the standard codes).
            int code = 0;
            /// @brief Short error description.
            std::string message;
            /// @brief Optional additional error information ("data" member).
            std::optional<json> data;

            /// @brief Build the JSON error object {"code", "message"[, "data"]}.
            /// @return The error object.
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

            /// @brief Read an error object.
            /// @param j JSON object with integer "code", string "message" and optional "data".
            /// @return The parsed error.
            /// @throws nlohmann::json::exception if "code" or "message" is missing or has the
            ///         wrong type.
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
            /// @brief -32700 Parse error (invalid JSON).
            /// @param message Error message.
            /// @return The error.
            static JsonRpcError parseError(const std::string& message = "Parse error")
            {
                return {-32700, message, std::nullopt};
            }

            /// @brief -32600 Invalid Request (not a valid request object).
            /// @param message Error message.
            /// @return The error.
            static JsonRpcError invalidRequest(const std::string& message = "Invalid Request")
            {
                return {-32600, message, std::nullopt};
            }

            /// @brief -32601 Method not found; message is "Method not found: <method>".
            /// @param method Name of the unknown method.
            /// @return The error.
            static JsonRpcError methodNotFound(const std::string& method)
            {
                return {-32601, "Method not found: " + method, std::nullopt};
            }

            /// @brief -32602 Invalid params.
            /// @param message Error message.
            /// @return The error.
            static JsonRpcError invalidParams(const std::string& message = "Invalid params")
            {
                return {-32602, message, std::nullopt};
            }

            /// @brief -32603 Internal error.
            /// @param message Error message.
            /// @return The error.
            static JsonRpcError internalError(const std::string& message = "Internal error")
            {
                return {-32603, message, std::nullopt};
            }

            // MCP-specific error codes (-32000 to -32099 reserved for implementation-defined errors)
            /// @brief Error with a caller-chosen code (-32000 to -32099 are reserved for
            /// implementation-defined server errors; the code is not checked).
            /// @param code Error code.
            /// @param message Error message.
            /// @return The error.
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
        /// @param id Id to test.
        /// @return false only for std::monostate (absent id).
        inline bool jsonRpcIdPresent(const JsonRpcId& id)
        {
            return !std::holds_alternative<std::monostate>(id);
        }

        /// @brief Convert a JSON value to a JsonRpcId.
        /// @param j JSON value of an "id" member.
        /// @param out Receives the id on success; unchanged on failure.
        /// @return false if the value is not a valid JSON-RPC id (string, integer, null);
        ///         fractional numbers, booleans, objects, arrays and unsigned integers
        ///         above INT64_MAX are rejected.
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
        /// @param id Id to convert.
        /// @return JSON string, integer or null.
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
            /// @brief Protocol version (default "2.0"; not validated by parse()).
            std::string jsonrpc = "2.0";
            /// @brief Request id: absent (monostate, i.e. a notification), string, integer, or null.
            JsonRpcId id;
            /// @brief Method name.
            std::string method;
            /// @brief Optional "params" value (any JSON; object or array per the spec).
            std::optional<json> params;

            /// @brief Build the request object. "id" is omitted when absent and "params"
            /// when not set.
            /// @return The request object.
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

            /// @brief Serialize toJson() as compact JSON text.
            /// @return JSON text.
            std::string serialize() const
            {
                return toJson().dump();
            }

            /// @brief Parse a request (or notification) from JSON text.
            /// @param jsonStr JSON object text; "jsonrpc" defaults to "2.0" when missing.
            /// @return The request; id stays std::monostate when "id" is absent.
            /// @throws nlohmann::json::exception on invalid JSON, a non-object value, or a
            ///         missing/non-string "method".
            /// @throws std::invalid_argument if "id" is not a string, integer or null.
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
            /// @return jsonRpcIdPresent(id).
            bool hasId() const
            {
                return jsonRpcIdPresent(id);
            }
        };

        /// @brief JSON-RPC 2.0 notification (request without ID)
        struct JsonRpcNotification
        {
            /// @brief Protocol version (default "2.0"; not validated by parse()).
            std::string jsonrpc = "2.0";
            /// @brief Method name.
            std::string method;
            /// @brief Optional "params" value.
            std::optional<json> params;

            /// @brief Build the notification object (no "id"; "params" only when set).
            /// @return The notification object.
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

            /// @brief Serialize toJson() as compact JSON text.
            /// @return JSON text.
            std::string serialize() const
            {
                return toJson().dump();
            }

            /// @brief Parse a notification from JSON text. An "id" member, if present, is
            /// ignored (use JsonRpcRequest::parse() to distinguish requests).
            /// @param jsonStr JSON object text.
            /// @return The notification.
            /// @throws nlohmann::json::exception on invalid JSON, a non-object value, or a
            ///         missing/non-string "method".
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
            /// @brief Protocol version (default "2.0"; not validated by parse()).
            std::string jsonrpc = "2.0";
            /// @brief Id of the request being answered; absent (monostate) serializes as null.
            JsonRpcId id;
            /// @brief Result on success (ignored by toJson() when error is set).
            std::optional<json> result;
            /// @brief Error on failure; takes precedence over result.
            std::optional<JsonRpcError> error;

            /// @brief Build the response object. Always includes "id"; includes "error" if
            /// set, else "result" (null when unset).
            /// @return The response object.
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

            /// @brief Serialize toJson() as compact JSON text.
            /// @return JSON text.
            std::string serialize() const
            {
                return toJson().dump();
            }

            /// @brief Parse a response from JSON text. "error" wins over "result"; one of
            /// them is required.
            /// @param jsonStr JSON object text.
            /// @return The response; id stays std::monostate when "id" is absent.
            /// @throws nlohmann::json::exception on invalid JSON, a non-object value, or a
            ///         malformed "error" object.
            /// @throws std::invalid_argument if "id" is not a string, integer or null, or
            ///         if the response has neither "result" nor "error".
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
                else
                {
                    throw std::invalid_argument("JSON-RPC response must contain \"result\" or \"error\"");
                }

                return resp;
            }

            /// @brief Make a success response.
            /// @param id Id of the request being answered.
            /// @param result Result value.
            /// @return The response.
            static JsonRpcResponse success(const JsonRpcId& id, const json& result)
            {
                JsonRpcResponse resp;
                resp.id = id;
                resp.result = result;
                return resp;
            }

            /// @brief Make an error response.
            /// @param id Id of the request being answered (monostate/null if unknown).
            /// @param error Error to report.
            /// @return The response.
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
