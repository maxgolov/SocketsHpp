// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// Functional tests for HttpServer deployment / operations features: response header
// casing, CORS methods, Unix domain listening sockets, socket activation, graceful
// drain, the log handler and metrics. The raw client uses the library's own Socket
// type, so the portable tests also run on Windows.

#include <gtest/gtest.h>

#include <SocketsHpp/http/client/http_client.h>
#include <SocketsHpp/http/server/http_server.h>
#include <SocketsHpp/http/server/multipart.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace SocketsHpp::http::server;
using SocketsHpp::net::utils::SocketAddr;
using SocketsHpp::net::utils::Socket;
namespace fs = std::filesystem;

#ifndef _WIN32
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

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

// ---------------------------------------------------------------------------
// Unix domain listening sockets
// ---------------------------------------------------------------------------

#ifdef HAVE_UNIX_DOMAIN
namespace
{
    /// Short, unique socket path in the temp directory (sun_path is ~104-108 bytes).
    std::string uniqueSocketPath(const char* tag)
    {
        static std::atomic<int> counter{ 0 };
        std::string dir = fs::temp_directory_path().string();
#  ifdef _WIN32
        const long pid = static_cast<long>(::GetCurrentProcessId());
        const char sep = '\\';
#  else
        const long pid = static_cast<long>(::getpid());
        const char sep = '/';
#  endif
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        {
            dir += sep;
        }
        std::string path = dir + "shpp-" + tag + "-" + std::to_string(pid) + "-" + std::to_string(++counter) + ".sock";
        std::error_code ec;
        fs::remove(path, ec);
        return path;
    }

