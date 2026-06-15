// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
//
// mcp_streamable_test.cc — Functional tests for Streamable HTTP transport (MCP 2025-03-26)
//
// Starts a real MCPServer on an ephemeral loopback port, sends raw HTTP/1.1
// requests via POSIX sockets, and validates:
//   1. initialize → JSON response + Mcp-Session-Id header
//   2. tools/list → JSON (Accept: application/json)
//   3. tools/list → SSE  (Accept: text/event-stream)
//   4. batch SSE  → two data: events
//   5. notification (no id) → 202 Accepted, empty body
//   6. batch JSON fallback  → application/json array
//   7. unknown method       → JSON-RPC -32601 error, HTTP 200
//   8. invalid JSON         → HTTP 400 with -32700
//   9. processMessage() STDIO regression (no HTTP server)

#include <gtest/gtest.h>
#include <SocketsHpp/mcp/server/mcp_server.h>
#include <SocketsHpp/mcp/common/mcp_config.h>
#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace SocketsHpp::mcp;
using namespace SocketsHpp::mcp::server;
using json = nlohmann::json;

// ── Minimal HTTP/1.1 client (no external deps) ────────────────────────────────

struct TestHttpResponse {
    int         status{0};
    std::string content_type;
    std::string session_id;
    std::string body;
};

static TestHttpResponse http_post(int port,
                               const std::string& body_str,
                               const std::string& session_id = "",
                               const std::string& accept     = "application/json")
{
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(port);
    if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        ::close(sock);
        return {};
    }

    // Build HTTP/1.1 request
    std::ostringstream req;
    req << "POST /mcp HTTP/1.1\r\n"
        << "Host: 127.0.0.1:" << port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Accept: " << accept << "\r\n"
        << "Content-Length: " << body_str.size() << "\r\n";
    if (!session_id.empty())
        req << "Mcp-Session-Id: " << session_id << "\r\n";
    req << "Connection: close\r\n\r\n" << body_str;

    std::string req_str = req.str();
    ::send(sock, req_str.c_str(), req_str.size(), 0);

    // Read full response
    std::string raw;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(sock, buf, sizeof(buf), 0)) > 0)
        raw.append(buf, n);
    ::close(sock);

    // Parse status line
    TestHttpResponse res;
    std::istringstream ss(raw);
    std::string line;
    std::getline(ss, line);  // HTTP/1.1 200 OK
    {
        std::istringstream ls(line);
        std::string proto; int code; ls >> proto >> code;
        res.status = code;
    }

    // Parse headers until blank line
    while (std::getline(ss, line) && line != "\r" && !line.empty()) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name  = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
            value.erase(value.begin());
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
            value.pop_back();
        std::string name_lower = name;
        for (auto& c : name_lower) c = std::tolower(c);
        if (name_lower == "content-type")  res.content_type = value;
        if (name_lower == "mcp-session-id") res.session_id  = value;
    }

    // Body is the rest
    std::ostringstream body_buf;
    body_buf << ss.rdbuf();
    res.body = body_buf.str();

    return res;
}

// Parse "data: {...}\n\n" SSE events
static std::vector<json> parse_sse(const std::string& sse_body)
{
    std::vector<json> result;
    std::istringstream ss(sse_body);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("data:", 0) == 0) {
            std::string payload = line.substr(5);
            while (!payload.empty() && (payload[0]==' '||payload[0]=='\t'))
                payload.erase(payload.begin());
            while (!payload.empty() && (payload.back()=='\r'||payload.back()=='\n'))
                payload.pop_back();
            if (!payload.empty()) {
                try { result.push_back(json::parse(payload)); } catch (...) {}
            }
        }
    }
    return result;
}

// ── Test fixture ──────────────────────────────────────────────────────────────

class StreamableHttpTest : public ::testing::Test
{
protected:
    std::unique_ptr<MCPServer> server_;
    std::thread                server_thread_;
    int                        port_{0};

