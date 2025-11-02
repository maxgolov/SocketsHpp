# MCP Streaming Implementation Gap Analysis

## Current State

### ✅ What We Have (Server-Side)

1. **HTTP Server with SSE Support** (`http_server.h`)
   - ✅ SSEEvent class for formatting events
   - ✅ Streaming responses via `streamCallback`
   - ✅ Content-Type: text/event-stream support
   - ✅ Chunked transfer encoding
   - ✅ SessionManager with Last-Event-ID support
   - ✅ Event history and replay (`addEvent`, `getEventsSince`)

2. **MCP Server** (`mcp_server.h`)
   - ✅ HTTP POST for JSON-RPC requests
   - ✅ HTTP GET for SSE streaming (session-based)
   - ✅ Last-Event-ID resumability
   - ✅ Session management integration
   - ✅ Method registry and request routing
   - ✅ Authentication (Bearer JWT, API Key)

3. **JSON-RPC Layer** (`json_rpc.h`)
   - ✅ JsonRpcRequest, JsonRpcResponse, JsonRpcNotification
   - ✅ Standard error codes
   - ✅ Variant-based ID handling

### ✅ What We Have (Client-Side)

1. **HTTP Client** (`http_client.h`)
   - ✅ Chunked response parsing (`receiveChunkedBody`)
   - ✅ Chunk callback support (`chunkCallback`)
   - ✅ Streaming callback support
   - ✅ GET/POST methods
   - ✅ Custom header support

### ❌ Critical Gaps

#### 1. **SSE Client Support** (HIGH PRIORITY)
The HTTP client can handle chunked responses but lacks SSE-specific parsing:

**Missing:**
- SSE event parser (parse `id:`, `event:`, `data:`, `retry:` fields)
- Event callback mechanism (per-event processing)
- Automatic Last-Event-ID tracking
- Reconnection logic with Last-Event-ID header
- Event buffering for multi-line data fields

**Impact:** Cannot consume SSE streams from MCP servers

**Implementation Required:**
```cpp
class SSEClient {
    // Parse SSE events from stream
    // Track Last-Event-ID automatically
    // Provide event callbacks
    // Handle reconnection
};
```

#### 2. **MCP Client** (HIGH PRIORITY)
No client implementation exists for MCP protocol.

