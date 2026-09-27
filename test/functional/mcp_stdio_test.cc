// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
//
// MCP stdio transport (StdioServerTransport, MCPClient over STDIO), utils::ChildProcess,
// and MCPClient behaviour shared with HTTP: protocol version check, connection retries,
// MCP-Protocol-Version header, early end of SSE POST responses.
//
// The client tests launch this test executable itself as the MCP server
// ("--mcp-stdio-child <mode>", handled in main()).

#include <gtest/gtest.h>
#include <sockets.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <csignal>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

using namespace SocketsHpp::mcp;
using SocketsHpp::http::common::JsonRpcError;
using SocketsHpp::mcp::client::MCPClient;
using SocketsHpp::mcp::server::MCPServer;
using SocketsHpp::mcp::server::StdioServerTransport;
using SocketsHpp::utils::NativeHandle;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
    // ── helpers ────────────────────────────────────────────────────────────

    std::string selfPath()
    {
#ifdef _WIN32
        char buf[MAX_PATH * 4];
        const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
        return std::string(buf, n);
#else
        return MCP_STDIO_TEST_SELF;
#endif
    }

    ClientConfig childConfig(const std::string& mode)
    {
        ClientConfig cfg;
        cfg.transport = TransportType::STDIO;
        cfg.stdio.command = selfPath();
        cfg.stdio.args = {"--mcp-stdio-child", mode};
        cfg.readTimeoutSeconds = 30;
        return cfg;
    }

    template <typename Pred>
    bool waitFor(Pred pred, int timeoutMs = 10000)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < until)
        {
            if (pred())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return pred();
    }

    // True once pid is gone and reaped (no zombie left behind).
    bool processReaped(std::int64_t pid)
    {
#ifdef _WIN32
        HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (h == nullptr)
            return true;
        const bool exited = ::WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
        ::CloseHandle(h);
        return exited;
#else
        return ::kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH;
#endif
    }

    json textResult(const std::string& text)
    {
        return {{"content", json::array({{{"type", "text"}, {"text", text}}})}};
    }

    std::string resultText(const json& result) { return result.at("content").at(0).at("text").get<std::string>(); }

    // ── child process modes ────────────────────────────────────────────────

    std::atomic<bool> g_lastSlowCancelled{false};

    void registerTestTools(MCPServer& server, StdioServerTransport& stdio)
    {
        server.registerMethod("initialize", [](const json&) -> json {
            return {{"capabilities", {{"tools", json::object()}, {"logging", json::object()}}},
                    {"serverInfo", {{"name", "stdio-test"}, {"version", "1"}}}};
        });
        server.registerMethod("tools/list", [](const json&) -> json {
            return {{"tools", json::array({{{"name", "echo"}}, {{"name", "slow"}}})}};
        });
        server.registerCancellable("tools/call", [&stdio](const json& p, std::shared_ptr<std::atomic<bool>> cancel) -> json {
            const std::string name = p.value("name", "");
            const json args = p.value("arguments", json::object());
            if (name == "echo")
                return textResult(args.value("text", ""));
            if (name == "slow")
            {
                const int ms = args.value("ms", 5000);
                g_lastSlowCancelled = false;
                const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
                while (std::chrono::steady_clock::now() < until)
                {
                    if (cancel->load())
                    {
                        g_lastSlowCancelled = true;
                        throw std::runtime_error("cancelled");
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                return textResult("slept");
            }
            if (name == "slow_status")
                return {{"cancelled", g_lastSlowCancelled.load()}};
            if (name == "notify")
            {
                stdio.log("error", "test", args.value("text", ""));
                stdio.log("debug", "test", "suppressed by the default level");
                stdio.notify("notifications/custom", {{"x", 1}});
                return textResult("notified");
            }
            if (name == "stderr")
            {
                std::cerr << args.value("text", "") << std::endl;
                return textResult("written");
            }
            if (name == "env")
            {
                const char* v = std::getenv(args.value("name", "").c_str());
                return {{"value", v ? json(v) : json()}};
            }
            if (name == "cwd")
                return {{"cwd", fs::current_path().u8string()}};
            if (name == "exit")
            {
                std::fflush(nullptr);
                std::_Exit(3);
            }
            throw JsonRpcError::invalidParams("unknown tool " + name);
        });
    }

    int runChild(const std::string& mode)
    {
        if (mode == "server")
        {
            ServerConfig cfg;
            MCPServer server(cfg);
            StdioServerTransport stdio(server);
            registerTestTools(server, stdio);
            stdio.runStdio();
            return 0;
        }
        if (mode == "bad-version")
        {
            std::string line;
            while (std::getline(std::cin, line))
            {
                json m = json::parse(line, nullptr, false);
                if (m.is_object() && m.value("method", "") == "initialize")
                {
                    json r = {{"jsonrpc", "2.0"},
                              {"id", m["id"]},
                              {"result", {{"protocolVersion", "1999-01-01"},
                                          {"capabilities", json::object()},
                                          {"serverInfo", {{"name", "old"}, {"version", "0"}}}}}};
                    std::cout << r.dump() << std::endl;
                }
            }
            return 0;
        }
        if (mode == "stubborn")
        {
#ifndef _WIN32
            std::signal(SIGTERM, SIG_IGN);
#endif
            for (;;)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return 2;
    }

    // ── in-process pipes for StdioServerTransport::run(handle, handle) ───────

    struct TestPipe
    {
        NativeHandle read = SocketsHpp::utils::invalidNativeHandle();
        NativeHandle write = SocketsHpp::utils::invalidNativeHandle();
        TestPipe()
        {
#ifdef _WIN32
            HANDLE r = nullptr, w = nullptr;
            EXPECT_TRUE(::CreatePipe(&r, &w, nullptr, 0));
            read = r;
            write = w;
#else
            int fds[2];
            EXPECT_EQ(::pipe(fds), 0);
            read = fds[0];
            write = fds[1];
#endif
        }
        ~TestPipe()
        {
            closeRead();
            closeWrite();
        }
        void closeRead() { SocketsHpp::utils::detail::closeNativeHandle(read); }
        void closeWrite() { SocketsHpp::utils::detail::closeNativeHandle(write); }
        void send(const std::string& line)
        {
            const std::string data = line + "\n";
            ASSERT_TRUE(SocketsHpp::utils::writeAll(write, data.data(), data.size()));
        }
    };

    // Collects the lines the transport writes to its output pipe.
    class OutputCollector
    {
    public:
        explicit OutputCollector(NativeHandle h)
        {
            m_reader.open(h);
            m_thread = std::thread([this] {
                std::string buf;
                char chunk[4096];
                long n;
                while ((n = m_reader.read(chunk, sizeof(chunk))) > 0)
                {
                    buf.append(chunk, static_cast<size_t>(n));
                    size_t nl;
                    while ((nl = buf.find('\n')) != std::string::npos)
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_lines.push_back(buf.substr(0, nl));
                        buf.erase(0, nl + 1);
                        m_cv.notify_all();
                    }
                }
            });
        }
        ~OutputCollector() { stop(); }
        void stop()
        {
            m_reader.interrupt();
            if (m_thread.joinable())
                m_thread.join();
        }
        // Wait for a message matching pred; returns it (null on timeout).
        template <typename Pred>
        json waitMessage(Pred pred, int timeoutMs = 10000)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            json found;
            m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
                for (const auto& l : m_lines)
                {
                    json m = json::parse(l, nullptr, false);
                    if (pred(m))
                    {
                        found = m;
                        return true;
                    }
                }
                return false;
            });
            return found;
        }
        json waitResponse(int id, int timeoutMs = 10000)
        {
            return waitMessage([id](const json& m) { return m.is_object() && m.value("id", json()) == json(id); },
                               timeoutMs);
        }
        std::vector<std::string> lines()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_lines;
        }

    private:
        SocketsHpp::utils::PipeReader m_reader;
        std::thread m_thread;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::vector<std::string> m_lines;
    };

    const char* kInit =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{"roots":{}},"clientInfo":{"name":"t","version":"1"}}})";
}  // namespace