    /// Bind a Unix socket at @p path; false if AF_UNIX is unusable here (e.g. Wine).
    bool unixSocketsUsable()
    {
        const std::string path = uniqueSocketPath("probe");
        try
        {
            Socket s(AF_UNIX, SOCK_STREAM, 0);
            const bool ok = s.bind(SocketAddr(path.c_str(), true)) == 0;
            s.close();
            std::error_code ec;
            fs::remove(path, ec);
            return ok;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
}  // namespace

#  define REQUIRE_UNIX_SOCKETS()                                                  \
      do                                                                          \
      {                                                                           \
          if (!unixSocketsUsable())                                               \
          {                                                                       \
              GTEST_SKIP() << "AF_UNIX sockets are not usable on this platform";  \
          }                                                                       \
      } while (0)

TEST(HttpServerUnixSocketTest, RoundTripReplacesStaleFileAndCleansUp)
{
    REQUIRE_UNIX_SOCKETS();
    const std::string path = uniqueSocketPath("rt");
    {
        // A stale socket file: bound, then closed without unlinking.
        Socket stale(AF_UNIX, SOCK_STREAM, 0);
        ASSERT_EQ(stale.bind(SocketAddr(path.c_str(), true)), 0);
        stale.close();
        ASSERT_TRUE(fs::exists(path));
    }

    HttpServer server;
    server.addListeningUnixSocket(path, 0600);
    EXPECT_EQ(server.getListeningPort(), -1);
    ASSERT_EQ(server.getListeningUnixSockets().size(), 1u);
    EXPECT_EQ(server.getListeningUnixSockets()[0], path);
#  ifndef _WIN32
    struct stat st;
    ASSERT_EQ(::lstat(path.c_str(), &st), 0);
    EXPECT_TRUE(S_ISSOCK(st.st_mode));
    EXPECT_EQ(st.st_mode & 0777, 0600u);
#  endif
    server.route("/who", [](const HttpRequest& req, HttpResponse& res) {
        res.set_content(req.client);
        return 200;
    });
    server.start();

    {
        RawConn c(path);
        ASSERT_TRUE(c.connected());
        RawResponse res = c.request(get("/who"));
        ASSERT_TRUE(res.complete);
        EXPECT_EQ(res.code, 200);
        EXPECT_EQ(res.body, "unix");
        // Keep-alive works over the Unix socket too.
        EXPECT_EQ(c.request(get("/who")).body, "unix");
    }

    SocketsHpp::http::client::HttpClient client;
    client.setUnixSocketPath(path);
    EXPECT_EQ(client.getUnixSocketPath(), path);
    SocketsHpp::http::client::HttpClientResponse response;
    ASSERT_TRUE(client.get("http://localhost/who", response));
    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body, "unix");

    server.stop();
    EXPECT_FALSE(fs::exists(path));  // removed on stop
    SocketsHpp::http::client::HttpClientResponse after;
    EXPECT_FALSE(client.get("http://localhost/who", after));
}

TEST(HttpServerUnixSocketTest, RefusesNonSocketFileAndLiveSocket)
{
    REQUIRE_UNIX_SOCKETS();
    const std::string path = uniqueSocketPath("ns");
    {
        std::ofstream f(path);
        f << "not a socket";
    }
    {
        HttpServer server;
        EXPECT_THROW(server.addListeningUnixSocket(path), std::runtime_error);
    }
    EXPECT_TRUE(fs::exists(path));  // left alone
    std::error_code ec;
    fs::remove(path, ec);

    HttpServer first;
    first.addListeningUnixSocket(path);
    first.route("/", [](const HttpRequest&, HttpResponse& res) {
        res.set_content("first");
        return 200;
    });
    first.start();
    {
        HttpServer second;
        EXPECT_THROW(second.addListeningUnixSocket(path), std::runtime_error);
    }
    RawConn c(path);
    ASSERT_TRUE(c.connected());
    EXPECT_EQ(c.request(get("/")).body, "first");  // the live server is untouched
    first.stop();
    EXPECT_FALSE(fs::exists(path));

    HttpServer bad;
    EXPECT_THROW(bad.addListeningUnixSocket(""), std::invalid_argument);
    EXPECT_THROW(bad.addListeningUnixSocket(std::string(300, 'x')), std::invalid_argument);
}

TEST(HttpServerUnixSocketTest, TcpAndUnixListenersTogether)
{
    REQUIRE_UNIX_SOCKETS();
    const std::string path = uniqueSocketPath("mix");
    HttpServer server;
    server.addListeningUnixSocket(path);
    int port = server.addListeningPort("127.0.0.1", 0);
    EXPECT_EQ(server.getListeningPort(), port);
    server.route("/who", [](const HttpRequest& req, HttpResponse& res) {
        res.set_content(req.client);
        return 200;
    });
    server.start();
    RawConn u(path);
    RawConn t(port);
    ASSERT_TRUE(u.connected());
    ASSERT_TRUE(t.connected());
    EXPECT_EQ(u.request(get("/who")).body, "unix");
    EXPECT_EQ(t.request(get("/who")).body.rfind("127.0.0.1:", 0), 0u);
    server.stop();
    EXPECT_FALSE(fs::exists(path));
}
#endif  // HAVE_UNIX_DOMAIN

// ---------------------------------------------------------------------------
// Adopted / inherited listening sockets (systemd socket activation)
// ---------------------------------------------------------------------------

TEST(HttpServerAdoptTest, RejectsNonListeningSocket)
{
    Socket s(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    HttpServer server;
    EXPECT_THROW(server.adoptListeningSocket(s.m_sock), std::invalid_argument);
    EXPECT_THROW(server.adoptListeningSocket(Socket::Invalid), std::invalid_argument);
    s.close();  // still ours after a failed adoption
}

TEST(HttpServerAdoptTest, AdoptsListeningTcpSocket)
{
    Socket s(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_EQ(s.bind(SocketAddr("127.0.0.1", 0)), 0);
    ASSERT_TRUE(s.listen(8));
    SocketAddr bound;
    ASSERT_TRUE(s.getsockname(bound));

    HttpServer server;
    EXPECT_EQ(server.adoptListeningSocket(s.m_sock), bound.port());
    EXPECT_EQ(server.getListeningPort(), bound.port());
    server.route("/", [](const HttpRequest&, HttpResponse& res) {
        res.set_content("adopted");
        return 200;
    });
    server.start();
    RawConn c(bound.port());
    ASSERT_TRUE(c.connected());
    EXPECT_EQ(c.request(get("/")).body, "adopted");
    server.stop();  // closes the adopted socket
}

#ifdef __linux__
namespace
{
    /// Runs @p body with a listening TCP socket installed as fd 3 (as systemd passes
    /// it), restoring whatever fd 3 was before.
    template <typename Body>
    void withSocketAtFd3(Body body)
    {
        int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(lfd, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ASSERT_EQ(::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        ASSERT_EQ(::listen(lfd, 8), 0);
        socklen_t len = sizeof(addr);
        ASSERT_EQ(::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &len), 0);
        const int port = ntohs(addr.sin_port);

        const int saved = (::fcntl(3, F_GETFD) != -1) ? ::fcntl(3, F_DUPFD_CLOEXEC, 10) : -1;
        if (lfd != 3)
        {
            ASSERT_EQ(::dup2(lfd, 3), 3);
            ::close(lfd);
        }
        body(port);
        if (::fcntl(3, F_GETFD) != -1)
        {
            ::close(3);  // not adopted: close our copy
        }
        if (saved >= 0)
        {
            ::dup2(saved, 3);
            ::close(saved);
        }
    }
}  // namespace

TEST(HttpServerAdoptTest, SystemdSocketActivation)
{
    withSocketAtFd3([](int port) {
        ::setenv("LISTEN_PID", std::to_string(::getpid()).c_str(), 1);
        ::setenv("LISTEN_FDS", "1", 1);
        ::setenv("LISTEN_FDNAMES", "http", 1);
        HttpServer server;
        EXPECT_EQ(server.addInheritedListeningSockets(), 1u);
        EXPECT_EQ(std::getenv("LISTEN_PID"), nullptr);
        EXPECT_EQ(std::getenv("LISTEN_FDS"), nullptr);
        EXPECT_EQ(std::getenv("LISTEN_FDNAMES"), nullptr);
        EXPECT_EQ(server.getListeningPort(), port);
        EXPECT_NE(::fcntl(3, F_GETFD) & FD_CLOEXEC, 0);
        server.route("/", [](const HttpRequest&, HttpResponse& res) {
            res.set_content("activated");
            return 200;
        });
        server.start();
        {
            RawConn c(port);
            ASSERT_TRUE(c.connected());
            EXPECT_EQ(c.request(get("/")).body, "activated");
        }
        server.stop();
        EXPECT_EQ(::fcntl(3, F_GETFD), -1);  // closed by the server
    });
}

TEST(HttpServerAdoptTest, SocketActivationIgnoresOtherProcess)
{
    withSocketAtFd3([](int) {
        ::setenv("LISTEN_PID", std::to_string(::getpid() + 1).c_str(), 1);
        ::setenv("LISTEN_FDS", "1", 1);
        HttpServer server;
        EXPECT_EQ(server.addInheritedListeningSockets(), 0u);
        EXPECT_EQ(std::getenv("LISTEN_PID"), nullptr);
        EXPECT_EQ(server.getListeningPort(), -1);

        ::setenv("LISTEN_PID", "garbage", 1);
        ::setenv("LISTEN_FDS", "1", 1);
        EXPECT_EQ(server.addInheritedListeningSockets(false), 0u);
        EXPECT_NE(std::getenv("LISTEN_PID"), nullptr);  // kept on request
        ::unsetenv("LISTEN_PID");
        ::unsetenv("LISTEN_FDS");
        EXPECT_EQ(server.addInheritedListeningSockets(), 0u);  // not activated
    });
}

TEST(HttpServerAdoptTest, SdNotifySendsDatagram)
{
    ::unsetenv("NOTIFY_SOCKET");
    EXPECT_FALSE(SocketsHpp::net::utils::sdNotify("READY=1"));

    // Filesystem socket
    const std::string path = uniqueSocketPath("notify");
    int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    ASSERT_GE(fd, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size());
    ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ::setenv("NOTIFY_SOCKET", path.c_str(), 1);
    EXPECT_TRUE(SocketsHpp::net::utils::sdNotify("READY=1"));
    char buf[64] = {};
    EXPECT_EQ(::recv(fd, buf, sizeof(buf), MSG_DONTWAIT), 7);
    EXPECT_EQ(std::string(buf, 7), "READY=1");
    ::close(fd);
    ::unlink(path.c_str());

    // Abstract socket ("@name")
    const std::string name = "shpp-notify-" + std::to_string(::getpid());
    fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    ASSERT_GE(fd, 0);
    sockaddr_un abs{};
    abs.sun_family = AF_UNIX;
    std::memcpy(abs.sun_path + 1, name.data(), name.size());
    ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&abs),
        static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size())), 0);
    ::setenv("NOTIFY_SOCKET", ("@" + name).c_str(), 1);
    EXPECT_TRUE(SocketsHpp::net::utils::sdNotify("STATUS=serving\nREADY=1"));
    std::memset(buf, 0, sizeof(buf));
    EXPECT_EQ(::recv(fd, buf, sizeof(buf), MSG_DONTWAIT), 22);
    EXPECT_EQ(std::string(buf), "STATUS=serving\nREADY=1");
    ::close(fd);
    ::unsetenv("NOTIFY_SOCKET");
}
#else
TEST(HttpServerAdoptTest, SocketActivationIsLinuxOnly)
{
    HttpServer server;
    EXPECT_EQ(server.addInheritedListeningSockets(), 0u);
    EXPECT_FALSE(SocketsHpp::net::utils::sdNotify("READY=1"));
}
#endif

