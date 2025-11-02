// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <gtest/gtest.h>
#include <SocketsHpp/http/client/sse_client.h>
#include <string>
#include <vector>

using namespace SocketsHpp;

// SSE Parser Tests
TEST(SSEParserTest, SimpleEvent) {
    SSEParser parser;
    
    auto events = auto events = parser.parseChunk("data: hello\n\n");
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "hello");
    EXPECT_TRUE(events[0].event.empty());
    EXPECT_TRUE(events[0].id.empty());
}

TEST(SSEParserTest, EventWithType) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    auto events = parser.parseChunk("event: message\ndata: test\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event, "message");
    EXPECT_EQ(events[0].data, "test");
}

TEST(SSEParserTest, EventWithId) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    auto events = parser.parseChunk("id: 123\ndata: content\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].id, "123");
    EXPECT_EQ(events[0].data, "content");
}

TEST(SSEParserTest, EventWithRetry) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    auto events = parser.parseChunk("retry: 5000\ndata: test\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].retry, 5000);
    EXPECT_EQ(events[0].data, "test");
}

TEST(SSEParserTest, MultiLineData) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    auto events = parser.parseChunk("data: line 1\ndata: line 2\ndata: line 3\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "line 1\nline 2\nline 3");
}

TEST(SSEParserTest, AllFieldsTogether) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    std::string chunk = 
        "event: custom\n"
        "id: evt-456\n"
        "retry: 3000\n"
        "data: {\"type\":\"update\"}\n"
        "\n";
    
    auto events = parser.parseChunk(chunk, [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event, "custom");
    EXPECT_EQ(events[0].id, "evt-456");
    EXPECT_EQ(events[0].retry, 3000);
    EXPECT_EQ(events[0].data, "{\"type\":\"update\"}");
}

TEST(SSEParserTest, MultipleEvents) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    std::string chunk = 
        "data: event1\n"
        "\n"
        "data: event2\n"
        "\n"
        "data: event3\n"
        "\n";
    
    auto events = parser.parseChunk(chunk, [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 3);
    EXPECT_EQ(events[0].data, "event1");
    EXPECT_EQ(events[1].data, "event2");
    EXPECT_EQ(events[2].data, "event3");
}

TEST(SSEParserTest, ChunkedData) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // Simulate data arriving in chunks
    auto events = parser.parseChunk("data: hel", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    EXPECT_EQ(events.size(), 0); // No complete event yet
    
    auto events = parser.parseChunk("lo\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    EXPECT_EQ(events.size(), 0); // Still no complete event
    
    auto events = parser.parseChunk("\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    ASSERT_EQ(events.size(), 1); // Now we have a complete event
    EXPECT_EQ(events[0].data, "hello");
}

TEST(SSEParserTest, CommentLines) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    std::string chunk = 
        ": this is a comment\n"
        "data: actual data\n"
        ": another comment\n"
        "\n";
    
    auto events = parser.parseChunk(chunk, [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "actual data");
}

TEST(SSEParserTest, EmptyDataField) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    auto events = parser.parseChunk("data:\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "");
}

TEST(SSEParserTest, DataWithColon) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    auto events = parser.parseChunk("data: key:value\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "key:value");
}

TEST(SSEParserTest, DataWithLeadingSpace) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // Leading space after colon should be removed
    auto events = parser.parseChunk("data: test\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "test");
    
    // No space after colon
    events.clear();
    auto events = parser.parseChunk("data:test2\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "test2");
}

TEST(SSEParserTest, InvalidRetryValue) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // Non-numeric retry should be ignored
    auto events = parser.parseChunk("retry: not-a-number\ndata: test\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].retry, -1); // Should be default value
}

TEST(SSEParserTest, UnknownFieldType) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // Unknown fields should be ignored
    auto events = parser.parseChunk("unknown: value\ndata: test\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "test");
}

TEST(SSEParserTest, OnlyNewlines) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // Just newlines should not trigger events
    auto events = parser.parseChunk("\n\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    EXPECT_EQ(events.size(), 0);
}

TEST(SSEParserTest, JSONData) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    std::string json_chunk = 
        "event: update\n"
        "id: msg-1\n"
        "data: {\"type\":\"notification\",\"content\":{\"message\":\"Hello\"}}\n"
        "\n";
    
    auto events = parser.parseChunk(json_chunk, [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event, "update");
    EXPECT_EQ(events[0].id, "msg-1");
    // Just verify it's valid JSON-like string
    EXPECT_TRUE(events[0].data.find("\"type\"") != std::string::npos);
}

TEST(SSEParserTest, LargeEvent) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    std::string large_data(10000, 'X');
    std::string chunk = "data: " + large_data + "\n\n";
    
    auto events = parser.parseChunk(chunk, [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data.size(), 10000);
    EXPECT_EQ(events[0].data, large_data);
}

TEST(SSEParserTest, Reset) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // Parse partial event
    auto events = parser.parseChunk("data: partial", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    // Reset parser
    parser.reset();
    
    // Parse new complete event
    auto events = parser.parseChunk("data: complete\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "complete");
}

TEST(SSEParserTest, CarriageReturnHandling) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // Test with \r\n line endings
    auto events = parser.parseChunk("data: test\r\n\r\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "test");
}

TEST(SSEParserTest, MultiLineDataWithEmptyLines) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // Multi-line data with some empty data fields
    auto events = parser.parseChunk("data: line1\ndata:\ndata: line3\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    // Each data field adds a line, empty data adds empty line
    EXPECT_EQ(events[0].data, "line1\n\nline3");
}

TEST(SSEParserTest, EventBoundaryDetection) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // Single \n should not trigger event end
    auto events = parser.parseChunk("data: test\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    EXPECT_EQ(events.size(), 0);
    
    // Double \n should trigger event end
    auto events = parser.parseChunk("\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "test");
}

TEST(SSEParserTest, IdPersistence) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // First event with ID
    auto events = parser.parseChunk("id: persistent-id\ndata: event1\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].id, "persistent-id");
    
    // Get last event ID from parser
    EXPECT_EQ(parser.getLastEventId(), "persistent-id");
    
    // Second event without ID should use previous ID
    auto events = parser.parseChunk("data: event2\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    ASSERT_EQ(events.size(), 2);
    // Parser tracks last ID internally
    EXPECT_EQ(parser.getLastEventId(), "persistent-id");
}

TEST(SSEParserTest, EmptyId) {
    SSEParser parser;
    std::vector<SSEEvent> events;
    
    // Set initial ID
    auto events = parser.parseChunk("id: initial\ndata: event1\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    EXPECT_EQ(parser.getLastEventId(), "initial");
    
    // Empty ID should clear the last ID
    auto events = parser.parseChunk("id:\ndata: event2\n\n", [&](const SSEEvent& event) {
        events.push_back(event);
    });
    
    EXPECT_EQ(parser.getLastEventId(), "");
}
