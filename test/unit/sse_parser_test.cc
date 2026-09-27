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

    ASSERT_EQ(events2.size(), 1u);
    EXPECT_TRUE(events2[0].id.empty());
    EXPECT_TRUE(events2[0].hasId);  // present-but-empty id resets Last-Event-ID
}

// ---------------------------------------------------------------------------
// WHATWG spec conformance: line endings
// ---------------------------------------------------------------------------

namespace {
// Feed a stream to a fresh parser in pieces of `step` bytes and collect events.
std::vector<SSEEvent> feed(const std::string& stream, size_t step)
{
    SSEParser parser;
    std::vector<SSEEvent> all;
    for (size_t i = 0; i < stream.size(); i += step)
    {
        auto evs = parser.parseChunk(stream.substr(i, step));
        all.insert(all.end(), evs.begin(), evs.end());
    }
    return all;
}
}  // namespace

TEST(SSEParserTest, CRLFLineEndings) {
    SSEParser parser;
    auto events = parser.parseChunk("event: a\r\ndata: one\r\ndata: two\r\n\r\ndata: three\r\n\r\n");
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].event, "a");
    EXPECT_EQ(events[0].data, "one\ntwo");
    EXPECT_EQ(events[1].data, "three");
}

TEST(SSEParserTest, LoneCRLineEndings) {
    SSEParser parser;
    auto events = parser.parseChunk("data: one\rdata: two\r\rdata: three\r\r");
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].data, "one\ntwo");
    EXPECT_EQ(events[1].data, "three");
}

TEST(SSEParserTest, MixedLineEndings) {
    SSEParser parser;
    auto events = parser.parseChunk("data: a\rdata: b\ndata: c\r\n\n" "data: d\n\r\n" "data: e\r\r\n");
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].data, "a\nb\nc");
    EXPECT_EQ(events[1].data, "d");
    EXPECT_EQ(events[2].data, "e");
}

TEST(SSEParserTest, CRAtChunkEndFollowedByLF) {
    SSEParser parser;
    // A CRLF split across chunks must count as one line ending, not two.
    auto e1 = parser.parseChunk("data: a\r");
    EXPECT_TRUE(e1.empty());
    auto e2 = parser.parseChunk("\ndata: b\r");
    EXPECT_TRUE(e2.empty());  // no blank line seen yet
    auto e3 = parser.parseChunk("\n\r");
    ASSERT_EQ(e3.size(), 1u);  // blank line terminated by the lone CR
    EXPECT_EQ(e3[0].data, "a\nb");
    auto e4 = parser.parseChunk("\ndata: c\n\n");  // LF completing that CRLF is swallowed
    ASSERT_EQ(e4.size(), 1u);
    EXPECT_EQ(e4[0].data, "c");
}

TEST(SSEParserTest, CRAtChunkEndFollowedByCR) {
    SSEParser parser;
    EXPECT_TRUE(parser.parseChunk("data: x\r").empty());
    auto events = parser.parseChunk("\r");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].data, "x");
}

TEST(SSEParserTest, ByteByByteMatchesWholeStream) {
    const std::string stream =
        "\xEF\xBB\xBF"
        ": comment\r\n"
        "id: 1\r\n"
        "event: e\r"
        "data: first\r\n"
        "data:second\n"
        "\r\n"
        "retry: 250\n"
        "data\r"
        "\r"
        "data: last\r\n"
        "\r\n";
    for (size_t step : {size_t(1), size_t(2), size_t(3), size_t(7), stream.size()})
    {
        auto events = feed(stream, step);
        ASSERT_EQ(events.size(), 3u) << "step=" << step;
        EXPECT_EQ(events[0].id, "1");
        EXPECT_EQ(events[0].event, "e");
        EXPECT_EQ(events[0].data, "first\nsecond");
        EXPECT_EQ(events[1].retry, 250);
        EXPECT_TRUE(events[1].hasData);
        EXPECT_EQ(events[1].data, "");
        EXPECT_EQ(events[2].data, "last");
    }
}

TEST(SSEParserTest, LeadingBOMStripped) {
    auto events = feed("\xEF\xBB\xBF" "data: x\n\n", 1);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].data, "x");

    // A partial BOM is not stripped: it becomes part of the first field name,
    // which is then unknown and ignored.
    SSEParser parser;
    auto evs = parser.parseChunk("\xEF" "data: y\n\ndata: z\n\n");
    ASSERT_EQ(evs.size(), 1u);
    EXPECT_EQ(evs[0].data, "z");
}

