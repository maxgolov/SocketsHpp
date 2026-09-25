// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
//
// Functional tests for HttpClient / SSEClient against a tiny scripted raw-socket
// HTTP server. POSIX-only (excluded from the Windows build in test/CMakeLists.txt).

#include <gtest/gtest.h>
#include <SocketsHpp/http/client/sse_client.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __linux__
#  include <dirent.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace SocketsHpp::http::client;

namespace
{
    bool writeAll(int fd, const std::string& data)
    {
        size_t off = 0;
        while (off < data.size())
        {
            ssize_t n = ::send(fd, data.data() + off, data.size() - off, 0);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return false;
            off += static_cast<size_t>(n);
        }
        return true;
    }

    // Wait until the peer closes (or 5 s pass). Used to hold a connection open so a
    // client that wrongly waits for more data would hang until its read timeout.
    void waitForPeerClose(int fd)
    {
        char buf[256];
        while (true)
        {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return;
        }
    }

    std::string lowerCopy(std::string s)
    {
        for (auto& c : s)
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    /// Minimal scripted HTTP/1.1 server: accepts connections sequentially, reads one
    /// request, records it, and hands it to a handler that writes the raw response.
    class ScriptedServer
    {
    public:
        using Handler = std::function<void(int fd, const std::string& request, const std::string& target)>;

        explicit ScriptedServer(Handler handler, int family = AF_INET) : m_handler(std::move(handler))
        {
            ::signal(SIGPIPE, SIG_IGN);
            m_listen = ::socket(family, SOCK_STREAM, 0);
            if (m_listen < 0)
                return;
            int on = 1;
            ::setsockopt(m_listen, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
            sockaddr_storage ss{};
            socklen_t len = 0;
            if (family == AF_INET6)
            {
                auto* a6 = reinterpret_cast<sockaddr_in6*>(&ss);
                a6->sin6_family = AF_INET6;
                a6->sin6_addr = in6addr_loopback;
                a6->sin6_port = 0;
                len = sizeof(sockaddr_in6);
            }
            else
            {
                auto* a4 = reinterpret_cast<sockaddr_in*>(&ss);
                a4->sin_family = AF_INET;
                a4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                a4->sin_port = 0;  // ephemeral
                len = sizeof(sockaddr_in);
            }
            if (::bind(m_listen, reinterpret_cast<sockaddr*>(&ss), len) != 0 || ::listen(m_listen, 64) != 0)
            {
                ::close(m_listen);
                m_listen = -1;
                return;
            }
            sockaddr_storage bound{};
            socklen_t blen = sizeof(bound);
            ::getsockname(m_listen, reinterpret_cast<sockaddr*>(&bound), &blen);
            m_family = family;
            m_port = (family == AF_INET6) ? ntohs(reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port)
                                          : ntohs(reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
            m_thread = std::thread([this] { run(); });
        }

        ~ScriptedServer() { stop(); }

        bool ok() const { return m_listen >= 0; }
        int port() const { return m_port; }

        std::string url(const std::string& target) const
        {
            const std::string host = (m_family == AF_INET6) ? "[::1]" : "127.0.0.1";
            return "http://" + host + ":" + std::to_string(m_port) + target;
        }

        std::vector<std::string> requests()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_requests;
        }

        void stop()
        {
            if (m_stop.exchange(true))
                return;
            if (m_listen >= 0)
            {
                // Wake accept() portably by connecting to ourselves.
                int s = ::socket(m_family, SOCK_STREAM, 0);
                sockaddr_storage ss{};
                socklen_t len = sizeof(ss);
                ::getsockname(m_listen, reinterpret_cast<sockaddr*>(&ss), &len);
                ::connect(s, reinterpret_cast<sockaddr*>(&ss), len);
                ::close(s);
            }
            if (m_thread.joinable())
                m_thread.join();
            if (m_listen >= 0)
                ::close(m_listen);
            m_listen = -1;
        }

    private:
        int m_listen = -1;
        int m_family = AF_INET;
        int m_port = 0;
        std::atomic<bool> m_stop{false};
        std::thread m_thread;
        Handler m_handler;
        std::mutex m_mutex;
        std::vector<std::string> m_requests;

        static std::string readRequest(int fd)
        {
            std::string req;
            char buf[4096];
            size_t headerEnd = std::string::npos;
            while ((headerEnd = req.find("\r\n\r\n")) == std::string::npos)
            {
                ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0)
                    return req;
                req.append(buf, static_cast<size_t>(n));
            }
            size_t contentLength = 0;
            const std::string lower = lowerCopy(req.substr(0, headerEnd));
            const size_t cl = lower.find("\r\ncontent-length:");
            if (cl != std::string::npos)
                contentLength = std::strtoul(lower.c_str() + cl + 17, nullptr, 10);
            while (req.size() < headerEnd + 4 + contentLength)
            {
                ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0)
                    break;
                req.append(buf, static_cast<size_t>(n));
            }
            return req;
        }

        void run()
        {
            while (!m_stop)
            {
                int c = ::accept(m_listen, nullptr, nullptr);
                if (c < 0)
                {
                    if (errno == EINTR)
                        continue;
                    break;
                }
                if (m_stop)
                {
                    ::close(c);
                    break;
                }
                timeval tv{};
                tv.tv_sec = 5;
                ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                const std::string req = readRequest(c);
                std::string target;
                const size_t sp1 = req.find(' ');
                const size_t sp2 = (sp1 == std::string::npos) ? sp1 : req.find(' ', sp1 + 1);
                if (sp2 != std::string::npos)
                    target = req.substr(sp1 + 1, sp2 - sp1 - 1);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_requests.push_back(req);
                }
                if (!req.empty())
                    m_handler(c, req, target);
                ::close(c);
            }
        }
    };

    std::string pathOf(const std::string& target)
    {
        return target.substr(0, target.find('?'));
    }

    // The scripted responses used by most tests.
    void defaultHandler(int fd, const std::string& req, const std::string& target)
    {
        const std::string path = pathOf(target);
        if (path == "/chunked")
        {
            // Lowercase header names, chunk extensions, uppercase hex, trailer field.
            writeAll(fd,
                     "HTTP/1.1 200 OK\r\n"
                     "transfer-encoding: chunked\r\n"
                     "content-type: text/plain\r\n"
                     "\r\n"
                     "5;name=value\r\nHello\r\n"
                     "7 ; ext=\"q\"\r\n, World\r\n"
                     "A\r\n0123456789\r\n"
                     "0\r\n"
                     "x-trailer: yes\r\n"
                     "\r\n");
        }
        else if (path == "/chunked-slow")
        {
            // Same stream as /chunked but written a byte at a time.
            const std::string resp =
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "3\r\nabc\r\n2;x=y\r\nde\r\n0\r\n\r\n";
            for (char ch : resp)
            {
                writeAll(fd, std::string(1, ch));
            }
        }
        else if (path == "/cl")
        {
            writeAll(fd, "HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\nabcde");
            waitForPeerClose(fd);  // body completion must come from Content-Length, not EOF
        }
        else if (path == "/head" || path == "/204" || path == "/304")
        {
            const std::string status = (path == "/204") ? "204 No Content"
                                       : (path == "/304") ? "304 Not Modified" : "200 OK";
            writeAll(fd, "HTTP/1.1 " + status + "\r\nContent-Length: 100\r\n\r\n");
            waitForPeerClose(fd);  // client must not wait for a body
        }
        else if (path == "/until-close")
        {
            writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\npart1-");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            writeAll(fd, "part2");
        }
        else if (path == "/continue")
        {
            writeAll(fd, "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        }
        else if (path == "/bad-cl")
        {
            // The query selects which invalid Content-Length value to send.
            const std::string value = target.substr(target.find('?') + 1);
            std::string decoded;
            for (size_t i = 0; i < value.size(); i++)
            {
                if (value[i] == '%' && i + 2 < value.size())
                {
                    decoded += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                }
                else
                {
                    decoded += value[i];
                }
            }
            writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Length: " + decoded + "\r\n\r\nbody");
        }
        else if (path == "/bad-chunk")
        {
            const std::string which = target.substr(target.find('?') + 1);
            std::string body;
            if (which == "hex")
                body = "zz\r\nabc\r\n0\r\n\r\n";
            else if (which == "overflow")
                body = "ffffffffffffffffffff\r\nabc\r\n0\r\n\r\n";
            else if (which == "huge")
                body = "7fffffffffffffff\r\nabc\r\n0\r\n\r\n";
            else if (which == "neg")
                body = "-1\r\nabc\r\n0\r\n\r\n";
            else if (which == "nocrlf")
                body = "3\r\nabcXY0\r\n\r\n";
            else
                body = "\r\n";
            writeAll(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + body);
        }
        else if (path == "/echo")
        {
            writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(req.size()) + "\r\n\r\n" + req);
        }
        else if (path == "/stall")
        {
            writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc");
            waitForPeerClose(fd);
        }
        else if (path == "/dup-headers")
        {
            writeAll(fd,
                     "HTTP/1.1 200 OK\r\nX-Multi: a\r\nx-multi: b\r\nContent-Length: 2\r\n"
                     "Content-Length: 2\r\n\r\nhi");
        }
        else
        {
            writeAll(fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
        }
    }

    double secondsSince(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

#ifdef __linux__
    int countOpenFds()
    {
        int count = 0;
        DIR* dir = ::opendir("/proc/self/fd");
        if (!dir)
            return -1;
        while (::readdir(dir) != nullptr)
            count++;
        ::closedir(dir);
        return count;
    }
#endif
}  // namespace

class HttpClientTest : public ::testing::Test
{
protected:
    ScriptedServer server{defaultHandler};
    HttpClient client;

    void SetUp() override
    {
        ASSERT_TRUE(server.ok());
        client.setReadTimeout(3000);
        client.setConnectTimeout(3000);
    }
};

TEST_F(HttpClientTest, ChunkedWithLowercaseHeaderAndExtensions)
{
    HttpClientResponse response;
    ASSERT_TRUE(client.get(server.url("/chunked"), response));
    EXPECT_EQ(response.code, 200);
    EXPECT_TRUE(response.isChunked);
    EXPECT_EQ(response.body, "Hello, World0123456789");
    // Case-insensitive lookups
    EXPECT_EQ(response.getHeader("Content-Type"), "text/plain");
    EXPECT_EQ(response.getHeader("CONTENT-TYPE"), "text/plain");
    EXPECT_TRUE(response.hasHeader("Transfer-Encoding"));
}

TEST_F(HttpClientTest, ChunkedByteByByte)
{
    HttpClientResponse response;
    std::vector<std::string> chunks;
    response.chunkCallback = [&](const std::string& c) { chunks.push_back(c); };
    ASSERT_TRUE(client.get(server.url("/chunked-slow"), response));
    ASSERT_EQ(chunks.size(), 2u);  // one callback per HTTP chunk
    EXPECT_EQ(chunks[0], "abc");
    EXPECT_EQ(chunks[1], "de");
    EXPECT_TRUE(response.body.empty());
}

TEST_F(HttpClientTest, ContentLengthBody)
{
    HttpClientResponse response;
    auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(client.get(server.url("/cl"), response));
    EXPECT_LT(secondsSince(start), 2.0);
    EXPECT_EQ(response.code, 200);
    EXPECT_FALSE(response.isChunked);
    EXPECT_EQ(response.body, "abcde");
}

TEST_F(HttpClientTest, ContentLengthBodyStreamsToCallback)
{
    HttpClientResponse response;
    std::string streamed;
    bool completed = false;
    response.chunkCallback = [&](const std::string& c) { streamed += c; };
    response.onComplete = [&] { completed = true; };
    ASSERT_TRUE(client.get(server.url("/cl"), response));
    EXPECT_EQ(streamed, "abcde");
    EXPECT_TRUE(completed);
}

TEST_F(HttpClientTest, HeadDoesNotWaitForBody)
{
    HttpClientRequest request;
    request.method = "HEAD";
    request.uri = server.url("/head");
    HttpClientResponse response;
    auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(client.send(request, response));
    EXPECT_LT(secondsSince(start), 2.0);
    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.getHeader("content-length"), "100");
    EXPECT_TRUE(response.body.empty());
}

TEST_F(HttpClientTest, NoBodyStatusCodes)
{
    for (const char* path : {"/204", "/304"})
    {
        HttpClientResponse response;
        auto start = std::chrono::steady_clock::now();
        ASSERT_TRUE(client.get(server.url(path), response)) << path;
        EXPECT_LT(secondsSince(start), 2.0) << path;
        EXPECT_TRUE(response.body.empty()) << path;
    }
}

TEST_F(HttpClientTest, InterimContinueIsSkipped)
{
    HttpClientResponse response;
    ASSERT_TRUE(client.get(server.url("/continue"), response));
    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body, "ok");
}

