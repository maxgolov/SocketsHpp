// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sockets.hpp>
#include <thread>
#include <chrono>
#include <atomic>

using namespace SocketsHpp::http::server;
using namespace SocketsHpp::http::client;

// Both http::server and http::client declare an SSEEvent type. These tests
// exercise the server-side event (which provides message()/custom()/format()),
// so use a uniquely-named alias to avoid an ambiguous-reference build error.
using ServerSSEEvent = SocketsHpp::http::server::SSEEvent;

class HttpStreamingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        testPort = 0;  // Assigned from the server's ephemeral port in each test
    }

    int testPort;
};

// Test basic chunked encoding
TEST_F(HttpStreamingTest, ChunkedEncodingBasic)
{
    HttpServer server("127.0.0.1", 0);  // ephemeral port: safe under ctest -j
    testPort = server.getListeningPort();
    ASSERT_GT(testPort, 0);
    
    // Handler that uses chunked encoding
    HttpRequestCallback chunkedHandler{[](HttpRequest const& req, HttpResponse& resp) {
        resp.code = 200;
        resp.streaming = true;
        resp.useChunkedEncoding = true;
        resp.headers[CONTENT_TYPE] = CONTENT_TYPE_TEXT;
        
        int chunkCount = 0;
        resp.streamCallback = [chunkCount]() mutable -> std::string {
            if (chunkCount < 3)
            {
                chunkCount++;
                return "Chunk " + std::to_string(chunkCount) + "\n";
            }
            return "";  // End stream
        };
        
        return 200;
    }};
    
    server["/chunked"] = chunkedHandler;
    
    // start() is non-blocking: the reactor runs on its own thread
    server.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Make client request
    HttpClient client;
    HttpClientResponse response;
    
    bool success = client.get("http://127.0.0.1:" + std::to_string(testPort) + "/chunked", response);
    
    server.stop();
    
    ASSERT_TRUE(success);
    EXPECT_EQ(200, response.code);
    EXPECT_TRUE(response.isChunked);
    EXPECT_EQ("Chunk 1\nChunk 2\nChunk 3\n", response.body);
}

// Test Server-Sent Events (SSE)
TEST_F(HttpStreamingTest, SSEEvents)
{
    HttpServer server("127.0.0.1", 0);  // ephemeral port: safe under ctest -j
    testPort = server.getListeningPort();
    ASSERT_GT(testPort, 0);
    
    HttpRequestCallback sseHandler{[](HttpRequest const& req, HttpResponse& resp) {
        resp.code = 200;
        resp.streaming = true;
        resp.headers[CONTENT_TYPE] = CONTENT_TYPE_SSE;
        
        int eventCount = 0;
        resp.streamCallback = [eventCount]() mutable -> std::string {
            if (eventCount < 2)
            {
                eventCount++;
                ServerSSEEvent evt = ServerSSEEvent::message("Event " + std::to_string(eventCount), std::to_string(eventCount));
                return evt.format();
            }
            return "";  // End stream
        };
        
        return 200;
    }};
    
    server["/events"] = sseHandler;
    
    server.start();  // non-blocking: the reactor runs on its own thread
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    HttpClient client;
    HttpClientResponse response;
    
    bool success = client.get("http://127.0.0.1:" + std::to_string(testPort) + "/events", response);
    
    server.stop();
    
    ASSERT_TRUE(success);
    EXPECT_EQ(200, response.code);
    EXPECT_EQ(CONTENT_TYPE_SSE, response.getHeader(CONTENT_TYPE));
    EXPECT_TRUE(response.body.find("id: 1") != std::string::npos);
    EXPECT_TRUE(response.body.find("data: Event 1") != std::string::npos);
    EXPECT_TRUE(response.body.find("id: 2") != std::string::npos);
    EXPECT_TRUE(response.body.find("data: Event 2") != std::string::npos);
}