    void SetUp() override
    {
        // Find an ephemeral port
        int probe = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family      = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port        = 0;
        ::bind(probe, (sockaddr*)&a, sizeof(a));
        socklen_t len = sizeof(a);
        ::getsockname(probe, (sockaddr*)&a, &len);
        port_ = ntohs(a.sin_port);
        ::close(probe);

        ServerConfig cfg;
        cfg.transport        = TransportType::HTTP_STREAMABLE;
        cfg.host             = "127.0.0.1";
        cfg.port             = port_;
        cfg.allowNonLoopback = false;

        server_ = std::make_unique<MCPServer>(cfg);

        server_->registerMethod("initialize", [](const json&) -> json {
            return {
                {"protocolVersion", "2025-03-26"},
                {"capabilities",    {{"tools", json::object()}}},
                {"serverInfo",      {{"name","test-streamable"},{"version","1.0"}}}
            };
        });

        server_->registerMethod("tools/list", [](const json&) -> json {
            return {{"tools", json::array({
                {{"name","add"},    {"description","Add two numbers"}},
                {{"name","reverse"},{"description","Reverse a string"}}
            })}};
        });

        server_->registerMethod("tools/call", [](const json& params) -> json {
            std::string name = params.value("name", "");
            json args = params.value("arguments", json::object());
            if (name == "add") {
                int a = args.value("a", 0), b = args.value("b", 0);
                return {{"content", json::array({{{"type","text"},{"text",std::to_string(a+b)}}})}};
            }
            if (name == "reverse") {
                std::string s = args.value("text", "");
                std::reverse(s.begin(), s.end());
                return {{"content", json::array({{{"type","text"},{"text",s}}})}};
            }
            throw std::runtime_error("Unknown tool: " + name);
        });

        server_->registerMethod("notifications/initialized", [](const json&) -> json {
            return json::object();
        });

        server_thread_ = std::thread([this]() {
            try { server_->listen(); } catch (...) {}
        });

        // Spin until the port is reachable
        for (int i = 0; i < 100; ++i) {
            int s = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr{};
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port        = htons(port_);
            bool ok = (::connect(s, (sockaddr*)&addr, sizeof(addr)) == 0);
            ::close(s);
            if (ok) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }

    void TearDown() override
    {
        if (server_) server_->stop();
        if (server_thread_.joinable()) server_thread_.join();
    }

    // Convenience: initialize and return session id
    std::string do_init()
    {
        auto r = http_post(port_,
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
        return r.session_id;
    }
};

// ── Tests ─────────────────────────────────────────────────────────────────────

// T1: initialize → JSON + session header
TEST_F(StreamableHttpTest, InitializeReturnsJsonAndSession)
{
    auto r = http_post(port_,
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");

    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.content_type.find("application/json"), std::string::npos);
    EXPECT_FALSE(r.session_id.empty()) << "Mcp-Session-Id must be set on initialize";

    auto d = json::parse(r.body);
    EXPECT_EQ(d["id"], 1);
    EXPECT_EQ(d["result"]["serverInfo"]["name"], "test-streamable");
    EXPECT_EQ(d["result"]["protocolVersion"], "2025-03-26");
}

// T2: tools/list → application/json
TEST_F(StreamableHttpTest, ToolsListReturnsJson)
{
    std::string session = do_init();
    auto r = http_post(port_,
        R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})", session);

    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.content_type.find("application/json"), std::string::npos);

    auto d = json::parse(r.body);
    EXPECT_EQ(d["id"], 2);
    EXPECT_EQ(d["result"]["tools"].size(), 2u);
    EXPECT_EQ(d["result"]["tools"][0]["name"], "add");
    EXPECT_EQ(d["result"]["tools"][1]["name"], "reverse");
}

// T3: tools/list → SSE when Accept includes text/event-stream
TEST_F(StreamableHttpTest, ToolsListReturnsSseWhenRequested)
{
    std::string session = do_init();
    auto r = http_post(port_,
        R"({"jsonrpc":"2.0","id":3,"method":"tools/list","params":{}})",
        session, "application/json, text/event-stream");

    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.content_type.find("text/event-stream"), std::string::npos)
        << "Content-Type should be text/event-stream";

    auto events = parse_sse(r.body);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0]["id"], 3);
    EXPECT_EQ(events[0]["result"]["tools"].size(), 2u);
}

// T4: batch → two SSE events
TEST_F(StreamableHttpTest, BatchRequestReturnsTwoSseEvents)
{
    std::string session = do_init();
    auto r = http_post(port_,
        R"([{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"add","arguments":{"a":3,"b":4}}},)"
        R"( {"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"reverse","arguments":{"text":"hello"}}}])",
        session, "application/json, text/event-stream");

    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.content_type.find("text/event-stream"), std::string::npos);

    auto events = parse_sse(r.body);
    ASSERT_EQ(events.size(), 2u);

    json ev4, ev5;
    for (auto& e : events) {
        if (e["id"] == 4) ev4 = e;
        if (e["id"] == 5) ev5 = e;
    }
    EXPECT_FALSE(ev4.is_null());
    EXPECT_FALSE(ev5.is_null());
    EXPECT_EQ(ev4["result"]["content"][0]["text"], "7");
    EXPECT_EQ(ev5["result"]["content"][0]["text"], "olleh");
}

// T5: notification (no id) → 202
TEST_F(StreamableHttpTest, NotificationReturns202)
{
    auto r = http_post(port_,
        R"({"jsonrpc":"2.0","method":"notifications/initialized","params":{}})");
    EXPECT_EQ(r.status, 202);
    EXPECT_TRUE(r.body.empty() || r.body.find_first_not_of(" \r\n\t") == std::string::npos)
        << "Notification body must be empty";
}