TEST(SSEParserTest, ResetClearsPendingState) {
    SSEParser parser;
    parser.parseChunk("data: partial\ndata: more\r");
    parser.reset();
    // After reset nothing is pending: the leading LF is a blank line, then "fresh".
    auto events = parser.parseChunk("\ndata: fresh\n\n");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].data, "fresh");
}

// ---------------------------------------------------------------------------
// WHATWG spec conformance: fields
// ---------------------------------------------------------------------------

TEST(SSEParserTest, RetryDigitsOnly) {
    SSEParser parser;
    const char* invalid[] = {"retry: 12a\n\n", "retry: -1\n\n", "retry: 1.5\n\n", "retry:  100\n\n",
                             "retry: +5\n\n",  "retry:\n\n",    "retry\n\n",      "retry: 99999999999999999999\n\n",
                             "retry: 100 \n\n"};
    for (const char* s : invalid)
    {
        auto events = parser.parseChunk(s);
        EXPECT_TRUE(events.empty()) << "input: " << s;  // retry ignored => nothing to report
    }
    auto ok = parser.parseChunk("retry:0\n\nretry: 007\n\n");
    ASSERT_EQ(ok.size(), 2u);
    EXPECT_EQ(ok[0].retry, 0);
    EXPECT_EQ(ok[1].retry, 7);
    EXPECT_FALSE(ok[0].isValid());  // no data => not to be dispatched
}

TEST(SSEParserTest, EmptyIdResetsLastEventId) {
    SSEParser parser;
    auto events = parser.parseChunk("id\ndata: x\n\nid:\n\n");
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(events[0].hasId);
    EXPECT_EQ(events[0].id, "");
    EXPECT_TRUE(events[1].hasId);  // id-only block is still reported so a client can reset
    EXPECT_FALSE(events[1].isValid());
}

TEST(SSEParserTest, IdWithNullIgnored) {
    SSEParser parser;
    std::string chunk = std::string("id: a") + '\0' + "b\ndata: x\n\n";
    auto events = parser.parseChunk(chunk);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].hasId);
    EXPECT_EQ(events[0].id, "");
}

TEST(SSEParserTest, SingleLeadingSpaceStripped) {
    SSEParser parser;
    auto events = parser.parseChunk("data:  two spaces\ndata:none\ndata: \n\nevent:  e\ndata: y\n\n");
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].data, " two spaces\nnone\n");
    EXPECT_EQ(events[1].event, " e");
}

TEST(SSEParserTest, FieldWithoutColon) {
    SSEParser parser;
    auto events = parser.parseChunk("data\ndata\n\n");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].hasData);
    EXPECT_EQ(events[0].data, "\n");
}

TEST(SSEParserTest, OnlyOneTrailingNewlineRemoved) {
    SSEParser parser;
    auto events = parser.parseChunk("data: a\ndata:\ndata:\n\n");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].data, "a\n\n");
}

TEST(SSEParserTest, NoDataNoDispatch) {
    SSEParser parser;
    auto events = parser.parseChunk("event: only-type\n\n: just a comment\n\nfoo: bar\n\n\n\n");
    EXPECT_TRUE(events.empty());
}

TEST(SSEParserTest, CommentLineWithColonInside) {
    SSEParser parser;
    auto events = parser.parseChunk(":data: not data\ndata: real\n\n");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].data, "real");
}

TEST(SSEParserTest, FieldNamesAreCaseSensitive) {
    SSEParser parser;
    auto events = parser.parseChunk("Data: nope\nunknown: x\ndata: yes\n\n");
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].data, "yes");
}

TEST(SSEParserTest, EventTypeDoesNotLeakIntoNextEvent) {
    SSEParser parser;
    auto events = parser.parseChunk("event: custom\ndata: 1\n\ndata: 2\n\n");
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].event, "custom");
    EXPECT_TRUE(events[1].event.empty());
}

TEST(SSEParserTest, IncompleteEventNotDispatched) {
    SSEParser parser;
    // No trailing blank line: the event must stay pending.
    EXPECT_TRUE(parser.parseChunk("data: pending\n").empty());
    EXPECT_TRUE(parser.parseChunk("data: still").empty());
}