// ── StdioServerTransport ────────────────────────────────────────────────────

TEST(McpStdioServerTest, StreamRoundTrip)
{
    MCPServer server(ServerConfig{});
    StdioServerTransport stdio(server);
    registerTestTools(server, stdio);

    std::istringstream in(std::string(kInit) + "\n" +
                          R"({"jsonrpc":"2.0","method":"notifications/initialized"})" "\n"
                          "\n"
                          R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})" "\r\n"
                          "{not json\n" +
                          R"({"jsonrpc":"2.0","id":"three","method":"tools/call","params":{"name":"echo","arguments":{"text":"hi"}}})" "\n" +
                          R"({"jsonrpc":"2.0","id":4,"method":"nope"})");  // last line without newline
    std::ostringstream out;
    stdio.run(in, out);
    EXPECT_FALSE(stdio.isRunning());

    std::map<std::string, json> byId;
    int parseErrors = 0;
    std::istringstream lines(out.str());
    std::string line;
    int count = 0;
    while (std::getline(lines, line))
    {
        ++count;
        json m = json::parse(line);  // every output line is one JSON message
        if (m["id"].is_null())
        {
            EXPECT_EQ(m["error"]["code"], -32700);
            ++parseErrors;
        }
        else
        {
            byId[m["id"].dump()] = m;
        }
    }
    EXPECT_EQ(count, 5);  // 4 responses + 1 parse error; nothing for the notification
    EXPECT_EQ(parseErrors, 1);
    EXPECT_EQ(byId["1"]["result"]["serverInfo"]["name"], "stdio-test");
    EXPECT_EQ(byId["2"]["result"]["tools"].size(), 2u);
    EXPECT_EQ(resultText(byId["\"three\""]["result"]), "hi");
    EXPECT_EQ(byId["4"]["error"]["code"], -32601);
    EXPECT_EQ(server.get_client_capabilities("")["roots"], json::object());
}