// Test client chunked response parsing
TEST_F(HttpStreamingTest, ClientChunkedParsing)
{
    HttpServer server("127.0.0.1", 0);  // ephemeral port: safe under ctest -j
    testPort = server.getListeningPort();
    ASSERT_GT(testPort, 0);
    
    HttpRequestCallback handler{[](HttpRequest const& req, HttpResponse& resp) {
        resp.code = 200;
        resp.streaming = true;
        resp.headers[CONTENT_TYPE] = CONTENT_TYPE_TEXT;
        
        std::vector<std::string> chunks = {"First", "Second", "Third"};
        int index = 0;
        
        resp.streamCallback = [chunks, index]() mutable -> std::string {
            if (index < static_cast<int>(chunks.size()))
            {
                return chunks[index++];
            }
            return "";
        };
        
        return 200;
    }};
    
    server["/test"] = handler;
    
    server.start();  // non-blocking: the reactor runs on its own thread
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    HttpClient client;
    HttpClientResponse response;
    
    std::vector<std::string> receivedChunks;
    response.chunkCallback = [&receivedChunks](const std::string& chunk) {
        receivedChunks.push_back(chunk);
    };
    
    bool success = client.get("http://127.0.0.1:" + std::to_string(testPort) + "/test", response);
    
    server.stop();
    
    ASSERT_TRUE(success);
    EXPECT_EQ(200, response.code);
    ASSERT_EQ(3u, receivedChunks.size());
    EXPECT_EQ("First", receivedChunks[0]);
    EXPECT_EQ("Second", receivedChunks[1]);
    EXPECT_EQ("Third", receivedChunks[2]);
}

// Test HTTP POST with body
TEST_F(HttpStreamingTest, PostRequest)
{
    HttpServer server("127.0.0.1", 0);  // ephemeral port: safe under ctest -j
    testPort = server.getListeningPort();
    ASSERT_GT(testPort, 0);
    
    HttpRequestCallback echoHandler{[](HttpRequest const& req, HttpResponse& resp) {
        resp.code = 200;
        resp.headers[CONTENT_TYPE] = CONTENT_TYPE_TEXT;
        resp.body = "Received: " + req.content;
        return 200;
    }};
    
    server["/echo"] = echoHandler;
    
    server.start();  // non-blocking: the reactor runs on its own thread
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    HttpClient client;
    HttpClientResponse response;
    
    bool success = client.post("http://127.0.0.1:" + std::to_string(testPort) + "/echo", 
                                "Hello Server", response);
    
    server.stop();
    
    ASSERT_TRUE(success);
    EXPECT_EQ(200, response.code);
    EXPECT_EQ("Received: Hello Server", response.body);
}

// Test SSE event formatting
TEST(SSEEventTest, BasicFormatting)
{
    ServerSSEEvent evt = ServerSSEEvent::message("test data", "123");
    std::string formatted = evt.format();
    
    EXPECT_TRUE(formatted.find("id: 123") != std::string::npos);
    EXPECT_TRUE(formatted.find("data: test data") != std::string::npos);
    EXPECT_TRUE(formatted.find("\n\n") != std::string::npos);  // Event terminator
}

TEST(SSEEventTest, CustomEvent)
{
    ServerSSEEvent evt = ServerSSEEvent::custom("myevent", "payload", "456");
    evt.retry = 5000;
    std::string formatted = evt.format();
    
    EXPECT_TRUE(formatted.find("event: myevent") != std::string::npos);
    EXPECT_TRUE(formatted.find("id: 456") != std::string::npos);
    EXPECT_TRUE(formatted.find("data: payload") != std::string::npos);
    EXPECT_TRUE(formatted.find("retry: 5000") != std::string::npos);
}

TEST(SSEEventTest, MultilineData)
{
    ServerSSEEvent evt = ServerSSEEvent::message("line1\nline2\nline3");
    std::string formatted = evt.format();
    
    EXPECT_TRUE(formatted.find("data: line1") != std::string::npos);
    EXPECT_TRUE(formatted.find("data: line2") != std::string::npos);
    EXPECT_TRUE(formatted.find("data: line3") != std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