TEST_F(HttpClientTest, ReadUntilClose)
{
    HttpClientResponse response;
    ASSERT_TRUE(client.get(server.url("/until-close"), response));
    EXPECT_EQ(response.body, "part1-part2");
}

TEST_F(HttpClientTest, ReadUntilCloseStreamsToCallback)
{
    HttpClientResponse response;
    std::string streamed;
    int calls = 0;
    response.chunkCallback = [&](const std::string& c) {
        streamed += c;
        calls++;
    };
    ASSERT_TRUE(client.get(server.url("/until-close"), response));
    EXPECT_EQ(streamed, "part1-part2");
    EXPECT_GE(calls, 1);
    EXPECT_TRUE(response.body.empty());
}

TEST_F(HttpClientTest, GarbageContentLengthFailsCleanly)
{
    // abc, -1, 12abc, overflow, empty, differing list, "+5"
    for (const char* value : {"abc", "-1", "12abc", "99999999999999999999999", "", "4,%205", "%2B4"})
    {
        HttpClientResponse response;
        bool ok = true;
        EXPECT_NO_THROW(ok = client.get(server.url(std::string("/bad-cl?") + value), response)) << value;
        EXPECT_FALSE(ok) << "Content-Length: " << value;
    }
    // Identical duplicates are fine.
    HttpClientResponse response;
    ASSERT_TRUE(client.get(server.url("/bad-cl?4,%204"), response));
    EXPECT_EQ(response.body, "body");
}