TEST(McpStdioServerTest, OverlongLineIsRejected)
{
    MCPServer server(ServerConfig{});
    StdioServerTransport::Options opts;
    opts.maxMessageSize = 64;
    StdioServerTransport stdio(server, opts);

    std::istringstream in(std::string(R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{"pad":")") +
                          std::string(100, 'x') + "\"}}\n" + R"({"jsonrpc":"2.0","id":2,"method":"ping"})" "\n");
    std::ostringstream out;
    stdio.run(in, out);
    std::istringstream lines(out.str());
    std::string first, second;
    ASSERT_TRUE(std::getline(lines, first));
    ASSERT_TRUE(std::getline(lines, second));
    EXPECT_EQ(json::parse(first)["error"]["code"], -32600);
    EXPECT_EQ(json::parse(second)["id"], 2);
}

TEST(McpStdioServerTest, OverlongLineIsRejectedOnPipes)
{
    MCPServer server(ServerConfig{});
    StdioServerTransport::Options opts;
    opts.maxMessageSize = 1000;
    StdioServerTransport stdio(server, opts);
    TestPipe input, output;
    OutputCollector collector(output.read);
    std::thread runner([&] { stdio.run(input.read, output.write); });

    // Written in pieces so the limit is hit before the newline arrives.
    const std::string big(300000, 'x');
    std::thread writer([&] {
        input.send(std::string(R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{"pad":")") + big + "\"}}");
        input.send(R"({"jsonrpc":"2.0","id":2,"method":"ping"})");
        input.closeWrite();
    });
    writer.join();
    runner.join();
    json tooLarge = collector.waitMessage([](const json& m) { return m.is_object() && m["id"].is_null(); });
    EXPECT_EQ(tooLarge["error"]["code"], -32600);
    EXPECT_TRUE(collector.waitResponse(2).contains("result"));
}

TEST(McpStdioServerTest, ConcurrentRequestsCancellationAndNotifications)
{
    MCPServer server(ServerConfig{});
    StdioServerTransport stdio(server);
    registerTestTools(server, stdio);
    TestPipe input, output;
    OutputCollector collector(output.read);
    std::thread runner([&] { stdio.run(input.read, output.write); });
    ASSERT_TRUE(waitFor([&] { return stdio.isRunning(); }));

    input.send(kInit);
    ASSERT_TRUE(collector.waitResponse(1).contains("result"));

    // A slow request does not block a ping sent after it.
    input.send(R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"slow","arguments":{"ms":20000}}})");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    input.send(R"({"jsonrpc":"2.0","id":11,"method":"ping"})");
    ASSERT_TRUE(collector.waitResponse(11, 5000).contains("result"));

    // Cancel it: the handler stops and no response is written for id 10.
    input.send(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":10}})");
    ASSERT_TRUE(waitFor([] { return g_lastSlowCancelled.load(); }, 5000));

    // Server-initiated notifications; log() honours logging/setLevel.
    input.send(R"({"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"notify","arguments":{"text":"boom"}}})");
    ASSERT_TRUE(collector.waitResponse(12).contains("result"));
    EXPECT_TRUE(stdio.notify("notifications/tools/list_changed"));
    json custom = collector.waitMessage([](const json& m) { return m.is_object() && m.value("method", "") == "notifications/custom"; });
    EXPECT_EQ(custom["params"]["x"], 1);
    json log = collector.waitMessage([](const json& m) { return m.is_object() && m.value("method", "") == "notifications/message"; });
    EXPECT_EQ(log["params"]["data"], "boom");
    EXPECT_EQ(log["params"]["level"], "error");

    EXPECT_EQ(stdio.logLevel(), 3);
    input.send(R"({"jsonrpc":"2.0","id":13,"method":"logging/setLevel","params":{"level":"debug"}})");
    ASSERT_TRUE(collector.waitResponse(13).contains("result"));
    EXPECT_EQ(stdio.logLevel(), 0);
    EXPECT_TRUE(stdio.log("debug", "t", "now visible"));
    EXPECT_TRUE(stdio.progress("tok", 1, 2, "half"));
    json progress = collector.waitMessage([](const json& m) { return m.is_object() && m.value("method", "") == "notifications/progress"; });
    EXPECT_EQ(progress["params"]["progressToken"], "tok");
    EXPECT_EQ(progress["params"]["total"], 2.0);

    // End of input: run() returns after pending work.
    input.closeWrite();
    runner.join();
    EXPECT_FALSE(stdio.isRunning());
    EXPECT_FALSE(stdio.notify("notifications/late"));

    for (const auto& l : collector.lines())
        EXPECT_NE(json::parse(l).value("id", json()), json(10)) << "cancelled request must not be answered";
}

