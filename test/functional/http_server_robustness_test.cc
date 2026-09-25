// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// Functional robustness tests for HttpServer, driven over raw POSIX sockets
// so that malformed / adversarial requests can be sent byte-for-byte.
// Every test uses its own server on an ephemeral port (safe under ctest -j).

#include <gtest/gtest.h>

#include <SocketsHpp/http/server/http_server.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace SocketsHpp::http::server;

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;  // macOS: SO_NOSIGPIPE is set on the socket instead
#endif

namespace
{
    struct RawResponse
    {
        bool complete = false;  // A full response was parsed
        std::string statusLine;
        int code = 0;
        std::map<std::string, std::string> headers;  // lower-cased names
        std::string body;

        bool hasHeader(const std::string& lowerName) const { return headers.count(lowerName) != 0; }
        std::string header(const std::string& lowerName) const
        {
            auto it = headers.find(lowerName);
            return it == headers.end() ? std::string() : it->second;
        }
    };

    std::string toLower(std::string s)
    {
        for (char& c : s)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }

    class RawClient
    {
    public:
        explicit RawClient(int port, int rcvbuf = 0)
        {
            m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
#if defined(SO_NOSIGPIPE)
            int one = 1;
            ::setsockopt(m_fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
            if (rcvbuf > 0)
            {
                ::setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
            }
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(static_cast<uint16_t>(port));
            m_connected = ::connect(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
        }

        ~RawClient()
        {
            if (m_fd >= 0)
            {
                ::close(m_fd);
            }
        }

        RawClient(const RawClient&) = delete;
        RawClient& operator=(const RawClient&) = delete;

        bool connected() const { return m_connected; }

        bool send(const std::string& data)
        {
            size_t off = 0;
            while (off < data.size())
            {
                ssize_t n = ::send(m_fd, data.data() + off, data.size() - off, kSendFlags);
                if (n <= 0)
                {
                    return false;
                }
                off += static_cast<size_t>(n);
            }
            return true;
        }

        /// Read more bytes into the internal buffer. Returns false on EOF/error/timeout.
        bool fill(int timeoutMs = 5000)
        {
            pollfd pfd{ m_fd, POLLIN, 0 };
            int r = ::poll(&pfd, 1, timeoutMs);
            if (r <= 0)
            {
                return false;
            }
            char buf[65536];
            ssize_t n = ::recv(m_fd, buf, sizeof(buf), 0);
            if (n <= 0)
            {
                m_eof = true;
                return false;
            }
            m_buffer.append(buf, static_cast<size_t>(n));
            return true;
        }

        /// True once the peer has closed (or reset) the connection with nothing left to read.
        bool waitForClose(int timeoutMs = 5000)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (!m_eof && std::chrono::steady_clock::now() < deadline)
            {
                fill(100);
            }
            return m_eof;
        }

        /// Parse one response from the stream (Content-Length, chunked, or bodyless).
        RawResponse readResponse(bool headRequest = false, int timeoutMs = 5000)
        {
            RawResponse res;
            size_t headerEnd;
            while ((headerEnd = m_buffer.find("\r\n\r\n")) == std::string::npos)
            {
                if (!fill(timeoutMs))
                {
                    return res;
                }
            }
            std::string head = m_buffer.substr(0, headerEnd);
            m_buffer.erase(0, headerEnd + 4);

            size_t eol = head.find("\r\n");
            res.statusLine = head.substr(0, eol);
            if (res.statusLine.size() >= 12)
            {
                res.code = std::atoi(res.statusLine.c_str() + 9);
            }
            size_t pos = (eol == std::string::npos) ? head.size() : eol + 2;
            while (pos < head.size())
            {
                size_t next = head.find("\r\n", pos);
                if (next == std::string::npos)
                {
                    next = head.size();
                }
                std::string line = head.substr(pos, next - pos);
                size_t colon = line.find(':');
                if (colon != std::string::npos)
                {
                    std::string value = line.substr(colon + 1);
                    value.erase(0, value.find_first_not_of(' '));
                    res.headers[toLower(line.substr(0, colon))] = value;
                }
                pos = next + 2;
            }

            const bool bodyless = headRequest || (res.code >= 100 && res.code < 200) || res.code == 204 || res.code == 304;
            if (bodyless)
            {
                res.complete = true;
                return res;
            }
            if (toLower(res.header("transfer-encoding")) == "chunked")
            {
                for (;;)
                {
                    size_t lineEnd;
                    while ((lineEnd = m_buffer.find("\r\n")) == std::string::npos)
                    {
                        if (!fill(timeoutMs))
                        {
                            return res;
                        }
                    }
                    size_t size = std::stoul(m_buffer.substr(0, lineEnd), nullptr, 16);
                    while (m_buffer.size() < lineEnd + 2 + size + 2)
                    {
                        if (!fill(timeoutMs))
                        {
                            return res;
                        }
                    }
                    res.body.append(m_buffer, lineEnd + 2, size);
                    m_buffer.erase(0, lineEnd + 2 + size + 2);
                    if (size == 0)
                    {
                        res.complete = true;
                        return res;
                    }
                }
            }
            if (res.hasHeader("content-length"))
            {
                size_t length = std::stoul(res.header("content-length"));
                while (m_buffer.size() < length)
                {
                    if (!fill(timeoutMs))
                    {
                        return res;
                    }
                }
                res.body = m_buffer.substr(0, length);
                m_buffer.erase(0, length);
                res.complete = true;
                return res;
            }
            // Close-delimited
            while (fill(timeoutMs))
            {
            }
            res.body = m_buffer;
            m_buffer.clear();
            res.complete = m_eof;
            return res;
        }

    private:
        int m_fd = -1;
        bool m_connected = false;
        bool m_eof = false;
        std::string m_buffer;
    };