TEST_F(HttpClientTest, GarbageChunkSizeFailsCleanly)
{
    for (const char* which : {"hex", "overflow", "huge", "neg", "nocrlf", "empty"})
    {
        HttpClientResponse response;
        bool ok = true;
        EXPECT_NO_THROW(ok = client.get(server.url(std::string("/bad-chunk?") + which), response)) << which;
        EXPECT_FALSE(ok) << which;
    }
}

TEST_F(HttpClientTest, RequestLineIncludesQueryAndHostIncludesPort)
{
    HttpClientResponse response;
    ASSERT_TRUE(client.get(server.url("/echo?a=1&b=two"), response));
    const std::string& echoed = response.body;
    EXPECT_EQ(echoed.rfind("GET /echo?a=1&b=two HTTP/1.1\r\n", 0), 0u) << echoed;
    EXPECT_NE(echoed.find("\r\nHost: 127.0.0.1:" + std::to_string(server.port()) + "\r\n"), std::string::npos)
        << echoed;
}

TEST_F(HttpClientTest, PathWithoutQueryAndFragmentStripped)
{
    HttpClientResponse response;
    ASSERT_TRUE(client.get(server.url("/echo#frag"), response));
    EXPECT_EQ(response.body.rfind("GET /echo HTTP/1.1\r\n", 0), 0u) << response.body;
}