// ---------------------------------------------------------------------------
// Graceful drain (shutdown())
// ---------------------------------------------------------------------------

namespace
{
    template <typename Pred>
    bool waitFor(Pred pred, int timeoutMs = 5000)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!pred())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return true;
    }

    /// true once connecting to 127.0.0.1:port fails (listening socket closed).
    bool connectionsRefused(int port, int timeoutMs = 3000)
    {
        return waitFor([port]() { return !RawConn(port).connected(); }, timeoutMs);
    }
}  // namespace

TEST(HttpServerDrainTest, InFlightRequestCompletesIdleClosedNewRefused)
{
    HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);
    server.enableThreadPool(2);
    std::atomic<bool> started{ false };
    std::atomic<bool> release{ false };
    server.route("/slow", [&](const HttpRequest&, HttpResponse& res) {
        started = true;
        waitFor([&]() { return release.load(); }, 10000);
        res.set_content("done");
        return 200;
    });
    server.route("/", [](const HttpRequest&, HttpResponse& res) {
        res.set_content("ok");
        return 200;
    });
    server.start();

    RawConn idle(port);
    ASSERT_TRUE(idle.connected());
    RawResponse first = idle.request(get("/"));
    ASSERT_EQ(first.code, 200);
    EXPECT_EQ(toLower(first.header("connection")), "keep-alive");

    RawConn slow(port);
    ASSERT_TRUE(slow.connected());
    ASSERT_TRUE(slow.send(get("/slow")));
    ASSERT_TRUE(waitFor([&]() { return started.load(); }));

    std::atomic<int> result{ -1 };
    std::thread drainer([&]() { result = server.shutdown(std::chrono::seconds(10)) ? 1 : 0; });

    EXPECT_TRUE(idle.waitForClose(3000));  // idle keep-alive connection closed at once
    EXPECT_TRUE(connectionsRefused(port));  // no longer accepting
    EXPECT_TRUE(server.isDraining());
    EXPECT_EQ(result.load(), -1);           // still waiting for /slow

    release = true;
    RawResponse r = slow.readResponse(5000);
    ASSERT_TRUE(r.complete);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.body, "done");
    EXPECT_EQ(toLower(r.header("connection")), "close");
    EXPECT_TRUE(slow.waitForClose(3000));
    drainer.join();
    EXPECT_EQ(result.load(), 1);  // drained before the deadline
}