// T6: batch JSON fallback — no SSE in Accept → application/json array
TEST_F(StreamableHttpTest, BatchReturnsJsonArrayWhenNoSse)
{
    std::string session = do_init();
    auto r = http_post(port_,
        R"([{"jsonrpc":"2.0","id":6,"method":"tools/list","params":{}},)"
        R"( {"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"add","arguments":{"a":10,"b":20}}}])",
        session);  // default Accept: application/json

    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.content_type.find("application/json"), std::string::npos);

    auto arr = json::parse(r.body);
    ASSERT_TRUE(arr.is_array());
    ASSERT_EQ(arr.size(), 2u);

    json r6, r7;
    for (auto& e : arr) {
        if (e["id"] == 6) r6 = e;
        if (e["id"] == 7) r7 = e;
    }
    EXPECT_FALSE(r6.is_null());
    EXPECT_EQ(r6["result"]["tools"].size(), 2u);
    EXPECT_EQ(r7["result"]["content"][0]["text"], "30");
}

// T7: unknown method → JSON-RPC -32601, HTTP 200
TEST_F(StreamableHttpTest, UnknownMethodReturnsJsonRpcError)
{
    std::string session = do_init();
    auto r = http_post(port_,
        R"({"jsonrpc":"2.0","id":9,"method":"does/not/exist","params":{}})", session);

    EXPECT_EQ(r.status, 200) << "JSON-RPC errors must still return HTTP 200";
    auto d = json::parse(r.body);
    EXPECT_EQ(d["id"], 9);
    EXPECT_TRUE(d.contains("error"));
    EXPECT_EQ(d["error"]["code"], -32601);
}

// T8: invalid JSON → HTTP 400 + -32700
TEST_F(StreamableHttpTest, InvalidJsonReturnsHttp400)
{
    auto r = http_post(port_, "this is not json");
    EXPECT_EQ(r.status, 400);
    auto d = json::parse(r.body);
    EXPECT_EQ(d["error"]["code"], -32700);
}

// T9: processMessage() (STDIO) regression — no HTTP server started
TEST(StreamableHttpStdioRegression, ProcessMessageUnchanged)
{
    ServerConfig cfg;
    cfg.transport = TransportType::STDIO;
    cfg.port      = 0;

    MCPServer srv(cfg);
    srv.registerMethod("initialize", [](const json&) -> json {
        return {
            {"protocolVersion", "2025-03-26"},
            {"capabilities",    json::object()},
            {"serverInfo",      {{"name","stdio-test"},{"version","1"}}}
        };
    });
    srv.registerMethod("tools/list", [](const json&) -> json {
        return {{"tools", json::array({{{"name","ping"},{"description","A ping"}}})}};
    });

    std::string resp = srv.processMessage(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
    ASSERT_FALSE(resp.empty());
    auto d = json::parse(resp);
    EXPECT_EQ(d["id"], 1);
    EXPECT_EQ(d["result"]["serverInfo"]["name"], "stdio-test");

    std::string list_resp = srv.processMessage(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})");
    ASSERT_FALSE(list_resp.empty());
    auto dl = json::parse(list_resp);
    EXPECT_EQ(dl["result"]["tools"][0]["name"], "ping");
}

// ── New feature tests (protocol compliance) ────────────────────────────────────

// T10: ping — built-in, auto-registered, returns {}
TEST_F(StreamableHttpTest, PingReturnsEmptyObject)
{
    std::string session = do_init();
    auto r = http_post(port_,
        R"({"jsonrpc":"2.0","id":10,"method":"ping","params":{}})", session);
    EXPECT_EQ(r.status, 200);
    auto d = json::parse(r.body);
    EXPECT_EQ(d["id"], 10);
    EXPECT_TRUE(d["result"].is_object());
    EXPECT_TRUE(d["result"].empty()) << "ping must return {}";
}