TEST_F(HttpClientTest, UserHeaderNotDuplicatedCaseInsensitively)
{
    HttpClientRequest request;
    request.uri = server.url("/echo");
    request.headers["host"] = "custom.example";
    request.headers["accept"] = "text/plain";
    HttpClientResponse response;
    ASSERT_TRUE(client.send(request, response));
    const std::string lower = lowerCopy(response.body);
    EXPECT_NE(lower.find("\r\nhost: custom.example\r\n"), std::string::npos);
    EXPECT_EQ(lower.find("host:"), lower.rfind("host:"));  // exactly one Host header
    EXPECT_EQ(lower.find("accept:"), lower.rfind("accept:"));
}

TEST_F(HttpClientTest, PostSendsBody)
{
    HttpClientResponse response;
    const std::string payload(200000, 'p');  // large enough to need several send() calls
    ASSERT_TRUE(client.post(server.url("/echo"), payload, response));
    EXPECT_NE(response.body.find("\r\nContent-Length: 200000\r\n"), std::string::npos);
    EXPECT_EQ(response.body.substr(response.body.size() - payload.size()), payload);
}

TEST_F(HttpClientTest, DuplicateHeadersCombined)
{
    HttpClientResponse response;
    ASSERT_TRUE(client.get(server.url("/dup-headers"), response));
    EXPECT_EQ(response.getHeader("X-MULTI"), "a, b");
    EXPECT_EQ(response.body, "hi");
}

TEST_F(HttpClientTest, ReadTimeoutApplied)
{
    client.setReadTimeout(300);
    HttpClientResponse response;
    auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(client.get(server.url("/stall"), response));
    const double elapsed = secondsSince(start);
    EXPECT_GE(elapsed, 0.2);
    EXPECT_LT(elapsed, 3.0);
}