TEST(HttpServerDrainTest, RequestStillBeingReceivedCompletes)
{
    HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);
    server.route("/", [](const HttpRequest& req, HttpResponse& res) {
        res.set_content(req.content);
        return 200;
    });
    server.start();

    RawConn c(port);
    ASSERT_TRUE(c.connected());
    ASSERT_TRUE(c.send("POST /echo HTTP/1.1\r\nHost: t\r\nContent-Length: 6\r\n\r\nabc"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // head received, body partial

    std::atomic<int> result{ -1 };
    std::thread drainer([&]() { result = server.shutdown(std::chrono::seconds(10)) ? 1 : 0; });
    EXPECT_TRUE(connectionsRefused(port));
    ASSERT_TRUE(c.send("def"));
    RawResponse r = c.readResponse(5000);
    ASSERT_TRUE(r.complete);
    EXPECT_EQ(r.body, "abcdef");
    EXPECT_EQ(toLower(r.header("connection")), "close");
    drainer.join();
    EXPECT_EQ(result.load(), 1);
}

TEST(HttpServerDrainTest, StreamsAreEndedGracefully)
{
    HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);
    std::atomic<int> chunks{ 0 };
    std::atomic<bool> ended{ false };
    server.route("/events", [&](const HttpRequest&, HttpResponse& res) {
        res.set_header("Content-Type", "text/event-stream");
        res.send_chunk_stream(
            [&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                return "data: " + std::to_string(++chunks) + "\n\n";
            },
            [&]() { ended = true; });
        return 200;
    });
    server.start();

    RawConn c(port);
    ASSERT_TRUE(c.connected());
    ASSERT_TRUE(c.send(get("/events")));
    ASSERT_TRUE(waitFor([&]() { return chunks.load() >= 3; }));

    std::atomic<int> result{ -1 };
    std::thread drainer([&]() { result = server.shutdown(std::chrono::seconds(10)) ? 1 : 0; });
    RawResponse r = c.readResponse(8000);
    EXPECT_TRUE(r.complete);  // terminating chunk received
    EXPECT_NE(r.body.find("data: 1\n\n"), std::string::npos);
    drainer.join();
    EXPECT_EQ(result.load(), 1);
    EXPECT_TRUE(ended.load());
}

