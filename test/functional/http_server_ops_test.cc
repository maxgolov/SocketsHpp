// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// Functional tests for HttpServer deployment / operations features: response header
// casing, CORS methods, Unix domain listening sockets, socket activation, graceful
// drain, the log handler and metrics. The raw client uses the library's own Socket
// type, so the portable tests also run on Windows.

#include <gtest/gtest.h>

#include <SocketsHpp/http/server/http_server.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace SocketsHpp::http::server;
using SocketsHpp::net::utils::SocketAddr;
using SocketsHpp::net::utils::Socket;

namespace
{
    std::string toLower(std::string s)
    {
        for (char& c : s)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }

    /// Parsed HTTP response (header names as received and lower-cased).
    struct RawResponse
    {
        bool complete = false;
        int code = 0;
        std::map<std::string, std::string> headers;    // lower-cased names
        std::vector<std::string> rawHeaderNames;       // names exactly as sent
        std::string body;

        std::string header(const std::string& lowerName) const
        {
            auto it = headers.find(lowerName);
            return it == headers.end() ? std::string() : it->second;
        }
    };

    /// Minimal blocking client over SocketsHpp::net::utils::Socket (portable).
    class RawConn
    {
    public:
        /// Connect to 127.0.0.1:port.
        explicit RawConn(int port)
        {
            open(SocketAddr("127.0.0.1", port), AF_INET, IPPROTO_TCP);
        }

#ifdef HAVE_UNIX_DOMAIN
        /// Connect to a Unix domain socket path.
        explicit RawConn(const std::string& path)
        {
            open(SocketAddr(path.c_str(), true), AF_UNIX, 0);
        }
#endif

        ~RawConn()
        {
            if (!m_sock.invalid())
            {
                m_sock.close();
            }
        }

        RawConn(const RawConn&) = delete;
        RawConn& operator=(const RawConn&) = delete;

        bool connected() const { return m_connected; }

        bool send(const std::string& data)
        {
            size_t off = 0;
            while (off < data.size())
            {
                int n = m_sock.send(data.data() + off, data.size() - off);
                if (n <= 0)
                {
                    return false;
                }
                off += static_cast<size_t>(n);
            }
            return true;
        }

        /// Receive more data (with a timeout). false on EOF, error or timeout.
        bool fill(int timeoutMs = 5000)
        {
            setRecvTimeout(timeoutMs);
            char buf[16384];
            int n = m_sock.recv(buf, sizeof(buf));
            if (n <= 0)
            {
                if (n == 0 || !Socket::isWouldBlock(m_sock.error()))
                {
                    m_eof = true;
                }
                return false;
            }
            m_buffer.append(buf, static_cast<size_t>(n));
            return true;
        }

        /// true once the peer closed the connection (EOF or reset) within the timeout.
        bool waitForClose(int timeoutMs = 5000)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (!m_eof && std::chrono::steady_clock::now() < deadline)
            {
                fill(100);
            }
            return m_eof;
        }

        /// Read one response (Content-Length or chunked body).
        RawResponse readResponse(int timeoutMs = 5000)
        {
            RawResponse res;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            size_t headEnd;
            while ((headEnd = m_buffer.find("\r\n\r\n")) == std::string::npos)
            {
                if (std::chrono::steady_clock::now() >= deadline || (!fill(100) && m_eof))
                {
                    return res;
                }
            }
            std::string head = m_buffer.substr(0, headEnd);
            m_buffer.erase(0, headEnd + 4);
            size_t lineEnd = head.find("\r\n");
            std::string status = head.substr(0, lineEnd);
            if (status.size() >= 12)
            {
                res.code = std::atoi(status.substr(9, 3).c_str());
            }
            size_t pos = (lineEnd == std::string::npos) ? head.size() : lineEnd + 2;
            while (pos < head.size())
            {
                size_t e = head.find("\r\n", pos);
                if (e == std::string::npos)
                {
                    e = head.size();
                }
                std::string line = head.substr(pos, e - pos);
                size_t colon = line.find(':');
                if (colon != std::string::npos)
                {
                    std::string name = line.substr(0, colon);
                    std::string value = line.substr(colon + 1);
                    while (!value.empty() && value[0] == ' ')
                    {
                        value.erase(0, 1);
                    }
                    res.rawHeaderNames.push_back(name);
                    res.headers[toLower(name)] = value;
                }
                pos = e + 2;
            }
            if (toLower(res.header("transfer-encoding")) == "chunked")
            {
                for (;;)
                {
                    size_t eol;
                    while ((eol = m_buffer.find("\r\n")) == std::string::npos)
                    {
                        if (std::chrono::steady_clock::now() >= deadline || (!fill(100) && m_eof))
                        {
                            return res;
                        }
                    }
                    size_t size = std::stoul(m_buffer.substr(0, eol), nullptr, 16);
                    while (m_buffer.size() < eol + 2 + size + 2)
                    {
                        if (std::chrono::steady_clock::now() >= deadline || (!fill(100) && m_eof))
                        {
                            return res;
                        }
                    }
                    res.body.append(m_buffer, eol + 2, size);
                    m_buffer.erase(0, eol + 2 + size + 2);
                    if (size == 0)
                    {
                        res.complete = true;
                        return res;
                    }
                }
            }
            size_t length = static_cast<size_t>(std::atoll(res.header("content-length").c_str()));
            while (m_buffer.size() < length)
            {
                if (std::chrono::steady_clock::now() >= deadline || (!fill(100) && m_eof))
                {
                    return res;
                }
            }
            res.body = m_buffer.substr(0, length);
            m_buffer.erase(0, length);
            res.complete = true;
            return res;
        }