    class HttpServerRobustnessTest : public ::testing::Test
    {
    protected:
        std::unique_ptr<HttpServer> server;
        int port = 0;
        std::atomic<int> dispatchCount{ 0 };

        void SetUp() override
        {
            server = std::make_unique<HttpServer>("127.0.0.1", 0);
            port = server->getListeningPort();
            ASSERT_GT(port, 0);

            server->route("/drop", [](const HttpRequest&, HttpResponse&) { return -1; });
            server->route("/echo", [this](const HttpRequest& req, HttpResponse& res) {
                ++dispatchCount;
                res.set_content(req.content);
                return 200;
            });
            server->route("/nocontent", [](const HttpRequest&, HttpResponse& res) {
                res.body = "must not be sent";
                res.set_header("Content-Length", "16");
                return 204;
            });
        }

        void start()
        {
            // Catch-all registered last: handlers are matched by prefix, in order.
            // It declines DELETE/OPTIONS (returns 0 without setting a status) so
            // those fall through to the server's built-in handling.
            server->route("/", [this](const HttpRequest& req, HttpResponse& res) {
                if (req.method == "DELETE" || req.method == "OPTIONS")
                {
                    return 0;
                }
                ++dispatchCount;
                res.set_content(req.uri);
                return 200;
            });
            server->start();
        }

        void TearDown() override
        {
            if (server)
            {
                server->stop();
            }
        }