// T11: protocol version negotiation — client sends 2025-03-26, server agrees
TEST_F(StreamableHttpTest, VersionNegotiationMatchesClientVersion)
{
    auto r = http_post(port_,
        R"({"jsonrpc":"2.0","id":11,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
    EXPECT_EQ(r.status, 200);
    auto d = json::parse(r.body);
    EXPECT_EQ(d["result"]["protocolVersion"], "2025-03-26");
}

// T12: protocol version negotiation — client sends older 2024-11-05
TEST_F(StreamableHttpTest, VersionNegotiationDowngradesForOlderClient)
{
    auto r = http_post(port_,
        R"({"jsonrpc":"2.0","id":12,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"old-client","version":"1"}}})");
    EXPECT_EQ(r.status, 200);
    auto d = json::parse(r.body);
    // Server must not respond with a version higher than the client's
    std::string negotiated = d["result"]["protocolVersion"].get<std::string>();
    EXPECT_LE(negotiated, std::string("2024-11-05"));
}

// T13: protocol version negotiation — unsupported client version → -32002
TEST_F(StreamableHttpTest, VersionNegotiationRejectsUnknownVersion)
{
    auto r = http_post(port_,
        R"({"jsonrpc":"2.0","id":13,"method":"initialize","params":{"protocolVersion":"2000-01-01","capabilities":{},"clientInfo":{"name":"ancient","version":"1"}}})");
    EXPECT_EQ(r.status, 200) << "JSON-RPC errors always return HTTP 200";
    auto d = json::parse(r.body);
    EXPECT_TRUE(d.contains("error"));
    EXPECT_EQ(d["error"]["code"], -32002) << "Unsupported protocol version must be -32002";
}

// T14: GET /health → discovery JSON with server name + version
TEST_F(StreamableHttpTest, HealthEndpointReturnsServerInfo)
{
    // HTTP GET via raw socket
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(port_);
    ASSERT_EQ(::connect(sock, (sockaddr*)&addr, sizeof(addr)), 0);

    std::string req_str = "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    ::send(sock, req_str.c_str(), req_str.size(), 0);

    std::string raw;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(sock, buf, sizeof(buf), 0)) > 0) raw.append(buf, n);
    ::close(sock);

    // Parse status
    int status = 0;
    {
        std::istringstream ss(raw);
        std::string line; std::getline(ss, line);
        std::istringstream ls(line); std::string p; ls >> p >> status;
    }
    EXPECT_EQ(status, 200);

    // Body is after the blank line
    auto sep = raw.find("\r\n\r\n");
    ASSERT_NE(sep, std::string::npos);
    auto body = raw.substr(sep + 4);
    auto d = json::parse(body);
    EXPECT_TRUE(d.contains("name"));
    EXPECT_TRUE(d.contains("protocolVersion"));
}

// T15: logging/setLevel — built-in, auto-registered
TEST_F(StreamableHttpTest, LoggingSetLevelAccepted)
{
    std::string session = do_init();
    // Send logging/setLevel as a notification (no id)
    auto r = http_post(port_,
        R"({"jsonrpc":"2.0","method":"logging/setLevel","params":{"level":"info"}})", session);
    // Notification → 202
    EXPECT_EQ(r.status, 202);
}

// T16: push_progress / push_log — verify helper exists and can be called
//      (Integration: server pushes a progress event via SSE, client receives it on GET stream)
//      This test verifies the API compiles and the method returns false for unknown sessions.
TEST(StreamableHttpApiCompiles, PushProgressAndLogReturnFalseForUnknownSession)
{
    ServerConfig cfg;
    cfg.transport = TransportType::STDIO;
    cfg.port      = 0;
    MCPServer srv(cfg);

    // push_progress to unknown session — must return false, not crash
    bool ok = srv.push_progress("no-such-session", json("token-1"), 0.5, 1.0, "halfway");
    EXPECT_FALSE(ok);

    // push_log to unknown session — must return false, not crash
    bool ok2 = srv.push_log("no-such-session", "info", "test-logger", json("hello"));
    EXPECT_FALSE(ok2);
}

// T17: get_client_capabilities — returns null for unknown session
TEST(StreamableHttpApiCompiles, GetClientCapsNullForUnknownSession)
{
    ServerConfig cfg;
    cfg.transport = TransportType::STDIO;
    cfg.port      = 0;
    MCPServer srv(cfg);
    auto caps = srv.get_client_capabilities("no-such-session");
    EXPECT_TRUE(caps.is_null() || caps.is_discarded() || caps.empty())
        << "Unknown session should return null/empty capabilities";
}

// T18: registerCancellable — handler can be called without cancellation
TEST(StreamableHttpApiCompiles, CancellableHandlerExecutes)
{
    ServerConfig cfg;
    cfg.transport = TransportType::STDIO;
    cfg.port      = 0;
    MCPServer srv(cfg);

    srv.registerCancellable("echo", [](const json& p, std::shared_ptr<std::atomic<bool>> cancel) -> json {
        if (cancel->load()) throw std::runtime_error("cancelled");
        return {{"echoed", p.value("text", "")}};
    });

    std::string resp = srv.processMessage(
        R"({"jsonrpc":"2.0","id":1,"method":"echo","params":{"text":"hello"}})");
    ASSERT_FALSE(resp.empty());
    auto d = json::parse(resp);
    EXPECT_EQ(d["result"]["echoed"], "hello");
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

