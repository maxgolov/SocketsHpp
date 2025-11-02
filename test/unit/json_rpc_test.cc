// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <gtest/gtest.h>
#include <SocketsHpp/http/common/json_rpc.h>
#include <nlohmann/json.hpp>

using namespace SocketsHpp::http::common;
using json = nlohmann::json;

// JSON-RPC Request Tests
TEST(JsonRpcTest, RequestWithStringId) {
    JsonRpcRequest req;
    req.id = "test-123";
    req.method = "tools/list";
    req.params = json{{"arg1", "value1"}};

    auto j = req.toJson();
    
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], "test-123");
    EXPECT_EQ(j["method"], "tools/list");
    EXPECT_EQ(j["params"]["arg1"], "value1");
}

TEST(JsonRpcTest, RequestWithIntId) {
    JsonRpcRequest req;
    req.id = 42;
    req.method = "prompts/get";
    req.params = json{{"name", "code-review"}};

    auto j = req.toJson();
    
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], 42);
    EXPECT_EQ(j["method"], "prompts/get");
}

TEST(JsonRpcTest, RequestWithNullId) {
    JsonRpcRequest req;
    req.id = nullptr;
    req.method = "ping";

    auto j = req.toJson();
    
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_TRUE(j["id"].is_null());
    EXPECT_EQ(j["method"], "ping");
}

TEST(JsonRpcTest, RequestWithNoParams) {
    JsonRpcRequest req;
    req.id = "1";
    req.method = "initialize";

    auto j = req.toJson();
    
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], "1");
    EXPECT_EQ(j["method"], "initialize");
    EXPECT_FALSE(j.contains("params"));
}

TEST(JsonRpcTest, RequestParseStringId) {
    std::string json_str = R"({
        "jsonrpc": "2.0",
        "id": "req-456",
        "method": "tools/call",
        "params": {"name": "search", "arguments": {"query": "test"}}
    })";

    auto req = JsonRpcRequest::parse(json_str);
    
    EXPECT_TRUE(std::holds_alternative<std::string>(req.id));
    EXPECT_EQ(std::get<std::string>(req.id), "req-456");
    EXPECT_EQ(req.method, "tools/call");
    EXPECT_EQ(req.params.value()["name"], "search");
    EXPECT_EQ(req.params.value()["arguments"]["query"], "test");
}

TEST(JsonRpcTest, RequestParseIntId) {
    std::string json_str = R"({
        "jsonrpc": "2.0",
        "id": 99,
        "method": "resources/read",
        "params": {"uri": "file://test.txt"}
    })";

    auto req = JsonRpcRequest::parse(json_str);
    
    EXPECT_TRUE(std::holds_alternative<int>(req.id));
    EXPECT_EQ(std::get<int>(req.id), 99);
    EXPECT_EQ(req.method, "resources/read");
}

TEST(JsonRpcTest, RequestParseNullId) {
    std::string json_str = R"({
        "jsonrpc": "2.0",
        "id": null,
        "method": "ping"
    })";

    auto req = JsonRpcRequest::parse(json_str);
    
    EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(req.id));
}

// JSON-RPC Response Tests
TEST(JsonRpcTest, ResponseSuccess) {
    JsonRpcResponse resp;
    resp.id = "resp-1";
    resp.result = {{"status", "ok"}, {"data", {1, 2, 3}}};

    auto j = resp.toJson();
    
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], "resp-1");
    EXPECT_EQ(j["result"]["status"], "ok");
    EXPECT_EQ(j["result"]["data"][0], 1);
    EXPECT_FALSE(j.contains("error"));
}

TEST(JsonRpcTest, ResponseError) {
    JsonRpcResponse resp;
    resp.id = 5;
    resp.error = JsonRpcError{-32600, "Invalid Request", json{{"detail", "Missing method"}}};

    auto j = resp.toJson();
    
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["id"], 5);
    EXPECT_FALSE(j.contains("result"));
    EXPECT_EQ(j["error"]["code"], -32600);
    EXPECT_EQ(j["error"]["message"], "Invalid Request");
    EXPECT_EQ(j["error"]["data"]["detail"], "Missing method");
}

TEST(JsonRpcTest, ResponseParseSuccess) {
    std::string json_str = R"({
        "jsonrpc": "2.0",
        "id": "test",
        "result": {"tools": [{"name": "search"}, {"name": "calculator"}]}
    })";

    auto resp = JsonRpcResponse::parse(json_str);
    
    EXPECT_EQ(std::get<std::string>(resp.id), "test");
    EXPECT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result.value()["tools"].size(), 2);
    EXPECT_EQ(resp.result.value()["tools"][0]["name"], "search");
}

TEST(JsonRpcTest, ResponseParseError) {
    std::string json_str = R"({
        "jsonrpc": "2.0",
        "id": 42,
        "error": {
            "code": -32601,
            "message": "Method not found",
            "data": {"method": "unknown/method"}
        }
    })";

    auto resp = JsonRpcResponse::parse(json_str);
    
    EXPECT_EQ(std::get<int>(resp.id), 42);
    EXPECT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, -32601);
    EXPECT_EQ(resp.error->message, "Method not found");
    EXPECT_EQ(resp.error->data.value()["method"], "unknown/method");
}