TEST(HttpServerDrainTest, DeadlineCutsBlockedStream)
{
    HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);
    server.enableThreadPool(2);
    std::atomic<bool> release{ false };
    std::atomic<int> calls{ 0 };
    std::atomic<bool> ended{ false };
    server.route("/events", [&](const HttpRequest&, HttpResponse& res) {
        res.set_header("Content-Type", "text/event-stream");
        res.send_chunk_stream(
            [&]() {
                if (++calls > 1)
                {
                    waitFor([&]() { return release.load(); }, 10000);  // blocks past the deadline
                }
                return std::string("data: x\n\n");
            },
            [&]() { ended = true; });
        return 200;
    });
    server.start();

    RawConn c(port);
    ASSERT_TRUE(c.connected());
    ASSERT_TRUE(c.send(get("/events")));
    ASSERT_TRUE(waitFor([&]() { return calls.load() >= 2; }));

    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_FALSE(server.shutdown(std::chrono::milliseconds(300)));
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_GE(elapsed, std::chrono::milliseconds(250));
    EXPECT_LT(elapsed, std::chrono::seconds(3));

    RawResponse r = c.readResponse(3000);
    EXPECT_FALSE(r.complete);  // cut: no terminating chunk
    EXPECT_TRUE(c.waitForClose(3000));
    release = true;  // let the pool worker finish (the destructor waits for it)
    EXPECT_FALSE(ended.load());
}