        RawResponse roundTrip(const std::string& request)
        {
            RawClient client(port);
            EXPECT_TRUE(client.connected());
            EXPECT_TRUE(client.send(request));
            return client.readResponse();
        }
    };
}

// --- Handler returning -1 (use-after-free regression) ------------------------

TEST_F(HttpServerRobustnessTest, HandlerReturningMinusOneClosesConnection)
{
    start();
    for (int i = 0; i < 20; ++i)
    {
        RawClient client(port);
        ASSERT_TRUE(client.connected());
        // Pipeline a second request behind it: must not be processed on a freed connection.
        ASSERT_TRUE(client.send("GET /drop HTTP/1.1\r\nHost: x\r\n\r\nGET /after HTTP/1.1\r\nHost: x\r\n\r\n"));
        EXPECT_TRUE(client.waitForClose());
    }
    // Server is still healthy
    auto res = roundTrip("GET /alive HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "/alive");
}

// --- Send backpressure (sendMore with EWOULDBLOCK) ---------------------------

TEST_F(HttpServerRobustnessTest, LargeResponseToSlowReaderIsComplete)
{
    const size_t size = 8 * 1024 * 1024;
    std::string payload(size, '\0');
    for (size_t i = 0; i < size; ++i)
    {
        payload[i] = static_cast<char>('a' + (i * 7919) % 26);
    }
    server->route("/big", [&payload](const HttpRequest&, HttpResponse& res) {
        res.set_content(payload, "application/octet-stream");
        return 200;
    });
    start();

    RawClient client(port, 4096);
    ASSERT_TRUE(client.connected());
    ASSERT_TRUE(client.send("GET /big HTTP/1.1\r\nHost: x\r\n\r\n"));
    // Let the server fill the socket buffers and hit EWOULDBLOCK.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto res = client.readResponse(false, 10000);
    ASSERT_TRUE(res.complete);
    EXPECT_EQ(res.code, 200);
    ASSERT_EQ(res.body.size(), size);
    EXPECT_TRUE(res.body == payload);

    // Connection is still usable afterwards (keep-alive)
    ASSERT_TRUE(client.send("GET /again HTTP/1.1\r\nHost: x\r\n\r\n"));
    auto res2 = client.readResponse();
    EXPECT_EQ(res2.code, 200);
    EXPECT_EQ(res2.body, "/again");
}

// --- Content-Length strictness ------------------------------------------------

TEST_F(HttpServerRobustnessTest, InvalidContentLengthIsRejected)
{
    start();
    const char* bad[] = { "+5", "5abc", "5, 7", "-1", "0x5", "5 5", "", "99999999999999999999999999" };
    for (const char* value : bad)
    {
        RawClient client(port);
        ASSERT_TRUE(client.connected());
        ASSERT_TRUE(client.send(std::string("POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: ") + value +
                                "\r\n\r\nhello"));
        auto res = client.readResponse();
        EXPECT_EQ(res.statusLine, "HTTP/1.1 400 Bad Request") << "Content-Length: '" << value << "'";
        EXPECT_EQ(res.header("connection"), "close");
        EXPECT_TRUE(client.waitForClose());
    }
    EXPECT_EQ(dispatchCount.load(), 0);
}

TEST_F(HttpServerRobustnessTest, ValidContentLengthWithOptionalWhitespace)
{
    start();
    auto res = roundTrip("POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: \t5 \r\nConnection: close\r\n\r\nhello");
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "hello");
}

TEST_F(HttpServerRobustnessTest, DuplicateContentLengthIsRejected)
{
    start();
    for (const char* second : { "5", "7" })
    {
        auto res = roundTrip(std::string("POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: ") +
                             second + "\r\n\r\nhello");
        EXPECT_EQ(res.code, 400) << second;
    }
    EXPECT_EQ(dispatchCount.load(), 0);
}

TEST_F(HttpServerRobustnessTest, OversizedContentLengthIs413)
{
    server->setMaxRequestContentSize(1024);
    start();
    auto res = roundTrip("POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 1025\r\n\r\n");
    EXPECT_EQ(res.code, 413);
    EXPECT_EQ(dispatchCount.load(), 0);
}

// --- Transfer-Encoding ----------------------------------------------------------

TEST_F(HttpServerRobustnessTest, UnsupportedTransferEncodingIs501)
{
    start();
    auto res = roundTrip("POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n");
    EXPECT_EQ(res.statusLine, "HTTP/1.1 501 Not Implemented");
    EXPECT_EQ(dispatchCount.load(), 0);
}

TEST_F(HttpServerRobustnessTest, TransferEncodingWithContentLengthIs400)
{
    start();
    // Classic CL.TE smuggling attempt
    auto res = roundTrip("POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 6\r\nTransfer-Encoding: chunked\r\n\r\n"
                         "0\r\n\r\nG");
    EXPECT_EQ(res.code, 400);
    EXPECT_EQ(dispatchCount.load(), 0);
}

TEST_F(HttpServerRobustnessTest, ChunkedRequestBodyIsDecoded)
{
    start();
    RawClient client(port);
    ASSERT_TRUE(client.connected());
    // Chunk extension, trailer field, mixed case "Chunked", then a pipelined request.
    ASSERT_TRUE(client.send("POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: Chunked\r\n\r\n"
                            "5\r\nhello\r\n6;name=value\r\n world\r\n0\r\nX-Trailer: t\r\n\r\n"
                            "GET /next HTTP/1.1\r\nHost: x\r\n\r\n"));
    auto res = client.readResponse();
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "hello world");
    auto res2 = client.readResponse();
    EXPECT_EQ(res2.code, 200);
    EXPECT_EQ(res2.body, "/next");
}