// JSON-RPC Notification Tests
TEST(JsonRpcTest, Notification) {
    JsonRpcNotification notif;
    notif.method = "notifications/message";
    notif.params = json{{"level", "info"}, {"message", "Task completed"}};

    auto j = notif.toJson();
    
    EXPECT_EQ(j["jsonrpc"], "2.0");
    EXPECT_EQ(j["method"], "notifications/message");
    EXPECT_EQ(j["params"]["level"], "info");
    EXPECT_FALSE(j.contains("id"));
}

TEST(JsonRpcTest, NotificationParse) {
    std::string json_str = R"({
        "jsonrpc": "2.0",
        "method": "notifications/progress",
        "params": {"progress": 75, "total": 100}
    })";

    auto notif = JsonRpcNotification::parse(json_str);
    
    EXPECT_EQ(notif.method, "notifications/progress");
    EXPECT_EQ(notif.params.value()["progress"], 75);
    EXPECT_EQ(notif.params.value()["total"], 100);
}

// JSON-RPC Error Code Tests
TEST(JsonRpcTest, StandardErrorCodes) {
    auto parseError = JsonRpcError::parseError("Invalid JSON");
    EXPECT_EQ(parseError.code, -32700);
    EXPECT_EQ(parseError.message, "Invalid JSON");
    EXPECT_FALSE(parseError.data.has_value());

    auto invalidRequest = JsonRpcError::invalidRequest("Missing id");
    EXPECT_EQ(invalidRequest.code, -32600);
    EXPECT_EQ(invalidRequest.message, "Missing id");

    auto methodNotFound = JsonRpcError::methodNotFound("unknown");
    EXPECT_EQ(methodNotFound.code, -32601);
    EXPECT_EQ(methodNotFound.message, "Method not found: unknown");

    auto invalidParams = JsonRpcError::invalidParams("name is required");
    EXPECT_EQ(invalidParams.code, -32602);
    EXPECT_EQ(invalidParams.message, "name is required");

    auto internalError = JsonRpcError::internalError("Database connection failed");
    EXPECT_EQ(internalError.code, -32603);
    EXPECT_EQ(internalError.message, "Database connection failed");
}

// TODO: Add MCP-specific error codes (resourceNotFound, toolNotFound, promptNotFound)
// TEST(JsonRpcTest, MCPErrorCodes) {
//     auto resourceNotFound = JsonRpcError::resourceNotFound("file://missing.txt");
//     EXPECT_EQ(resourceNotFound.code, -32002);
//     EXPECT_EQ(resourceNotFound.message, "Resource not found");
//
//     auto toolNotFound = JsonRpcError::toolNotFound("calculator");
//     EXPECT_EQ(toolNotFound.code, -32003);
//     EXPECT_EQ(toolNotFound.message, "Tool not found");
//
//     auto promptNotFound = JsonRpcError::promptNotFound("code-review");
//     EXPECT_EQ(promptNotFound.code, -32004);
//     EXPECT_EQ(promptNotFound.message, "Prompt not found");
// }

// Edge Cases
TEST(JsonRpcTest, EmptyParams) {
    JsonRpcRequest req;
    req.id = 1;
    req.method = "test";
    req.params = json::object();

    auto j = req.toJson();
    EXPECT_TRUE(j["params"].is_object());
    EXPECT_TRUE(j["params"].empty());
}

TEST(JsonRpcTest, ComplexNestedParams) {
    JsonRpcRequest req;
    req.id = "complex";
    req.method = "test";
    req.params = {
        {"array", {1, 2, 3}},
        {"object", {{"nested", {{"deep", "value"}}}}},
        {"null_value", nullptr},
        {"bool_value", true}
    };

    auto j = req.toJson();
    EXPECT_EQ(j["params"]["array"].size(), 3);
    EXPECT_EQ(j["params"]["object"]["nested"]["deep"], "value");
    EXPECT_TRUE(j["params"]["null_value"].is_null());
    EXPECT_TRUE(j["params"]["bool_value"].get<bool>());
}

TEST(JsonRpcTest, RoundTripRequest) {
    JsonRpcRequest original;
    original.id = "round-trip";
    original.method = "test/method";
    original.params = json{{"key", "value"}};

    auto json_str = original.toJson().dump();
    auto parsed = JsonRpcRequest::parse(json_str);

    EXPECT_EQ(std::get<std::string>(parsed.id), std::get<std::string>(original.id));
    EXPECT_EQ(parsed.method, original.method);
    EXPECT_EQ(parsed.params.value()["key"], original.params.value()["key"]);
}

TEST(JsonRpcTest, RoundTripResponse) {
    JsonRpcResponse original;
    original.id = 123;
    original.result = json{{"data", "test"}};

    auto json_str = original.toJson().dump();
    auto parsed = JsonRpcResponse::parse(json_str);

    EXPECT_EQ(std::get<int>(parsed.id), std::get<int>(original.id));
    EXPECT_EQ(parsed.result.value()["data"], original.result.value()["data"]);
}

TEST(JsonRpcTest, InvalidJsonParse) {
    std::string invalid_json = "not valid json";
    
    EXPECT_THROW({
        JsonRpcRequest::parse(invalid_json);
    }, std::exception);
}

TEST(JsonRpcTest, MissingRequiredFields) {
    std::string no_method = R"({"jsonrpc": "2.0", "id": 1})";
    
    EXPECT_THROW({
        JsonRpcRequest::parse(no_method);
    }, std::exception);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