TEST(McpStdioServerTest, StopInterruptsReadAndCancelsRunningRequests)
{
    MCPServer server(ServerConfig{});
    StdioServerTransport stdio(server);
    registerTestTools(server, stdio);
    TestPipe input, output;
    OutputCollector collector(output.read);
    std::thread runner([&] { stdio.run(input.read, output.write); });
    ASSERT_TRUE(waitFor([&] { return stdio.isRunning(); }));

    input.send(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"slow","arguments":{"ms":30000}}})");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto start = std::chrono::steady_clock::now();
    stdio.stop();
    runner.join();  // input is still open: stop() must interrupt the blocked read
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
    EXPECT_TRUE(g_lastSlowCancelled.load());
    EXPECT_FALSE(stdio.isRunning());
}

TEST(McpStdioServerTest, CancelRemovesQueuedRequest)
{
    MCPServer server(ServerConfig{});
    StdioServerTransport::Options opts;
    opts.maxConcurrentRequests = 1;
    StdioServerTransport stdio(server, opts);
    std::atomic<int> quickCalls{0};
    server.registerCancellable("block", [](const json&, std::shared_ptr<std::atomic<bool>> cancel) -> json {
        while (!cancel->load())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        throw std::runtime_error("cancelled");
    });
    server.registerMethod("quick", [&quickCalls](const json&) -> json {
        ++quickCalls;
        return json::object();
    });
    TestPipe input, output;
    OutputCollector collector(output.read);
    std::thread runner([&] { stdio.run(input.read, output.write); });

    input.send(R"({"jsonrpc":"2.0","id":1,"method":"block"})");  // occupies the only worker
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    input.send(R"({"jsonrpc":"2.0","id":2,"method":"quick"})");  // queued
    input.send(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":2}})");
    input.send(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":1}})");
    input.send(R"({"jsonrpc":"2.0","id":3,"method":"quick"})");
    ASSERT_TRUE(collector.waitResponse(3).contains("result"));
    EXPECT_EQ(quickCalls.load(), 1);
    input.closeWrite();
    runner.join();
    for (const auto& l : collector.lines())
    {
        const json id = json::parse(l).value("id", json());
        EXPECT_NE(id, json(1));
        EXPECT_NE(id, json(2));
    }
}

// ── StdioConfig ─────────────────────────────────────────────────────────────

TEST(McpStdioConfigTest, EnvFileMergedWithEnv)
{
    const fs::path file = fs::temp_directory_path() / ("mcp_stdio_env_" + std::to_string(std::rand()) + ".env");
    {
        std::ofstream out(file);
        out << "# comment\n\nexport A=1\nB = two words \nC=\"quoted\\nvalue\"\nD='single'\nE=from-file\nbad line\n";
    }
    StdioConfig cfg;
    cfg.envFile = file.string();
    cfg.env["E"] = "from-env";
    auto env = cfg.resolvedEnvironment();
    fs::remove(file);
    EXPECT_EQ(env["A"], "1");
    EXPECT_EQ(env["B"], "two words");
    EXPECT_EQ(env["C"], "quoted\nvalue");
    EXPECT_EQ(env["D"], "single");
    EXPECT_EQ(env["E"], "from-env");
    EXPECT_EQ(env.count("bad line"), 0u);

    cfg.envFile = (fs::temp_directory_path() / "definitely-missing-mcp.env").string();
    EXPECT_THROW(cfg.resolvedEnvironment(), std::runtime_error);
}