TEST(HttpServerDrainTest, ShutdownWithoutStartOrConnections)
{
    HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);
    EXPECT_TRUE(server.shutdown(std::chrono::milliseconds(100)));
    EXPECT_FALSE(RawConn(port).connected());
    EXPECT_TRUE(server.shutdown(std::chrono::milliseconds(100)));  // idempotent

    HttpServer started;
    int port2 = started.addListeningPort("127.0.0.1", 0);
    started.start();
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_TRUE(started.shutdown(std::chrono::seconds(5)));
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::seconds(2));
    EXPECT_FALSE(RawConn(port2).connected());
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

TEST(HttpServerMetricsTest, CountersAndPrometheusEndpoint)
{
    HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);
    server.enableMetricsEndpoint();
    server.route("/ok", [](const HttpRequest&, HttpResponse& res) {
        res.set_content("fine");
        return 200;
    });
    server.route("/boom", [](const HttpRequest&, HttpResponse&) -> int { throw std::runtime_error("x"); });
    server.start();

    {
        RawConn c(port);
        ASSERT_TRUE(c.connected());
        EXPECT_EQ(c.request(get("/ok")).code, 200);
        EXPECT_EQ(c.request(get("/ok")).code, 200);
        EXPECT_EQ(c.request(get("/missing")).code, 404);
        EXPECT_EQ(c.request(get("/boom")).code, 500);
        EXPECT_EQ(c.request(get("/metrics/other")).code, 404);  // exact path only
    }
    {
        RawConn bad(port);
        ASSERT_TRUE(bad.connected());
        EXPECT_EQ(bad.request("BROKEN\r\n\r\n").code, 400);
    }

    ASSERT_TRUE(waitFor([&]() { return server.metrics().connectionsActive == 0; }));
    HttpServerMetrics m = server.metrics();
    EXPECT_EQ(m.connectionsAccepted, 2u);
    EXPECT_EQ(m.connectionsRefused, 0u);
    EXPECT_EQ(m.requestsTotal, 6u);
    EXPECT_EQ(m.responsesByClass[1], 2u);  // 2xx
    EXPECT_EQ(m.responsesByClass[3], 3u);  // 4xx: 404, 404, 400
    EXPECT_EQ(m.responsesByClass[4], 1u);  // 5xx
    EXPECT_GT(m.bytesReceived, 0u);
    EXPECT_GT(m.bytesSent, m.bytesReceived);

    RawConn c(port);
    RawResponse res = c.request(get("/metrics?x=1"));
    ASSERT_TRUE(res.complete);
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.header("content-type"), "text/plain; version=0.0.4; charset=utf-8");
    const std::string& text = res.body;
    EXPECT_NE(text.find("# TYPE socketshpp_http_connections_accepted_total counter\n"
                        "socketshpp_http_connections_accepted_total 3\n"), std::string::npos) << text;
    EXPECT_NE(text.find("socketshpp_http_connections_active 1\n"), std::string::npos) << text;
    EXPECT_NE(text.find("# TYPE socketshpp_http_connections_active gauge\n"), std::string::npos) << text;
    EXPECT_NE(text.find("socketshpp_http_responses_total{code=\"2xx\"} 2\n"), std::string::npos) << text;
    EXPECT_NE(text.find("socketshpp_http_responses_total{code=\"4xx\"} 3\n"), std::string::npos) << text;
    EXPECT_NE(text.find("socketshpp_http_responses_total{code=\"5xx\"} 1\n"), std::string::npos) << text;
    EXPECT_NE(text.find("socketshpp_http_requests_total 7\n"), std::string::npos) << text;
    EXPECT_NE(text.find("socketshpp_http_sent_bytes_total "), std::string::npos) << text;
    EXPECT_EQ(text.back(), '\n');

    RawResponse post = c.request("POST /metrics HTTP/1.1\r\nHost: t\r\nContent-Length: 0\r\n\r\n");
    EXPECT_EQ(post.code, 404);  // only GET is answered
    server.stop();
}