TEST_F(HttpClientTest, CancelUnblocksRequest)
{
    client.setReadTimeout(0);  // no timeout: only cancel() can end this request
    HttpClientResponse response;
    std::atomic<bool> done{false};
    bool result = true;
    std::thread t([&] {
        result = client.get(server.url("/stall"), response);
        done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    client.cancel();
    for (int i = 0; i < 200 && !done; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(done);
    t.join();
    EXPECT_FALSE(result);
}

TEST_F(HttpClientTest, ConnectFailureReturnsFalse)
{
    // Grab an ephemeral port and close it so nothing listens there.
    ScriptedServer tmp(defaultHandler);
    ASSERT_TRUE(tmp.ok());
    const std::string url = tmp.url("/");
    tmp.stop();
    HttpClientResponse response;
    EXPECT_FALSE(client.get(url, response));
}

TEST_F(HttpClientTest, InvalidUrlsFailCleanly)
{
    HttpClientResponse response;
    for (const char* url : {"http://", "http://[::1", "http://host:notaport/", "http://host:70000/", "http://:80/"})
    {
        bool ok = true;
        EXPECT_NO_THROW(ok = client.get(url, response)) << url;
        EXPECT_FALSE(ok) << url;
    }
}

#ifdef __linux__
TEST_F(HttpClientTest, NoFileDescriptorLeak)
{
    // Warm up (lazy allocations, resolver files, ...).
    for (int i = 0; i < 5; i++)
    {
        HttpClientResponse response;
        ASSERT_TRUE(client.get(server.url("/chunked"), response));
    }
    const int before = countOpenFds();
    ASSERT_GT(before, 0);
    constexpr int kRequests = 2000;
    for (int i = 0; i < kRequests; i++)
    {
        HttpClientResponse response;
        ASSERT_TRUE(client.get(server.url("/chunked"), response)) << "request " << i;
    }
    // Also exercise failure paths.
    for (int i = 0; i < 50; i++)
    {
        HttpClientResponse response;
        EXPECT_FALSE(client.get(server.url("/bad-cl?abc"), response));
    }
    // The server thread closes its side asynchronously; allow it to settle.
    int after = countOpenFds();
    for (int i = 0; i < 100 && after > before + 2; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        after = countOpenFds();
    }
    EXPECT_LE(after, before + 2) << "before=" << before << " after=" << after;
}
#endif

// ---------------------------------------------------------------------------
// Redirects and scheme handling
// ---------------------------------------------------------------------------

namespace
{
    void reply(int fd, const std::string& status, const std::string& extraHeaders, const std::string& body)
    {
        writeAll(fd, "HTTP/1.1 " + status + "\r\n" + extraHeaders + "Content-Length: " +
                         std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
    }

    /// Echo the raw request back as the body.
    void echo(int fd, const std::string& req) { reply(fd, "200 OK", "", req); }
}  // namespace

TEST(HttpClientRedirectTest, FollowsRelativeRedirect)
{
    ScriptedServer server([](int fd, const std::string& req, const std::string& target) {
        if (pathOf(target) == "/old/page")
            reply(fd, "302 Found", "Location: new?x=1\r\n", "moved");
        else
            echo(fd, req);
    });
    ASSERT_TRUE(server.ok());
    HttpClient client;
    HttpClientResponse response;
    ASSERT_TRUE(client.get(server.url("/old/page"), response));
    EXPECT_EQ(response.code, 200);
    EXPECT_EQ(response.body.rfind("GET /old/new?x=1 HTTP/1.1\r\n", 0), 0u) << response.body;
}

TEST(HttpClientRedirectTest, SeeOtherSwitchesPostToGetWithoutBody)
{
    ScriptedServer server([](int fd, const std::string& req, const std::string& target) {
        if (pathOf(target) == "/submit")
            reply(fd, "303 See Other", "Location: /result\r\n", "");
        else
            echo(fd, req);
    });
    ASSERT_TRUE(server.ok());
    HttpClient client;
    HttpClientResponse response;
    ASSERT_TRUE(client.post(server.url("/submit"), "payload", response));
    EXPECT_EQ(response.body.rfind("GET /result HTTP/1.1\r\n", 0), 0u) << response.body;
    EXPECT_EQ(lowerCopy(response.body).find("content-length"), std::string::npos);
    EXPECT_EQ(response.body.find("payload"), std::string::npos);
}

TEST(HttpClientRedirectTest, TemporaryRedirectKeepsMethodAndBody)
{
    ScriptedServer server([](int fd, const std::string& req, const std::string& target) {
        if (pathOf(target) == "/a")
            reply(fd, "307 Temporary Redirect", "Location: /b\r\n", "");
        else
            echo(fd, req);
    });
    ASSERT_TRUE(server.ok());
    HttpClient client;
    HttpClientResponse response;
    ASSERT_TRUE(client.post(server.url("/a"), "payload", response));
    EXPECT_EQ(response.body.rfind("POST /b HTTP/1.1\r\n", 0), 0u) << response.body;
    EXPECT_NE(response.body.find("\r\n\r\npayload"), std::string::npos) << response.body;
}

TEST(HttpClientRedirectTest, RedirectLoopStopsAtLimit)
{
    std::atomic<int> hits{0};
    ScriptedServer server([&](int fd, const std::string&, const std::string&) {
        ++hits;
        reply(fd, "302 Found", "Location: /loop\r\n", "");
    });
    ASSERT_TRUE(server.ok());
    HttpClient client;
    client.setMaxRedirects(3);
    HttpClientResponse response;
    EXPECT_FALSE(client.get(server.url("/loop"), response));
    EXPECT_EQ(hits.load(), 4);  // original request + 3 redirects
}

TEST(HttpClientRedirectTest, DisabledReturnsRedirectResponse)
{
    ScriptedServer server([](int fd, const std::string&, const std::string&) {
        reply(fd, "301 Moved Permanently", "Location: /elsewhere\r\n", "moved");
    });
    ASSERT_TRUE(server.ok());
    HttpClient client;
    client.setFollowRedirects(false);
    HttpClientResponse response;
    ASSERT_TRUE(client.get(server.url("/x"), response));
    EXPECT_EQ(response.code, 301);
    EXPECT_EQ(response.getHeader("location"), "/elsewhere");
    EXPECT_EQ(response.body, "moved");
}

TEST(HttpClientRedirectTest, CredentialsDroppedOnCrossOriginRedirectOnly)
{
    ScriptedServer target([](int fd, const std::string& req, const std::string&) { echo(fd, req); });
    ASSERT_TRUE(target.ok());
    const std::string elsewhere = target.url("/landing");
    ScriptedServer origin([&](int fd, const std::string& req, const std::string& t) {
        if (pathOf(t) == "/same")
            reply(fd, "302 Found", "Location: /echo\r\n", "");
        else if (pathOf(t) == "/cross")
            reply(fd, "302 Found", "Location: " + elsewhere + "\r\n", "");
        else
            echo(fd, req);
    });
    ASSERT_TRUE(origin.ok());

    HttpClient client;
    HttpClientRequest same;
    same.method = METHOD_GET;
    same.uri = origin.url("/same");
    same.setHeader("Authorization", "Bearer secret");
    HttpClientResponse r1;
    ASSERT_TRUE(client.send(same, r1));
    EXPECT_NE(lowerCopy(r1.body).find("\r\nauthorization: bearer secret\r\n"), std::string::npos) << r1.body;

    HttpClientRequest cross;
    cross.method = METHOD_GET;
    cross.uri = origin.url("/cross");
    cross.setHeader("Authorization", "Bearer secret");
    cross.setHeader("Cookie", "sid=1");
    HttpClientResponse r2;
    ASSERT_TRUE(client.send(cross, r2));
    EXPECT_EQ(r2.body.rfind("GET /landing HTTP/1.1\r\n", 0), 0u) << r2.body;
    EXPECT_EQ(lowerCopy(r2.body).find("authorization"), std::string::npos) << r2.body;
    EXPECT_EQ(lowerCopy(r2.body).find("cookie"), std::string::npos) << r2.body;
    // Host must be recomputed for the new origin.
    EXPECT_NE(r2.body.find("\r\nHost: 127.0.0.1:" + std::to_string(target.port()) + "\r\n"), std::string::npos)
        << r2.body;
}

TEST(HttpClientRedirectTest, StreamingCallbackSeesOnlyFinalResponse)
{
    ScriptedServer server([](int fd, const std::string&, const std::string& target) {
        if (pathOf(target) == "/start")
            reply(fd, "302 Found", "Location: /final\r\n", "REDIRECT-BODY");
        else
            reply(fd, "200 OK", "", "FINAL-BODY");
    });
    ASSERT_TRUE(server.ok());
    HttpClient client;
    HttpClientResponse response;
    std::string streamed;
    response.chunkCallback = [&](const std::string& data) { streamed += data; };
    ASSERT_TRUE(client.get(server.url("/start"), response));
    EXPECT_EQ(streamed, "FINAL-BODY");
}

TEST(HttpClientRedirectTest, UnfollowedRedirectIsDeliveredToStreamingCallbacks)
{
    // A 3xx without Location is the final response: its body and completion must
    // reach the streaming callbacks even though redirect following is on.
    ScriptedServer moved([](int fd, const std::string&, const std::string&) {
        reply(fd, "302 Found", "", "no location here");
    });
    ASSERT_TRUE(moved.ok());
    HttpClient client;
    HttpClientResponse response;
    std::string streamed;
    bool completed = false;
    response.chunkCallback = [&](const std::string& data) { streamed += data; };
    response.onComplete = [&]() { completed = true; };
    ASSERT_TRUE(client.get(moved.url("/"), response));
    EXPECT_EQ(response.code, 302);
    EXPECT_EQ(streamed, "no location here");
    EXPECT_TRUE(completed);
}

TEST(HttpClientRequestTest, SendDoesNotModifyTheCallersRequest)
{
    ScriptedServer server([](int fd, const std::string& req, const std::string&) { echo(fd, req); });
    ASSERT_TRUE(server.ok());
    for (bool follow : {true, false})
    {
        HttpClient client;
        client.setFollowRedirects(follow);
        HttpClientRequest request;
        request.method = METHOD_POST;
        request.uri = server.url("/x");
        request.body = "payload";
        request.setHeader("X-Custom", "1");
        const auto headersBefore = request.headers;
        HttpClientResponse response;
        ASSERT_TRUE(client.send(request, response));
        EXPECT_EQ(request.headers, headersBefore) << "follow=" << follow;
        EXPECT_NE(lowerCopy(response.body).find("\r\nhost: "), std::string::npos) << "Host still sent";
    }
}

TEST(HttpClientSchemeTest, HttpsIsRejectedNotSentInCleartext)
{
    ScriptedServer server([](int fd, const std::string& req, const std::string&) { echo(fd, req); });
    ASSERT_TRUE(server.ok());
    HttpClient client;
    HttpClientResponse response;
    EXPECT_FALSE(client.get("https://127.0.0.1:" + std::to_string(server.port()) + "/", response));
    EXPECT_TRUE(server.requests().empty()) << "no bytes may be sent for an https URL";
}

TEST(HttpClientSchemeTest, RedirectToHttpsIsNotFollowedInCleartext)
{
    std::atomic<int> hits{0};
    ScriptedServer server([&](int fd, const std::string&, const std::string&) {
        const int n = ++hits;
        if (n == 1)
            reply(fd, "301 Moved Permanently",
                  "Location: https://127.0.0.1:" + std::to_string(0) + "/secure\r\n", "");
        else
            reply(fd, "200 OK", "", "should not happen");
    });
    ASSERT_TRUE(server.ok());
    HttpClient client;
    HttpClientResponse response;
    EXPECT_FALSE(client.get(server.url("/"), response));
    EXPECT_EQ(hits.load(), 1);
}

TEST(HttpClientIPv6Test, BracketedIPv6Host)
{
    ScriptedServer server(defaultHandler, AF_INET6);
    if (!server.ok())
    {
        GTEST_SKIP() << "IPv6 loopback not available";
    }
    HttpClient client;
    client.setReadTimeout(3000);
    HttpClientResponse response;
    ASSERT_TRUE(client.get(server.url("/echo?v=6"), response));
    EXPECT_EQ(response.body.rfind("GET /echo?v=6 HTTP/1.1\r\n", 0), 0u);
    EXPECT_NE(response.body.find("\r\nHost: [::1]:" + std::to_string(server.port()) + "\r\n"), std::string::npos);
}

TEST(HttpClientUrlTest, ParsedUrlComponents)
{
    detail::ParsedUrl u;
    ASSERT_TRUE(u.parse("http://[::1]:8080/a/b?x=1#frag"));
    EXPECT_EQ(u.host, "::1");
    EXPECT_EQ(u.port, 8080);
    EXPECT_EQ(u.target(), "/a/b?x=1");
    EXPECT_EQ(u.hostHeader(), "[::1]:8080");

    u = detail::ParsedUrl();
    ASSERT_TRUE(u.parse("https://user:pw@example.com/p"));
    EXPECT_EQ(u.host, "example.com");
    EXPECT_EQ(u.port, 443);
    EXPECT_EQ(u.hostHeader(), "example.com");

    u = detail::ParsedUrl();
    ASSERT_TRUE(u.parse("example.com:80?q"));
    EXPECT_EQ(u.target(), "/?q");
    EXPECT_EQ(u.hostHeader(), "example.com");

    u = detail::ParsedUrl();
    ASSERT_TRUE(u.parse("http://example.com:8443"));
    EXPECT_EQ(u.target(), "/");
    EXPECT_EQ(u.hostHeader(), "example.com:8443");
}

// ---------------------------------------------------------------------------
// SSEClient over a raw (non-chunked, read-until-close) event stream
// ---------------------------------------------------------------------------

TEST(SSEClientTest, NonChunkedStreamDeliversEventsAndCloseUnblocks)
{
    ScriptedServer server([](int fd, const std::string&, const std::string&) {
        writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n");
        writeAll(fd, "id: 1\r\ndata: one\r\n\r\n");
        writeAll(fd, "data: two\r");  // CR-only line ending split from its blank line
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        writeAll(fd, "\r");
        waitForPeerClose(fd);  // hold the stream open until the client goes away
    });
    ASSERT_TRUE(server.ok());

    SSEClient client;
    std::mutex m;
    std::vector<std::string> received;
    std::atomic<bool> returned{false};
    std::thread t([&] {
        client.connect(server.url("/events"), [&](const SSEEvent& e) {
            std::lock_guard<std::mutex> lock(m);
            received.push_back(e.data);
        });
        returned = true;
    });

    for (int i = 0; i < 300; i++)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            if (received.size() >= 2)
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    {
        std::lock_guard<std::mutex> lock(m);
        ASSERT_EQ(received.size(), 2u);
        EXPECT_EQ(received[0], "one");
        EXPECT_EQ(received[1], "two");
    }
    EXPECT_FALSE(returned);  // stream still open

    client.close();
    for (int i = 0; i < 200 && !returned; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(returned) << "close() did not unblock the SSE reader";
    t.join();
    EXPECT_EQ(client.getLastEventId(), "1");
}

TEST(SSEClientTest, AutoReconnectSendsLastEventIdAndHonorsRetry)
{
    std::atomic<int> connection{0};
    ScriptedServer server([&](int fd, const std::string&, const std::string&) {
        const int n = ++connection;
        if (n == 1)
        {
            // Chunked stream that ends: client must reconnect after `retry` ms.
            const std::string events = "retry: 50\nid: 7\ndata: first\n\n";
            char size[16];
            std::snprintf(size, sizeof(size), "%zx", events.size());
            writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\n\r\n" +
                             std::string(size) + "\r\n" + events + "\r\n0\r\n\r\n");
        }
        else
        {
            writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\ndata: second\n\n");
            waitForPeerClose(fd);
        }
    });
    ASSERT_TRUE(server.ok());

    SSEClient client;
    client.setAutoReconnect(true, 10000);  // server's retry: 50 must override this
    std::vector<std::string> received;
    auto start = std::chrono::steady_clock::now();
    const bool ok = client.connect(server.url("/events"), [&](const SSEEvent& e) {
        received.push_back(e.data);
        if (e.data == "second")
            client.close();  // close from inside the callback
    });
    EXPECT_TRUE(ok);
    EXPECT_LT(secondsSince(start), 5.0);
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0], "first");
    EXPECT_EQ(received[1], "second");

    auto reqs = server.requests();
    ASSERT_GE(reqs.size(), 2u);
    EXPECT_EQ(lowerCopy(reqs[0]).find("last-event-id"), std::string::npos);
    EXPECT_NE(lowerCopy(reqs[1]).find("\r\nlast-event-id: 7\r\n"), std::string::npos) << reqs[1];
    EXPECT_NE(lowerCopy(reqs[1]).find("\r\naccept: text/event-stream\r\n"), std::string::npos);
}

TEST(SSEClientTest, RequestHeadersSentOnEveryConnectAndCannotOverrideAccept)
{
    std::atomic<int> connection{0};
    ScriptedServer server([&](int fd, const std::string&, const std::string&) {
        const int n = ++connection;
        if (n == 1)
        {
            writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n"
                         "retry: 20\ndata: first\n\n");
            // Close after the first event so the client reconnects.
        }
        else
        {
            writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\ndata: second\n\n");
            waitForPeerClose(fd);
        }
    });
    ASSERT_TRUE(server.ok());

    SSEClient client;
    client.setAutoReconnect(true, 20);
    client.setRequestHeader("Authorization", "Bearer abc");
    client.setRequestHeader("Mcp-Session-Id", "sess-1");
    client.setRequestHeader("Accept", "application/json");  // must not win
    std::vector<std::string> received;
    const bool ok = client.connect(server.url("/events"), [&](const SSEEvent& e) {
        received.push_back(e.data);
        if (e.data == "second")
            client.close();
    });
    EXPECT_TRUE(ok);
    ASSERT_EQ(received.size(), 2u);

    auto reqs = server.requests();
    ASSERT_GE(reqs.size(), 2u);
    for (size_t i = 0; i < 2; ++i)
    {
        const std::string req = lowerCopy(reqs[i]);
        EXPECT_NE(req.find("\r\nauthorization: bearer abc\r\n"), std::string::npos) << reqs[i];
        EXPECT_NE(req.find("\r\nmcp-session-id: sess-1\r\n"), std::string::npos) << reqs[i];
        EXPECT_NE(req.find("\r\naccept: text/event-stream\r\n"), std::string::npos) << reqs[i];
        EXPECT_EQ(req.find("accept: application/json"), std::string::npos) << reqs[i];
        EXPECT_EQ(req.find("session="), std::string::npos) << "session id must not be in the URL";
    }
}

TEST(SSEClientTest, ConnectAfterCloseKeepsAutoReconnect)
{
    std::atomic<int> connections{0};
    ScriptedServer server([&](int fd, const std::string&, const std::string&) {
        ++connections;
        // One event, then end the stream so the client has to reconnect.
        writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\nretry: 10\ndata: x\n\n");
    });
    ASSERT_TRUE(server.ok());

    SSEClient client;
    client.setAutoReconnect(true, 10);
    for (int round = 0; round < 2; ++round)
    {
        const int before = connections.load();
        int events = 0;
        client.connect(server.url("/events"), [&](const SSEEvent&) {
            if (++events == 2)
                client.close();  // after one reconnect
        });
        EXPECT_EQ(events, 2) << "round " << round;
        EXPECT_GE(connections.load() - before, 2) << "round " << round << " did not reconnect";
    }
}

TEST(SSEClientTest, RequestHeaderNamesAreCaseInsensitive)
{
    ScriptedServer server([](int fd, const std::string&, const std::string&) {
        writeAll(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\ndata: x\n\n");
    });
    ASSERT_TRUE(server.ok());
    SSEClient client;
    client.setRequestHeader("authorization", "Bearer old");
    client.setRequestHeader("Authorization", "Bearer new");
    client.connect(server.url("/events"), [&](const SSEEvent&) {});
    auto reqs = server.requests();
    ASSERT_EQ(reqs.size(), 1u);
    const std::string req = lowerCopy(reqs[0]);
    EXPECT_NE(req.find("authorization: bearer new"), std::string::npos) << reqs[0];
    EXPECT_EQ(req.find("bearer old"), std::string::npos) << reqs[0];
    EXPECT_TRUE(client.getLastEventId().empty());
}

TEST(SSEClientTest, ReconnectRetriesAfterConnectFailureAndCloseStopsBackoff)
{
    // Nothing listens on this port: every attempt fails; close() must stop the loop.
    ScriptedServer tmp(defaultHandler);
    ASSERT_TRUE(tmp.ok());
    const std::string url = tmp.url("/events");
    tmp.stop();

    SSEClient client;
    client.setAutoReconnect(true, 20);
    std::atomic<int> errors{0};
    std::atomic<bool> returned{false};
    std::thread t([&] {
        client.connect(url, [](const SSEEvent&) {}, [&](const std::string&) { errors++; });
        returned = true;
    });
    for (int i = 0; i < 300 && errors < 3; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_GE(errors.load(), 3);  // retried several times
    client.close();
    for (int i = 0; i < 200 && !returned; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(returned);
    t.join();
}

TEST(SSEClientTest, NonOkStatusDoesNotReconnect)
{
    std::atomic<int> connections{0};
    ScriptedServer server([&](int fd, const std::string&, const std::string&) {
        connections++;
        writeAll(fd, "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
    });
    ASSERT_TRUE(server.ok());
    SSEClient client;
    client.setAutoReconnect(true, 10);
    std::string error;
    EXPECT_FALSE(client.connect(server.url("/events"), [](const SSEEvent&) {},
                                [&](const std::string& e) { error = e; }));
    EXPECT_EQ(connections.load(), 1);
    EXPECT_NE(error.find("503"), std::string::npos);
}