// ── MCPClient over STDIO (spawns this executable) ───────────────────────────

TEST(McpStdioClientTest, InitializeListCallNotifyAndDisconnect)
{
    MCPClient client;
    std::mutex m;
    std::vector<json> logs;
    std::atomic<int> custom{0};
    std::vector<std::pair<bool, std::string>> statuses;
    client.onNotification("notifications/message", [&](const json& p) {
        std::lock_guard<std::mutex> lock(m);
        logs.push_back(p);
    });
    client.onNotification("notifications/custom", [&](const json&) { ++custom; });
    client.onStatus([&](bool ok, const std::string& msg) {
        std::lock_guard<std::mutex> lock(m);
        statuses.emplace_back(ok, msg);
    });

    ASSERT_TRUE(client.connect(childConfig("server")));
    EXPECT_TRUE(client.isConnected());
    const std::int64_t pid = client.serverProcessId();
    EXPECT_GT(pid, 0);

    client.setClientCapabilities({{"roots", {{"listChanged", true}}}});
    client.setClientInfo({{"name", "stdio-client-test"}, {"version", "2"}});
    json init = client.initialize();
    EXPECT_EQ(init["serverInfo"]["name"], "stdio-test");
    EXPECT_EQ(client.protocolVersion(), init["protocolVersion"]);
    EXPECT_NE(client.protocolVersion(), "");
    EXPECT_TRUE(client.sessionId().empty());
    EXPECT_TRUE(client.getServerCapabilities().contains("tools"));

    auto tools = client.listTools();
    ASSERT_EQ(tools.size(), 2u);
    EXPECT_EQ(resultText(client.callTool("echo", {{"text", "hello"}})), "hello");
    EXPECT_EQ(client.ping(), json::object());

    // Concurrent requests are multiplexed on the pipe.
    std::vector<std::thread> threads;
    std::atomic<int> ok{0};
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&, i] {
            if (resultText(client.callTool("echo", {{"text", std::to_string(i)}})) == std::to_string(i))
                ++ok;
        });
    }
    for (auto& t : threads)
        t.join();
    EXPECT_EQ(ok.load(), 8);

    try
    {
        client.callTool("missing");
        ADD_FAILURE() << "expected JsonRpcError";
    }
    catch (const JsonRpcError& e)
    {
        EXPECT_EQ(e.code, -32602);
    }

    client.callTool("notify", {{"text", "boom"}});
    ASSERT_TRUE(waitFor([&] {
        std::lock_guard<std::mutex> lock(m);
        return !logs.empty() && custom.load() == 1;
    }));
    {
        std::lock_guard<std::mutex> lock(m);
        EXPECT_EQ(logs.size(), 1u);  // the debug message is below the default level
        EXPECT_EQ(logs[0]["data"], "boom");
        ASSERT_FALSE(statuses.empty());
        EXPECT_TRUE(statuses[0].first);
    }

    const auto start = std::chrono::steady_clock::now();
    client.disconnect();  // closes stdin: the child's run loop ends and it exits
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
    EXPECT_FALSE(client.isConnected());
    EXPECT_EQ(client.serverProcessId(), -1);
    EXPECT_TRUE(processReaped(pid));
    EXPECT_THROW(client.ping(), std::runtime_error);
}

TEST(McpStdioClientTest, CancelTokenCancelsServerRequest)
{
    MCPClient client;
    ASSERT_TRUE(client.connect(childConfig("server")));
    client.initialize();

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    std::thread canceller([cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        *cancel = true;
    });
    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(client.callTool("slow", {{"ms", 20000}}, cancel), std::runtime_error);
    canceller.join();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));

    // The server saw notifications/cancelled and stopped the handler.
    ASSERT_TRUE(waitFor([&] { return client.callTool("slow_status")["cancelled"] == true; }, 5000));
    EXPECT_EQ(resultText(client.callTool("echo", {{"text", "still fine"}})), "still fine");
}