TEST(HttpServerMetricsTest, MaxConnectionsRefusesAndTimeoutsAreCounted)
{
    HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);
    server.setMaxConnections(2);
    server.setIdleTimeout(std::chrono::milliseconds(300));
    server.route("/", [](const HttpRequest&, HttpResponse& res) {
        res.set_content("ok");
        return 200;
    });
    server.start();

    RawConn a(port);
    RawConn b(port);
    ASSERT_TRUE(a.connected());
    ASSERT_TRUE(b.connected());
    EXPECT_EQ(a.request(get("/")).code, 200);
    EXPECT_EQ(b.request(get("/")).code, 200);

    RawConn refused(port);  // TCP connect succeeds (backlog), then the server closes it
    ASSERT_TRUE(refused.connected());
    EXPECT_TRUE(refused.waitForClose(3000));
    EXPECT_EQ(server.metrics().connectionsRefused, 1u);

    // a and b go idle and are closed by the idle timeout.
    EXPECT_TRUE(a.waitForClose(3000));
    EXPECT_TRUE(b.waitForClose(3000));
    ASSERT_TRUE(waitFor([&]() { return server.metrics().timeouts >= 2; }));
    HttpServerMetrics m = server.metrics();
    EXPECT_EQ(m.connectionsAccepted, 2u);
    EXPECT_EQ(m.connectionsActive, 0u);

    RawConn again(port);  // room again
    ASSERT_TRUE(again.connected());
    EXPECT_EQ(again.request(get("/")).code, 200);
    server.stop();
}

TEST(HttpServerMetricsTest, FormatPrometheusIsStatic)
{
    HttpServerMetrics m;
    m.connectionsAccepted = 7;
    m.responsesByClass[2] = 3;
    const std::string text = HttpServer::formatPrometheus(m);
    EXPECT_NE(text.find("socketshpp_http_connections_accepted_total 7\n"), std::string::npos);
    EXPECT_NE(text.find("socketshpp_http_responses_total{code=\"3xx\"} 3\n"), std::string::npos);
    EXPECT_NE(text.find("# HELP socketshpp_http_timeouts_total "), std::string::npos);
}

// ---------------------------------------------------------------------------
// multipart/form-data upload end to end
// ---------------------------------------------------------------------------

TEST(HttpServerMultipartTest, BinaryUploadRoundTrip)
{
    HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);
    server.route("/upload", [](const HttpRequest& req, HttpResponse& res) {
        auto form = SocketsHpp::http::server::multipart::parse(req);
        if (!form)
        {
            res.set_content(SocketsHpp::http::server::multipart::errorMessage(form.error));
            return 400;
        }
        const auto* file = form.find("file");
        if (file == nullptr)
        {
            return 422;
        }
        res.set_content(file->filename + ":" + std::to_string(file->data.size()) + ":" + file->body(),
            "application/octet-stream");
        return 200;
    });
    server.start();

    std::string payload;
    for (int i = 0; i < 3000; ++i)
    {
        payload.push_back(static_cast<char>(i * 7));
    }
    const std::string boundary = "xYzBoundary";
    const std::string body = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"b.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n" + payload + "\r\n--" + boundary + "--\r\n";
    RawConn c(port);
    ASSERT_TRUE(c.connected());
    RawResponse r = c.request("POST /upload HTTP/1.1\r\nHost: t\r\nContent-Type: multipart/form-data; boundary=" +
        boundary + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
    ASSERT_TRUE(r.complete);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.body, "b.bin:3000:" + payload);

    RawResponse bad = c.request("POST /upload HTTP/1.1\r\nHost: t\r\nContent-Type: text/plain\r\nContent-Length: 1\r\n\r\nx");
    EXPECT_EQ(bad.code, 400);
    EXPECT_EQ(bad.body, "Content-Type is not multipart");
    server.stop();
}
