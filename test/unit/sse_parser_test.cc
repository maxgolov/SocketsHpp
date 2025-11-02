// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <gtest/gtest.h>
#include <SocketsHpp/http/client/sse_client.h>
#include <string>
#include <vector>

using namespace SocketsHpp::http::client;

// SSE Parser Tests
TEST(SSEParserTest, SimpleEvent) {
    SSEParser parser;
    
    auto events = parser.parseChunk("data: hello\n\n");
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "hello");
    EXPECT_TRUE(events[0].event.empty());
    EXPECT_TRUE(events[0].id.empty());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(SSEParserTest, EventWithType) {
    SSEParser parser;
    
    auto events = parser.parseChunk("event: message\ndata: test\n\n");
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event, "message");
    EXPECT_EQ(events[0].data, "test");
}

TEST(SSEParserTest, EventWithId) {
    SSEParser parser;
    
    auto events = parser.parseChunk("id: 123\ndata: content\n\n");
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].id, "123");
    EXPECT_EQ(events[0].data, "content");
}

TEST(SSEParserTest, EventWithRetry) {
    SSEParser parser;
    
    auto events = parser.parseChunk("retry: 5000\ndata: test\n\n");
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].retry, 5000);
    EXPECT_EQ(events[0].data, "test");
}

TEST(SSEParserTest, MultiLineData) {
    SSEParser parser;
    
    auto events = parser.parseChunk("data: line 1\ndata: line 2\ndata: line 3\n\n");
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "line 1\nline 2\nline 3");
}

TEST(SSEParserTest, AllFieldsTogether) {
    SSEParser parser;
    
    std::string chunk = 
        "event: custom\n"
        "id: evt-456\n"
        "retry: 3000\n"
        "data: {\"type\":\"update\"}\n"
        "\n";
    
    auto events = parser.parseChunk(chunk);
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event, "custom");
    EXPECT_EQ(events[0].id, "evt-456");
    EXPECT_EQ(events[0].retry, 3000);
    EXPECT_EQ(events[0].data, "{\"type\":\"update\"}");
}

TEST(SSEParserTest, MultipleEvents) {
    SSEParser parser;
    
    std::string chunk = 
        "data: event1\n"
        "\n"
        "data: event2\n"
        "\n"
        "data: event3\n"
        "\n";
    
    auto events = parser.parseChunk(chunk);
    
    ASSERT_EQ(events.size(), 3);
    EXPECT_EQ(events[0].data, "event1");
    EXPECT_EQ(events[1].data, "event2");
    EXPECT_EQ(events[2].data, "event3");
}

TEST(SSEParserTest, ChunkedData) {
    SSEParser parser;
    
    // Simulate data arriving in chunks
    auto events1 = parser.parseChunk("data: hel");
    EXPECT_EQ(events1.size(), 0); // No complete event yet
    
    auto events2 = parser.parseChunk("lo\n");
    EXPECT_EQ(events2.size(), 0); // Still no complete event
    
    auto events3 = parser.parseChunk("\n");
    ASSERT_EQ(events3.size(), 1); // Now we have a complete event
    EXPECT_EQ(events3[0].data, "hello");
}

TEST(SSEParserTest, CommentLines) {
    SSEParser parser;
    
    std::string chunk = 
        ": this is a comment\n"
        "data: actual data\n"
        ": another comment\n"
        "\n";
    
    auto events = parser.parseChunk(chunk);
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "actual data");
}

TEST(SSEParserTest, EmptyDataField) {
    SSEParser parser;
    
    auto events = parser.parseChunk("data:\n\n");
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "");
}

TEST(SSEParserTest, DataWithColon) {
    SSEParser parser;
    
    auto events = parser.parseChunk("data: key:value\n\n");
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "key:value");
}

TEST(SSEParserTest, JSONData) {
    SSEParser parser;
    
    std::string json_chunk = 
        "event: update\n"
        "id: msg-1\n"
        "data: {\"type\":\"notification\",\"content\":{\"message\":\"Hello\"}}\n"
        "\n";
    
    auto events = parser.parseChunk(json_chunk);
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event, "update");
    EXPECT_EQ(events[0].id, "msg-1");
    // Just verify it's valid JSON-like string
    EXPECT_TRUE(events[0].data.find("\"type\"") != std::string::npos);
}

TEST(SSEParserTest, LargeEvent) {
    SSEParser parser;
    
    std::string large_data(10000, 'X');
    std::string chunk = "data: " + large_data + "\n\n";
    
    auto events = parser.parseChunk(chunk);
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data.size(), 10000);
    EXPECT_EQ(events[0].data, large_data);
}

TEST(SSEParserTest, Reset) {
    SSEParser parser;
    
    // Parse partial event
    auto events1 = parser.parseChunk("data: partial");
    
    // Reset parser
    parser.reset();
    
    // Parse new complete event
    auto events2 = parser.parseChunk("data: complete\n\n");
    
    ASSERT_EQ(events2.size(), 1);
    EXPECT_EQ(events2[0].data, "complete");
}

TEST(SSEParserTest, EventBoundaryDetection) {
    SSEParser parser;
    
    // Single \n should not trigger event end
    auto events1 = parser.parseChunk("data: test\n");
    EXPECT_EQ(events1.size(), 0);
    
    // Double \n should trigger event end
    auto events2 = parser.parseChunk("\n");
    ASSERT_EQ(events2.size(), 1);
    EXPECT_EQ(events2[0].data, "test");
}

TEST(SSEParserTest, EventIdPersistence) {
    SSEParser parser;
    
    // First event with ID
    auto events1 = parser.parseChunk("id: persistent-id\ndata: event1\n\n");
    
    ASSERT_EQ(events1.size(), 1);
    EXPECT_EQ(events1[0].id, "persistent-id");
    
    // Second event without ID
    auto events2 = parser.parseChunk("data: event2\n\n");
    
    ASSERT_EQ(events2.size(), 1);
    // Each event is independent in the parser (SSEClient tracks last ID)
    EXPECT_TRUE(events2[0].id.empty());
}

TEST(SSEParserTest, EmptyId) {
    SSEParser parser;
    
    // Set initial ID
    auto events1 = parser.parseChunk("id: initial\ndata: event1\n\n");
    EXPECT_EQ(events1[0].id, "initial");
    
    // Empty ID should be empty in the event
    auto events2 = parser.parseChunk("id:\ndata: event2\n\n");
    
    EXPECT_TRUE(events2[0].id.empty());
}