TEST(McpStdioClientTest, ReadTimeoutCancelsRequest)
{
    ClientConfig cfg = childConfig("server");
    cfg.readTimeoutSeconds = 1;
    MCPClient client;
    ASSERT_TRUE(client.connect(cfg));
    client.initialize();
    const auto start = std::chrono::steady_clock::now();
    try
    {
        client.callTool("slow", {{"ms", 20000}});
        ADD_FAILURE() << "expected a timeout";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("timed out"), std::string::npos) << e.what();
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
    ASSERT_TRUE(waitFor([&] { return client.callTool("slow_status")["cancelled"] == true; }, 5000));
}

TEST(McpStdioClientTest, ServerExitFailsPendingRequest)
{
    MCPClient client;
    std::atomic<bool> lost{false};
    client.onStatus([&](bool connected, const std::string&) {
        if (!connected)
            lost = true;
    });
    ASSERT_TRUE(client.connect(childConfig("server")));
    client.initialize();
    const std::int64_t pid = client.serverProcessId();
    try
    {
        client.callTool("exit");
        ADD_FAILURE() << "expected an error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("exited"), std::string::npos) << e.what();
    }
    ASSERT_TRUE(waitFor([&] { return lost.load(); }));
    EXPECT_FALSE(client.isConnected());
    EXPECT_THROW(client.ping(), std::runtime_error);
    client.disconnect();
    EXPECT_TRUE(processReaped(pid));
}

TEST(McpStdioClientTest, StderrEnvAndCwd)
{
    const fs::path dir = fs::temp_directory_path() / ("mcp_stdio_cwd_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    const fs::path envFile = dir / "server.env";
    {
        std::ofstream out(envFile);
        out << "FROM_FILE=file-value\nOVERRIDDEN=file\n";
    }

    ClientConfig cfg = childConfig("server");
    cfg.stdio.env["MCP_STDIO_TEST_VAR"] = "hello world";
    cfg.stdio.env["OVERRIDDEN"] = "env";
    cfg.stdio.envFile = envFile.string();
    cfg.stdio.cwd = dir.string();

    MCPClient client;
    std::mutex m;
    std::vector<std::string> stderrLines;
    client.onStderr([&](const std::string& line) {
        std::lock_guard<std::mutex> lock(m);
        stderrLines.push_back(line);
    });
    ASSERT_TRUE(client.connect(cfg));
    client.initialize();

    EXPECT_EQ(client.callTool("env", {{"name", "MCP_STDIO_TEST_VAR"}})["value"], "hello world");
    EXPECT_EQ(client.callTool("env", {{"name", "FROM_FILE"}})["value"], "file-value");
    EXPECT_EQ(client.callTool("env", {{"name", "OVERRIDDEN"}})["value"], "env");
    EXPECT_FALSE(client.callTool("env", {{"name", "PATH"}})["value"].is_null()) << "environment is inherited";
    const fs::path cwd = fs::u8path(client.callTool("cwd")["cwd"].get<std::string>());
    EXPECT_TRUE(fs::equivalent(cwd, dir)) << cwd << " vs " << dir;

    client.callTool("stderr", {{"text", "to stderr"}});
    EXPECT_TRUE(waitFor([&] {
        std::lock_guard<std::mutex> lock(m);
        return std::find(stderrLines.begin(), stderrLines.end(), "to stderr") != stderrLines.end();
    }));
    client.disconnect();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(McpStdioClientTest, UnsupportedProtocolVersionIsRejected)
{
    MCPClient client;
    ASSERT_TRUE(client.connect(childConfig("bad-version")));
    const std::int64_t pid = client.serverProcessId();
    try
    {
        client.initialize();
        ADD_FAILURE() << "expected std::runtime_error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("1999-01-01"), std::string::npos) << e.what();
    }
    EXPECT_FALSE(client.isConnected());
    EXPECT_EQ(client.protocolVersion(), "");
    EXPECT_TRUE(processReaped(pid));
}

TEST(McpStdioClientTest, UnresponsiveServerIsKilledOnDisconnect)
{
    ClientConfig cfg = childConfig("stubborn");  // ignores stdin EOF and SIGTERM
    cfg.stdio.shutdownTimeoutMs = 200;
    MCPClient client;
    ASSERT_TRUE(client.connect(cfg));
    const std::int64_t pid = client.serverProcessId();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // let it install its handler
    const auto start = std::chrono::steady_clock::now();
    client.disconnect();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
    EXPECT_TRUE(processReaped(pid));
}

TEST(McpStdioClientTest, ConnectFailures)
{
    MCPClient client;
    ClientConfig cfg;  // STDIO without a command
    EXPECT_EQ(cfg.transport, TransportType::STDIO);
    EXPECT_FALSE(client.connect(cfg));
    EXPECT_FALSE(client.isConnected());

    cfg.stdio.command = "definitely-not-an-mcp-server-binary-xyz";
    EXPECT_FALSE(client.connect(cfg));
    EXPECT_FALSE(client.isConnected());

    cfg = childConfig("server");
    cfg.stdio.envFile = "missing-dir/missing.env";
    EXPECT_FALSE(client.connect(cfg));
}

TEST(ChildProcessTest, EchoThroughPipesAndExitCode)
{
    SocketsHpp::utils::ChildProcess child;
    SocketsHpp::utils::ProcessOptions opts;
    opts.command = selfPath();
    opts.args = {"--mcp-stdio-child", "no-such-mode"};  // exits with code 2
    opts.stderrMode = SocketsHpp::utils::StderrMode::Discard;
    std::string error;
    ASSERT_TRUE(child.start(opts, &error)) << error;
    EXPECT_TRUE(child.waitForExit(10000));
    EXPECT_EQ(child.exitCode(), 2);
    EXPECT_FALSE(child.running());
    char buf[16];
    EXPECT_EQ(child.readStdout(buf, sizeof(buf)), 0);  // EOF
    EXPECT_FALSE(child.start(opts, &error));           // only once
}

// ── MCPClient over HTTP: retries, protocol version header, SSE early stop ─────

TEST(McpClientHttpTest, RetriesWhileServerIsNotListening)
{
    // Find a free port, then start the server there only after the client began.
    int port;
    {
        ServerConfig probe;
        probe.transport = TransportType::HTTP_STREAMABLE;
        probe.port = 0;
        MCPServer s(probe);
        s.listen();
        port = s.port();
        s.stop();
    }
    ASSERT_GT(port, 0);

    ServerConfig scfg;
    scfg.transport = TransportType::HTTP_STREAMABLE;
    scfg.port = port;
    std::unique_ptr<MCPServer> server;
    std::thread starter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        server = std::make_unique<MCPServer>(scfg);
        server->listen();
    });

    ClientConfig cfg;
    cfg.transport = TransportType::HTTP_STREAMABLE;
    cfg.http.url = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
    cfg.maxRetries = 50;
    cfg.retryBackoffMs = 100;
    cfg.connectTimeoutSeconds = 2;
    MCPClient client;
    ASSERT_TRUE(client.connect(cfg));
    json init;
    EXPECT_NO_THROW(init = client.initialize());
    starter.join();
    EXPECT_FALSE(client.protocolVersion().empty());
    client.disconnect();
    server->stop();
}

