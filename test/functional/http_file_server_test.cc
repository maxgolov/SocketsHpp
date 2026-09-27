// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

// End-to-end tests for HttpFileServer: real requests against files in a temporary
// document root (portable: runs on POSIX and Windows).

#include <gtest/gtest.h>
#include "sockets.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace SOCKETSHPP_NS::http::server;
using namespace SOCKETSHPP_NS::http::client;
namespace fs = std::filesystem;

namespace
{
    void writeFile(const fs::path& path, const std::string& content)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    class HttpFileServerTest : public ::testing::Test
    {
    protected:
        fs::path base;  // contains www/ (document root) and a sibling www-private/
        std::unique_ptr<HttpFileServer> server;
        std::string url;

        void SetUp() override
        {
            base = fs::temp_directory_path() /
                   ("socketshpp_files_" + std::to_string(std::random_device{}()));
            writeFile(base / "www" / "index.html", "<h1>home</h1>");
            writeFile(base / "www" / "style.css", "body{}");
            writeFile(base / "www" / "app.js", "let x;");
            writeFile(base / "www" / "docs" / "index.html", "<p>docs</p>");
            writeFile(base / "www" / "data" / "items.json", "[1,2]");
            writeFile(base / "www" / "with space.txt", "spaced");
            writeFile(base / "www" / "empty.txt", "");
            writeFile(base / "www" / "v1.2" / "docs" / "index.html", "<p>v1.2 docs</p>");
            writeFile(base / "www" / "UPPER.CSS", "p{}");
            writeFile(base / "www-private" / "secret.txt", "TOP SECRET");

            server = std::make_unique<HttpFileServer>("127.0.0.1", 0, (base / "www").string());
            server->route("/api/", [](const HttpRequest&, HttpResponse& res) {
                res.set_content("api");
                return 200;
            });
            server->InitializeFileEndpoint(*server);
            server->start();
            url = "http://127.0.0.1:" + std::to_string(server->getListeningPort());
        }

        void TearDown() override
        {
            server.reset();
            std::error_code ec;
            fs::remove_all(base, ec);
        }

        HttpClientResponse get(const std::string& path, const std::string& method = METHOD_GET)
        {
            HttpClient client;
            client.setReadTimeout(5000);
            HttpClientRequest request;
            request.method = method;
            request.uri = url + path;
            HttpClientResponse response;
            EXPECT_TRUE(client.send(request, response)) << path;
            return response;
        }
    };
}  // namespace

TEST_F(HttpFileServerTest, ServesFilesWithMimeTypes)
{
    auto css = get("/style.css");
    EXPECT_EQ(css.code, 200);
    EXPECT_EQ(css.body, "body{}");
    EXPECT_EQ(css.getHeader("Content-Type"), "text/css");

    auto json = get("/data/items.json");
    EXPECT_EQ(json.code, 200);
    EXPECT_EQ(json.body, "[1,2]");
    EXPECT_NE(json.getHeader("Content-Type").find("json"), std::string::npos);

    auto js = get("/app.js");
    EXPECT_EQ(js.code, 200);
    EXPECT_NE(js.getHeader("Content-Type").find("javascript"), std::string::npos);
}

TEST_F(HttpFileServerTest, DirectoryServesIndexHtml)
{
    auto root = get("/");
    EXPECT_EQ(root.code, 200);
    EXPECT_EQ(root.body, "<h1>home</h1>");
    EXPECT_EQ(root.getHeader("Content-Type"), "text/html");

    EXPECT_EQ(get("/docs").body, "<p>docs</p>");
    EXPECT_EQ(get("/docs/").body, "<p>docs</p>");
}

TEST_F(HttpFileServerTest, QueryStringIgnoredAndPathPercentDecoded)
{
    EXPECT_EQ(get("/style.css?v=123").body, "body{}");
    auto spaced = get("/with%20space.txt");
    EXPECT_EQ(spaced.code, 200);
    EXPECT_EQ(spaced.body, "spaced");
    EXPECT_EQ(get("/bad%zzescape.txt").code, 400);
}

TEST_F(HttpFileServerTest, EmptyFileAndHead)
{
    auto empty = get("/empty.txt");
    EXPECT_EQ(empty.code, 200);
    EXPECT_TRUE(empty.body.empty());

    auto head = get("/style.css", METHOD_HEAD);
    EXPECT_EQ(head.code, 200);
    EXPECT_TRUE(head.body.empty());
    EXPECT_EQ(head.getHeader("Content-Length"), "6");
}

TEST_F(HttpFileServerTest, MissingFileIs404)
{
    auto missing = get("/nope.txt");
    EXPECT_EQ(missing.code, 404);
    EXPECT_EQ(get("/nodir/").code, 404);
}

TEST_F(HttpFileServerTest, NothingOutsideTheRootIsServed)
{
    for (const std::string path : {"/../www-private/secret.txt", "/%2e%2e/www-private/secret.txt",
                                   "/docs/../../www-private/secret.txt", "/..%2fwww-private%2fsecret.txt"})
    {
        auto res = get(path);
        EXPECT_NE(res.code, 200) << path;
        EXPECT_EQ(res.body.find("TOP SECRET"), std::string::npos) << path;
    }
}

TEST_F(HttpFileServerTest, MoreSpecificRoutesWinOverFileEndpoint)
{
    // The "/" file endpoint was registered after "/api/" here, but registration order
    // does not matter: the longest matching prefix wins.
    auto api = get("/api/anything");
    EXPECT_EQ(api.code, 200);
    EXPECT_EQ(api.body, "api");
}

TEST_F(HttpFileServerTest, DottedDirectoriesAndUppercaseExtensions)
{
    auto docs = get("/v1.2/docs");
    EXPECT_EQ(docs.code, 200);
    EXPECT_EQ(docs.body, "<p>v1.2 docs</p>");
    EXPECT_EQ(get("/v1.2/docs/").body, "<p>v1.2 docs</p>");

    auto upper = get("/UPPER.CSS");
    EXPECT_EQ(upper.code, 200);
    EXPECT_EQ(upper.getHeader("Content-Type"), "text/css");
}

TEST_F(HttpFileServerTest, DestroyWithThreadPoolAndRequestInFlightIsSafe)
{
    server.reset();
    server = std::make_unique<HttpFileServer>("127.0.0.1", 0, (base / "www").string());
    server->enableThreadPool(2);
    server->InitializeFileEndpoint(*server);
    server->start();
    url = "http://127.0.0.1:" + std::to_string(server->getListeningPort());
    for (int i = 0; i < 20; ++i)
        EXPECT_EQ(get("/style.css").code, 200);
    server.reset();  // must not touch the destroyed handler from a worker
}