        /// Send a request and read its response.
        RawResponse request(const std::string& raw, int timeoutMs = 5000)
        {
            if (!send(raw))
            {
                return RawResponse();
            }
            return readResponse(timeoutMs);
        }

        std::string& buffer() { return m_buffer; }

    private:
        void open(const SocketAddr& addr, int af, int proto)
        {
            m_sock = Socket(af, SOCK_STREAM, proto);
            m_connected = m_sock.connect(addr);
        }

        void setRecvTimeout(int timeoutMs)
        {
#ifdef _WIN32
            DWORD tv = static_cast<DWORD>(timeoutMs);
            ::setsockopt(m_sock.m_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
            timeval tv;
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            ::setsockopt(m_sock.m_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        }

        Socket m_sock;
        bool m_connected = false;
        bool m_eof = false;
        std::string m_buffer;
    };

    std::string get(const std::string& path, const std::string& extra = "")
    {
        return "GET " + path + " HTTP/1.1\r\nHost: test\r\n" + extra + "\r\n";
    }
}  // namespace

// ---------------------------------------------------------------------------
// Header casing and CORS methods
// ---------------------------------------------------------------------------

TEST(HttpServerOpsTest, WellKnownResponseHeaderCasing)
{
    HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);
    server.route("/", [](const HttpRequest&, HttpResponse& res) {
        res.set_header("www-authenticate", "Bearer");
        res.set_header("etag", "\"v1\"");
        res.set_header("x-custom-header", "1");
        res.set_content("x");
        return 200;
    });
    server.start();