**Missing:**
- MCPClient class
- Initialize/ping/shutdown methods
- Tools discovery (tools/list)
- Tool invocation (tools/call)
- Prompt discovery (prompts/list)
- Resource access (resources/*)
- Server-to-client message handling (notifications)
- Bidirectional communication

**Impact:** Cannot connect to MCP servers

#### 3. **Bidirectional SSE** (MEDIUM PRIORITY)
MCP requires server→client notifications via SSE while client→server uses POST.

**Current Implementation:**
- ✅ Server can send SSE events
- ✅ Client can send POST requests
- ❌ No coordination between SSE stream and POST requests
- ❌ No request/response correlation for async responses

**Missing:**
```cpp
class MCPClient {
    SSEClient sseStream;        // Server→client notifications
    HttpClient httpClient;       // Client→server requests
    
    // Coordinate both channels
    void handleNotification(JsonRpcNotification notif);
    JsonRpcResponse sendRequest(JsonRpcRequest req);
};
```

#### 4. **Stream Keep-Alive** (LOW PRIORITY)
Long-lived SSE connections need keep-alive.

**Missing:**
- Ping/pong mechanism
- Connection health monitoring
- Automatic reconnection on disconnect
- Exponential backoff

#### 5. **Stdio Transport for Client** (MEDIUM PRIORITY)
MCP supports stdio transport (spawn process, read/write JSON-RPC).

**Missing:**
- Process spawning
- Stdin/stdout JSON-RPC communication
- Process lifecycle management

## Implementation Priority

### Phase 1: SSE Client (CRITICAL)
**Files to Create:**
1. `include/SocketsHpp/http/client/sse_client.h`
   - SSEEvent parsing
   - Event callbacks
   - Last-Event-ID tracking
   - Reconnection logic

**Estimated Effort:** 3-4 hours

### Phase 2: MCP Client Core (CRITICAL)
**Files to Create:**
1. `include/SocketsHpp/mcp/client/mcp_client.h`
   - MCPClient class
   - HTTP transport (SSE + POST)
   - Initialize/tools/prompts/resources methods
   - Notification handling

**Estimated Effort:** 4-5 hours

### Phase 3: Stdio Transport (OPTIONAL)
**Files to Create:**
1. `include/SocketsHpp/mcp/client/stdio_transport.h`
   - Process spawning
   - Stdin/stdout communication
   - Process management

**Estimated Effort:** 3-4 hours

### Phase 4: Testing (CRITICAL)
**Files to Create:**
1. `test/unit/sse_client_test.cc`
2. `test/functional/mcp_client_test.cc`
3. `test/functional/mcp_integration_test.cc`

**Estimated Effort:** 4-5 hours

## Detailed Gap Analysis

### SSE Client Requirements

```cpp
// What we need to parse:
/*
id: 123
event: message
data: {"jsonrpc":"2.0","method":"notifications/message","params":{}}
data: {"level":"info","logger":"mcp-server"}

id: 124
event: tools/list_changed
data: {}

*/

class SSEEventParser {
    struct ParsedEvent {
        std::string id;
        std::string event;
        std::string data;  // Accumulated multi-line data
        int retry;
    };
    
    ParsedEvent parseChunk(const std::string& chunk);
    std::vector<ParsedEvent> parseStream(const std::string& stream);
};

class SSEClient : public HttpClient {
    using EventCallback = std::function<void(const ParsedEvent&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    void connect(const std::string& url, EventCallback onEvent);
    void setLastEventId(const std::string& id);
    void reconnect();  // With Last-Event-ID
    void close();
    
private:
    std::string m_lastEventId;
    EventCallback m_eventCallback;
    bool m_autoReconnect = true;
    int m_reconnectDelay = 3000;  // ms
};
```

### MCP Client Requirements

```cpp
class MCPClient {
public:
    // Lifecycle
    void connect(const ClientConfig& config);
    void disconnect();
    
    // MCP Protocol Methods
    json initialize(const json& clientInfo);
    json ping();
    void shutdown();
    
    // Tools
    json listTools();
    json callTool(const std::string& name, const json& arguments);
    
    // Prompts
    json listPrompts();
    json getPrompt(const std::string& name, const json& arguments);
    
    // Resources
    json listResources();
    json readResource(const std::string& uri);
    json subscribeResource(const std::string& uri);
    json unsubscribeResource(const std::string& uri);
    
    // Notifications (server→client)
    void onNotification(const std::string& method, std::function<void(json)> handler);
    
private:
    SSEClient m_sseClient;         // For server→client messages
    HttpClient m_httpClient;        // For client→server requests
    std::string m_sessionId;
    std::map<std::string, std::function<void(json)>> m_notificationHandlers;
    
    JsonRpcResponse sendRequest(const JsonRpcRequest& request);
    void handleServerEvent(const SSEEventParser::ParsedEvent& event);
};
```

## API Design Proposal

### SSE Client Example
```cpp
#include <SocketsHpp/http/client/sse_client.h>

SSEClient client;
client.connect("http://localhost:8080/events", [](const SSEEvent& event) {
    std::cout << "Event ID: " << event.id << "\n";
    std::cout << "Event Type: " << event.event << "\n";
    std::cout << "Data: " << event.data << "\n";
});

// Automatic reconnection with Last-Event-ID
// Client handles parsing, buffering, reconnection
```

### MCP Client Example
```cpp
#include <SocketsHpp/mcp/client/mcp_client.h>

mcp::ClientConfig config;
config.transport = mcp::TransportType::HTTP;
config.http.url = "http://localhost:3000/mcp";

MCPClient client;
client.connect(config);

// Initialize
auto initResult = client.initialize({
    {"protocolVersion", "2024-11-05"},
    {"clientInfo", {
        {"name", "SocketsHpp-MCP-Client"},
        {"version", "1.0.0"}
    }}
});

// List available tools
auto tools = client.listTools();
for (auto& tool : tools["tools"]) {
    std::cout << tool["name"] << ": " << tool["description"] << "\n";
}

// Call a tool
auto result = client.callTool("get_weather", {
    {"location", "San Francisco"}
});

// Handle server notifications
client.onNotification("notifications/message", [](json params) {
    std::cout << "Server message: " << params["message"] << "\n";
});

client.disconnect();
```

## Recommended Approach

1. **Implement SSE Client first** - Foundation for MCP client
2. **Implement MCP Client (HTTP only)** - Core functionality
3. **Add comprehensive tests** - Validate both components
4. **Add Stdio transport (optional)** - For process-based servers
5. **Add examples** - Demonstrate usage

## Files to Create (Priority Order)

1. ✅ `include/SocketsHpp/http/client/sse_client.h` (NEW)
2. ✅ `include/SocketsHpp/mcp/client/mcp_client.h` (NEW)
3. ⏳ `test/unit/sse_parser_test.cc` (NEW)
4. ⏳ `test/functional/mcp_client_test.cc` (NEW)
5. ⏳ `examples/mcp_client_example.cpp` (NEW)
6. ⏳ `examples/mcp_server_example.cpp` (NEW)

## Conclusion

**Critical Path:** SSE Client → MCP Client → Tests

The main blockers for full MCP support are:
1. SSE event parsing and streaming (client-side)
2. MCP client implementation with bidirectional communication
3. Proper coordination between SSE (server→client) and POST (client→server)

Once these are implemented, SocketsHpp will have full MCP client/server capabilities.