TEST_F(HttpServerRobustnessTest, ChunkedRequestBodySentInPieces)
{
    start();
    RawClient client(port);
    ASSERT_TRUE(client.connected());
    const std::string request = "POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n"
                                "a\r\n0123456789\r\n3\r\nabc\r\n0\r\n\r\n";
    for (char c : request)
    {
        ASSERT_TRUE(client.send(std::string(1, c)));
    }
    auto res = client.readResponse();
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "0123456789abc");
}

TEST_F(HttpServerRobustnessTest, MalformedChunkedBodyIsRejected)
{
    server->setMaxRequestContentSize(1024);
    start();
    struct Case
    {
        const char* body;
        int code;
    } cases[] = {
        { "zz\r\nhello\r\n0\r\n\r\n", 400 },                    // not hex
        { "5\r\nhelloXX\r\n0\r\n\r\n", 400 },                    // chunk longer than declared
        { "5 junk\r\nhello\r\n0\r\n\r\n", 400 },                 // garbage after size
        { "FFFFFFFFFFFFFFFFFFFF\r\n", 413 },                     // size overflow
        { "401\r\n", 413 },                                      // exceeds body limit (1025 > 1024)
    };
    for (const auto& c : cases)
    {
        auto res = roundTrip(std::string("POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n") + c.body);
        EXPECT_EQ(res.code, c.code) << c.body;
    }
    EXPECT_EQ(dispatchCount.load(), 0);
}