TEST(McpClientHttpTest, RetriesAreBoundedAndJsonRpcErrorsAreNotRetried)
{
    int port;
    {
        ServerConfig probe;
        probe.transport = TransportType::HTTP_STREAMABLE;
        probe.port = 0;
        MCPServer s(probe);
        s.listen();
        port = s.port();
        s.stop();
    }
    ClientConfig cfg;
    cfg.transport = TransportType::HTTP_STREAMABLE;
    cfg.http.url = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
    cfg.maxRetries = 2;
    cfg.retryBackoffMs = 150;
    MCPClient client;
    ASSERT_TRUE(client.connect(cfg));
    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(client.initialize(), std::runtime_error);
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(290));  // 2 backoffs
    client.disconnect();

    // A JSON-RPC error reached the server: never retried.
    ServerConfig scfg;
    scfg.transport = TransportType::HTTP_STREAMABLE;
    scfg.port = 0;
    MCPServer server(scfg);
    std::atomic<int> calls{0};
    server.registerMethod("fail", [&calls](const json&) -> json {
        ++calls;
        throw JsonRpcError::invalidParams("nope");
    });
    server.listen();
    cfg.http.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/mcp";
    ASSERT_TRUE(client.connect(cfg));
    client.initialize();
    EXPECT_THROW(client.request("fail"), JsonRpcError);
    EXPECT_EQ(calls.load(), 1);
    client.disconnect();
    server.stop();
}