    RawConn c(port);
    ASSERT_TRUE(c.connected());
    RawResponse res = c.request(get("/"));
    ASSERT_TRUE(res.complete);
    auto has = [&](const std::string& name) {
        for (auto const& n : res.rawHeaderNames)
        {
            if (n == name)
            {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(has("WWW-Authenticate"));
    EXPECT_TRUE(has("ETag"));
    EXPECT_TRUE(has("X-Custom-Header"));
    EXPECT_TRUE(has("Content-Length"));
    server.stop();
}

// ---------------------------------------------------------------------------
// Runtime log handler
// ---------------------------------------------------------------------------

namespace
{
    struct CapturedLog
    {
        std::mutex mutex;
        std::vector<std::pair<SocketsHpp::LogLevel, std::string>> messages;

        bool contains(SocketsHpp::LogLevel level, const std::string& text)
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto const& m : messages)
            {
                if (m.first == level && m.second.find(text) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        bool any(SocketsHpp::LogLevel level)
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto const& m : messages)
            {
                if (m.first == level)
                {
                    return true;
                }
            }
            return false;
        }
    };

    int g_evaluations = 0;
    int countEvaluation()
    {
        return ++g_evaluations;
    }
}  // namespace

TEST(HttpServerOpsTest, LogHandlerReceivesLibraryMessages)
{
    auto captured = std::make_shared<CapturedLog>();
    SocketsHpp::setLogHandler([captured](SocketsHpp::LogLevel level, const char* msg) {
        std::lock_guard<std::mutex> lock(captured->mutex);
        captured->messages.emplace_back(level, msg);
    });
    SocketsHpp::setLogLevel(SocketsHpp::LogLevel::Info);
    {
        HttpServer server;
        int port = server.addListeningPort("127.0.0.1", 0);
        server.route("/hello", [](const HttpRequest&, HttpResponse& res) {
            res.set_content("hi");
            return 200;
        });
        server.start();
        RawConn c(port);
        ASSERT_TRUE(c.connected());
        EXPECT_EQ(c.request(get("/hello")).code, 200);
        server.stop();
    }
    SocketsHpp::setLogHandler(nullptr);

    EXPECT_TRUE(captured->contains(SocketsHpp::LogLevel::Info, "Listening on 127.0.0.1:"));
    EXPECT_TRUE(captured->contains(SocketsHpp::LogLevel::Info, "GET /hello HTTP/1.1"));
    EXPECT_FALSE(captured->any(SocketsHpp::LogLevel::Trace));  // below the Info threshold
}

TEST(HttpServerOpsTest, TraceLevelFormatsEveryMessage)
{
    // Exercises the trace statements of the reactor, the server state machine,
    // the thread pool and streaming (useful under sanitizers).
    std::atomic<size_t> count{ 0 };
    SocketsHpp::setLogHandler([&count](SocketsHpp::LogLevel, const char* msg) {
        if (msg != nullptr && msg[0] != '\0')
        {
            ++count;
        }
    });
    SocketsHpp::setLogLevel(SocketsHpp::LogLevel::Trace);
    {
        HttpServer server;
        int port = server.addListeningPort("127.0.0.1", 0);
        server.enableThreadPool(2);
        server.route("/stream", [](const HttpRequest&, HttpResponse& res) {
            auto n = std::make_shared<int>(0);
            res.set_header("Content-Type", "text/event-stream");
            res.send_chunk_stream([n]() { return (++*n <= 2) ? std::string("data: x\n\n") : std::string(); });
            return 200;
        });
        server.route("/", [](const HttpRequest& req, HttpResponse& res) {
            res.set_content(req.content);
            return 200;
        });
        server.start();
        RawConn c(port);
        ASSERT_TRUE(c.connected());
        EXPECT_EQ(c.request("POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 3\r\n\r\nabc").body, "abc");
        RawResponse s = c.request(get("/stream"));
        EXPECT_EQ(s.body, "data: x\n\ndata: x\n\n");
        server.stop();
    }
    SocketsHpp::setLogHandler(nullptr);
    SocketsHpp::setLogLevel(SocketsHpp::LogLevel::Info);
    EXPECT_GT(count.load(), 10u);
}

TEST(HttpServerOpsTest, LogLevelFiltersAndArgumentsAreLazy)
{
    auto captured = std::make_shared<CapturedLog>();
    g_evaluations = 0;
    LOG_ERROR("no handler: %d", countEvaluation());
    EXPECT_EQ(g_evaluations, 0);  // arguments not evaluated without a handler

    SocketsHpp::setLogHandler([captured](SocketsHpp::LogLevel level, const char* msg) {
        std::lock_guard<std::mutex> lock(captured->mutex);
        captured->messages.emplace_back(level, msg);
    });
    SocketsHpp::setLogLevel(SocketsHpp::LogLevel::Warn);
    LOG_INFO("filtered %d", countEvaluation());
    EXPECT_EQ(g_evaluations, 0);
    LOG_WARN("warned %d", countEvaluation());
    LOG_ERROR("plain error");
    EXPECT_EQ(g_evaluations, 1);
    EXPECT_EQ(SocketsHpp::getLogLevel(), SocketsHpp::LogLevel::Warn);

    SocketsHpp::setLogLevel(SocketsHpp::LogLevel::Trace);
    LOG_TRACE("trace %s", "on");

    SocketsHpp::setLogHandler(nullptr);
    SocketsHpp::setLogLevel(SocketsHpp::LogLevel::Info);
    LOG_ERROR("removed %d", countEvaluation());
    EXPECT_EQ(g_evaluations, 1);

    EXPECT_FALSE(captured->any(SocketsHpp::LogLevel::Info));
    EXPECT_TRUE(captured->contains(SocketsHpp::LogLevel::Warn, "warned 1"));
    EXPECT_TRUE(captured->contains(SocketsHpp::LogLevel::Error, "plain error"));
    EXPECT_TRUE(captured->contains(SocketsHpp::LogLevel::Trace, "trace on"));
    EXPECT_FALSE(captured->contains(SocketsHpp::LogLevel::Error, "removed"));
    EXPECT_STREQ(SocketsHpp::logLevelName(SocketsHpp::LogLevel::Warn), "WARN");
}

TEST(HttpServerOpsTest, CorsMethodsAreConfigurable)
{
    HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);
    server.enableCors();
    server.setCorsMethods("GET, PUT");
    server.start();

    RawConn c(port);
    ASSERT_TRUE(c.connected());
    RawResponse res = c.request("OPTIONS /x HTTP/1.1\r\nHost: t\r\nOrigin: http://a\r\n\r\n");
    ASSERT_TRUE(res.complete);
    EXPECT_EQ(res.code, 204);
    EXPECT_EQ(res.header("access-control-allow-methods"), "GET, PUT");
    server.stop();
}