TEST_F(HttpServerRobustnessTest, TransferEncodingInHttp10Is400)
{
    start();
    auto res = roundTrip("POST /echo HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    EXPECT_EQ(res.code, 400);
}

// --- Pipelining and line endings ---------------------------------------------

TEST_F(HttpServerRobustnessTest, PipelinedRequestsWithMixedLineEndings)
{
    start();
    const char* pipelines[] = {
        "GET /a HTTP/1.1\nHost: x\n\nGET /b HTTP/1.1\r\nHost: x\r\n\r\n",
        "GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\nHost: x\n\n",
        "GET /a HTTP/1.1\r\nHost: x\n\r\nGET /b HTTP/1.1\nHost: x\r\n\n",
    };
    for (const char* pipeline : pipelines)
    {
        RawClient client(port);
        ASSERT_TRUE(client.connected());
        ASSERT_TRUE(client.send(pipeline));
        auto first = client.readResponse();
        auto second = client.readResponse();
        EXPECT_EQ(first.code, 200) << pipeline;
        EXPECT_EQ(first.body, "/a") << pipeline;
        EXPECT_EQ(second.code, 200) << pipeline;
        EXPECT_EQ(second.body, "/b") << pipeline;
    }
}

TEST_F(HttpServerRobustnessTest, LeadingEmptyLinesAreIgnored)
{
    start();
    auto res = roundTrip("\r\n\r\nGET /x HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "/x");
}

// --- Error handling -------------------------------------------------------------

TEST_F(HttpServerRobustnessTest, InvalidDeleteDoesNotTerminateSession)
{
    start();
    const std::string session = server->createSession();
    // Parse error (bad Content-Length) on a DELETE carrying a valid session id
    auto res = roundTrip("DELETE / HTTP/1.1\r\nHost: x\r\nMcp-Session-Id: " + session +
                         "\r\nContent-Length: nope\r\n\r\n");
    EXPECT_EQ(res.code, 400);
    EXPECT_TRUE(server->validateSession(session));

    // A valid DELETE still works
    auto ok = roundTrip("DELETE / HTTP/1.1\r\nHost: x\r\nMcp-Session-Id: " + session + "\r\nConnection: close\r\n\r\n");
    EXPECT_EQ(ok.code, 200);
    EXPECT_FALSE(server->validateSession(session));
}

TEST_F(HttpServerRobustnessTest, RouteHandlingDeleteTakesPrecedence)
{
    std::atomic<int> routeDeletes{ 0 };
    server->route("/mcp", [&routeDeletes](const HttpRequest& req, HttpResponse& res) {
        if (req.method == "DELETE")
        {
            ++routeDeletes;
            res.set_status(204);  // handled via set_status() while returning 0
        }
        else if (req.method == "OPTIONS")
        {
            res.set_status(200);
            res.set_header("Allow", "GET, POST, DELETE, OPTIONS");
        }
        return 0;
    });
    start();
    const std::string session = server->createSession();

    auto del = roundTrip("DELETE /mcp HTTP/1.1\r\nHost: x\r\nMcp-Session-Id: " + session + "\r\nConnection: close\r\n\r\n");
    EXPECT_EQ(del.code, 204);
    EXPECT_EQ(routeDeletes.load(), 1);
    EXPECT_TRUE(server->validateSession(session));  // built-in session store untouched

    auto opt = roundTrip("OPTIONS /mcp HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_EQ(opt.code, 200);
    EXPECT_EQ(opt.header("allow"), "GET, POST, DELETE, OPTIONS");

    // Unhandled OPTIONS falls back to the built-in (CORS disabled => 405)
    auto opt2 = roundTrip("OPTIONS /other HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_EQ(opt2.code, 405);
}

TEST_F(HttpServerRobustnessTest, ErrorStatusLineHasProtocol)
{
    start();
    auto res = roundTrip("GARBAGE\r\n\r\n");
    EXPECT_EQ(res.statusLine, "HTTP/1.1 400 Bad Request");

    auto res2 = roundTrip("GET / HTTP/2.0\r\n\r\n");
    EXPECT_EQ(res2.statusLine, "HTTP/1.1 505 HTTP Version Not Supported");

    auto res3 = roundTrip("GET / HTTP/1.1x\r\n\r\n");
    EXPECT_EQ(res3.statusLine, "HTTP/1.1 400 Bad Request");
}

TEST_F(HttpServerRobustnessTest, NoStaleRequestStateOnKeepAlive)
{
    start();
    RawClient client(port);
    ASSERT_TRUE(client.connected());
    ASSERT_TRUE(client.send("GET /first HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
    auto first = client.readResponse();
    EXPECT_EQ(first.statusLine, "HTTP/1.0 200 OK");
    EXPECT_EQ(first.header("connection"), "keep-alive");

    ASSERT_TRUE(client.send("BOGUS\r\n\r\n"));
    auto second = client.readResponse();
    EXPECT_EQ(second.statusLine, "HTTP/1.1 400 Bad Request");
    EXPECT_EQ(dispatchCount.load(), 1);
}

TEST_F(HttpServerRobustnessTest, NoContentResponseHasNoBodyOrLength)
{
    start();
    RawClient client(port);
    ASSERT_TRUE(client.connected());
    ASSERT_TRUE(client.send("GET /nocontent HTTP/1.1\r\nHost: x\r\n\r\nGET /after HTTP/1.1\r\nHost: x\r\n\r\n"));
    auto res = client.readResponse();
    EXPECT_EQ(res.statusLine, "HTTP/1.1 204 No Content");
    EXPECT_FALSE(res.hasHeader("content-length"));
    // If a body had been sent, it would corrupt the next response on the connection.
    auto next = client.readResponse();
    EXPECT_EQ(next.statusLine, "HTTP/1.1 200 OK");
    EXPECT_EQ(next.body, "/after");
}

TEST_F(HttpServerRobustnessTest, ResponseHeaders)
{
    start();
    auto res = roundTrip("GET /h HTTP/1.1\r\nHost: x\r\nConnection: Upgrade, CLOSE\r\n\r\n");
    EXPECT_EQ(res.code, 200);
    EXPECT_TRUE(res.hasHeader("server"));
    EXPECT_FALSE(res.hasHeader("host"));
    EXPECT_EQ(res.header("connection"), "close");
    EXPECT_EQ(res.header("content-length"), "2");
}

TEST_F(HttpServerRobustnessTest, HeaderValuesAreTrimmedAndDuplicatesCombined)
{
    std::string seen;
    server->route("/hdr", [&seen](const HttpRequest& req, HttpResponse& res) {
        seen = req.get_header_value("X-Test") + "|" + req.get_header_value("X-Multi");
        res.set_content("ok");
        return 200;
    });
    start();
    auto res = roundTrip("GET /hdr HTTP/1.1\r\nHost: x\r\nX-Test:\tvalue \t\r\nX-Multi: a\r\nx-multi: b\r\n"
                         "Connection: close\r\n\r\n");
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(seen, "value|a, b");
}

TEST_F(HttpServerRobustnessTest, WhitespaceBeforeColonIsRejected)
{
    start();
    auto res = roundTrip("POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length : 5\r\n\r\nhello");
    EXPECT_EQ(res.code, 400);
}

// --- Streaming ------------------------------------------------------------------

TEST_F(HttpServerRobustnessTest, StreamCallbackNotCalledAfterEndOfStream)
{
    std::atomic<int> calls{ 0 };
    std::atomic<int> ends{ 0 };
    const std::string chunk(512 * 1024, 'q');
    server->route("/stream", [&](const HttpRequest&, HttpResponse& res) {
        res.set_status(200);
        res.send_chunk_stream(
            [&calls, &chunk]() -> std::string {
                int n = ++calls;
                return n <= 4 ? chunk : std::string();
            },
            [&ends]() { ++ends; });
        return 200;
    });
    start();

    RawClient client(port, 4096);
    ASSERT_TRUE(client.connected());
    ASSERT_TRUE(client.send("GET /stream HTTP/1.1\r\nHost: x\r\n\r\n"));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto res = client.readResponse(false, 10000);
    ASSERT_TRUE(res.complete);
    EXPECT_EQ(res.body.size(), 4 * chunk.size());

    // Keep-alive: a follow-up request must not re-trigger the stream callback.
    ASSERT_TRUE(client.send("GET /after HTTP/1.1\r\nHost: x\r\n\r\n"));
    auto next = client.readResponse();
    EXPECT_EQ(next.body, "/after");
    EXPECT_EQ(calls.load(), 5);
    EXPECT_EQ(ends.load(), 1);
}

// --- Lifetime -------------------------------------------------------------------

TEST(HttpServerLifetimeTest, StopThenDestroyDoesNotCloseReusedDescriptor)
{
    auto server = std::make_unique<HttpServer>("127.0.0.1", 0);
    server->start();
    server->stop();
    server->stop();  // idempotent

    // The listener's descriptor number is free again; a new pipe is likely to get it.
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    server.reset();  // must not close the listener fd a second time
    EXPECT_NE(::fcntl(fds[0], F_GETFD), -1);
    EXPECT_NE(::fcntl(fds[1], F_GETFD), -1);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(HttpServerLifetimeTest, DestroyWithoutStopWhileClientsConnected)
{
    for (int i = 0; i < 5; ++i)
    {
        auto server = std::make_unique<HttpServer>("127.0.0.1", 0);
        server->route("/", [](const HttpRequest&, HttpResponse& res) {
            res.set_content("ok");
            return 200;
        });
        server->start();
        RawClient idle(server->getListeningPort());
        RawClient active(server->getListeningPort());
        ASSERT_TRUE(active.send("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
        EXPECT_EQ(active.readResponse().code, 200);
        server.reset();  // no stop(): the destructor must join the reactor first
        EXPECT_TRUE(active.waitForClose());
    }
}

// --- Thread pool dispatch ------------------------------------------------------
// With enableThreadPool(), handlers run on worker threads; the reactor must keep
// serving other connections and the response path must behave exactly as before.

namespace
{
    class HttpServerThreadPoolTest : public HttpServerRobustnessTest
    {
    protected:
        void SetUp() override
        {
            HttpServerRobustnessTest::SetUp();
            server->enableThreadPool(4);
        }
    };
}  // namespace

TEST_F(HttpServerThreadPoolTest, SlowHandlerDoesNotBlockOtherRequests)
{
    std::atomic<bool> release{false};
    server->route("/slow", [&release](const HttpRequest&, HttpResponse& res) {
        for (int i = 0; i < 500 && !release.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        res.set_content("slow");
        return 200;
    });
    start();

    RawClient slow(port);
    ASSERT_TRUE(slow.connected());
    ASSERT_TRUE(slow.send("GET /slow HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // /slow is now running

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i)
    {
        auto res = roundTrip("GET /fast" + std::to_string(i) + " HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        EXPECT_EQ(res.code, 200);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::seconds(2))
        << "fast requests were blocked behind the slow handler";

    release = true;
    auto res = slow.readResponse();
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "slow");
}

TEST_F(HttpServerThreadPoolTest, LargeResponseFromWorkerIsComplete)
{
    const size_t size = 8 * 1024 * 1024;
    std::string payload(size, '\0');
    for (size_t i = 0; i < size; ++i)
        payload[i] = static_cast<char>('a' + (i * 7919) % 26);
    server->route("/big", [&payload](const HttpRequest&, HttpResponse& res) {
        res.set_content(payload, "application/octet-stream");
        return 200;
    });
    start();

    RawClient client(port, 4096);
    ASSERT_TRUE(client.connected());
    ASSERT_TRUE(client.send("GET /big HTTP/1.1\r\nHost: x\r\n\r\n"));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // force EWOULDBLOCK on the worker
    auto res = client.readResponse(false, 10000);
    ASSERT_TRUE(res.complete);
    ASSERT_EQ(res.body.size(), size);
    EXPECT_TRUE(res.body == payload);

    ASSERT_TRUE(client.send("GET /again HTTP/1.1\r\nHost: x\r\n\r\n"));
    auto res2 = client.readResponse();
    EXPECT_EQ(res2.code, 200);
    EXPECT_EQ(res2.body, "/again");
}

TEST_F(HttpServerThreadPoolTest, PipelinedRequestsAnsweredInOrder)
{
    start();
    RawClient client(port);
    ASSERT_TRUE(client.connected());
    std::string batch;
    for (int i = 0; i < 10; ++i)
        batch += "GET /p" + std::to_string(i) + " HTTP/1.1\r\nHost: x\r\n\r\n";
    ASSERT_TRUE(client.send(batch));
    for (int i = 0; i < 10; ++i)
    {
        auto res = client.readResponse();
        ASSERT_EQ(res.code, 200);
        EXPECT_EQ(res.body, "/p" + std::to_string(i));
    }
}

TEST_F(HttpServerThreadPoolTest, ClientGoneWhileHandlerRunsIsSafe)
{
    std::atomic<int> finished{0};
    server->route("/linger", [&finished](const HttpRequest&, HttpResponse& res) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        res.set_content(std::string(64 * 1024, 'z'));
        ++finished;
        return 200;
    });
    start();
    for (int i = 0; i < 8; ++i)
    {
        RawClient client(port);
        ASSERT_TRUE(client.connected());
        ASSERT_TRUE(client.send("GET /linger HTTP/1.1\r\nHost: x\r\n\r\n"));
        // Destructor closes the socket while the handler is still running.
    }
    for (int i = 0; i < 300 && finished.load() < 8; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(finished.load(), 8);

    auto res = roundTrip("GET /alive HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_EQ(res.code, 200);
    EXPECT_EQ(res.body, "/alive");
}

TEST_F(HttpServerThreadPoolTest, HandlerReturningMinusOneClosesConnection)
{
    start();
    for (int i = 0; i < 20; ++i)
    {
        RawClient client(port);
        ASSERT_TRUE(client.connected());
        ASSERT_TRUE(client.send("GET /drop HTTP/1.1\r\nHost: x\r\n\r\nGET /after HTTP/1.1\r\nHost: x\r\n\r\n"));
        EXPECT_TRUE(client.waitForClose());
    }
    auto res = roundTrip("GET /alive HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    EXPECT_EQ(res.code, 200);
}

TEST_F(HttpServerThreadPoolTest, StopWithHandlerInFlightIsSafe)
{
    std::atomic<bool> entered{false};
    server->route("/busy", [&entered](const HttpRequest&, HttpResponse& res) {
        entered = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        res.set_content("late");
        return 200;
    });
    start();
    RawClient client(port);
    ASSERT_TRUE(client.connected());
    ASSERT_TRUE(client.send("GET /busy HTTP/1.1\r\nHost: x\r\n\r\n"));
    for (int i = 0; i < 200 && !entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_TRUE(entered.load());
    server->stop();
    server.reset();  // waits for the worker; must not touch freed state
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