// A scripted endpoint that answers every request with an SSE stream that stays open
// after the response, and records the MCP-Protocol-Version header.
TEST(McpClientHttpTest, SseResponseEndsAtResponseAndVersionHeaderIsSent)
{
    using namespace SocketsHpp::http::server;
    HttpServer http("127.0.0.1", 0);
    const int port = http.getListeningPort();
    http.enableThreadPool(4);
    std::atomic<bool> shutdown{false};
    std::mutex m;
    std::map<std::string, std::string> versionHeaderByMethod;

    HttpRequestCallback handler{[&](HttpRequest const& req, HttpResponse& resp) {
        json msg = json::parse(req.content, nullptr, false);
        const std::string method = msg.is_object() ? msg.value("method", "") : "";
        {
            std::lock_guard<std::mutex> lock(m);
            versionHeaderByMethod[method] = req.has_header("MCP-Protocol-Version")
                                                ? req.get_header_value("MCP-Protocol-Version")
                                                : "<none>";
        }
        if (!msg.is_object() || !msg.contains("id"))
        {
            resp.set_status(202);
            return 202;
        }
        json result = json::object();
        if (method == "initialize")
            result = {{"protocolVersion", "2025-06-18"},
                      {"capabilities", json::object()},
                      {"serverInfo", {{"name", "scripted"}, {"version", "1"}}}};
        const std::string events =
            "data: " + json({{"jsonrpc", "2.0"}, {"method", "notifications/message"}, {"params", {{"data", method}}}}).dump() +
            "\n\n" + "data: " + json({{"jsonrpc", "2.0"}, {"id", msg["id"]}, {"result", result}}).dump() + "\n\n";
        resp.code = 200;
        resp.set_header("Content-Type", "text/event-stream");
        bool first = true;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        resp.send_chunk_stream([&shutdown, events, first, until]() mutable -> std::string {
            if (first)
            {
                first = false;
                return events;
            }
            if (shutdown.load() || std::chrono::steady_clock::now() > until)
                return "";
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return ": keepalive\n\n";  // the stream stays open
        });
        return 200;
    }};
    http["/mcp"] = handler;
    http.start();

    ClientConfig cfg;
    cfg.transport = TransportType::HTTP_STREAMABLE;
    cfg.http.url = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
    MCPClient client;
    std::atomic<int> notes{0};
    ASSERT_TRUE(client.connect(cfg));
    // Registered after initialize(), so no GET notification stream is opened before the
    // version is known; notifications embedded in POST responses are still dispatched.
    const auto start = std::chrono::steady_clock::now();
    json init = client.initialize();
    EXPECT_EQ(client.protocolVersion(), "2025-06-18");
    client.onNotification("notifications/message", [&](const json&) { ++notes; });
    // onNotification opens the GET stream only when a session id exists; there is none.
    EXPECT_EQ(client.ping(), json::object());
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5))
        << "the client must stop reading once the response arrived";
    EXPECT_EQ(notes.load(), 1);
    {
        std::lock_guard<std::mutex> lock(m);
        EXPECT_EQ(versionHeaderByMethod["initialize"], "<none>");
        EXPECT_EQ(versionHeaderByMethod["notifications/initialized"], "2025-06-18");
        EXPECT_EQ(versionHeaderByMethod["ping"], "2025-06-18");
    }
    client.disconnect();
    shutdown = true;
    http.stop();
}

TEST(McpClientHttpTest, CancelTokenAbortsHttpRequest)
{
    ServerConfig scfg;
    scfg.transport = TransportType::HTTP_STREAMABLE;
    scfg.port = 0;
    MCPServer server(scfg);
    std::atomic<bool> sawCancel{false};
    server.registerCancellable("slow", [&](const json&, std::shared_ptr<std::atomic<bool>> cancel) -> json {
        for (int i = 0; i < 2000 && !cancel->load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        sawCancel = cancel->load();
        return json::object();
    });
    server.listen();

    ClientConfig cfg;
    cfg.transport = TransportType::HTTP_STREAMABLE;
    cfg.http.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/mcp";
    MCPClient client;
    ASSERT_TRUE(client.connect(cfg));
    client.initialize();
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    std::thread canceller([cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        *cancel = true;
    });
    const auto start = std::chrono::steady_clock::now();
    EXPECT_THROW(client.request("slow", json::object(), cancel), std::runtime_error);
    canceller.join();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
    EXPECT_TRUE(waitFor([&] { return sawCancel.load(); }, 5000));
    client.disconnect();
    server.stop();
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::string(argv[1]) == "--mcp-stdio-child")
        return runChild(argv[2]);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
