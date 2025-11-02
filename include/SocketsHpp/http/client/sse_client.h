// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/http/client/http_client.h>

#include <functional>
#include <sstream>
#include <string>
#include <vector>

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace client
    {
        /// @brief Parsed Server-Sent Event
        struct SSEEvent
        {
            std::string id;
            std::string event;      // Event type (default: "message")
            std::string data;       // Event data (multi-line concatenated)
            int retry = -1;         // Reconnection time in ms (-1 = not set)
            bool hasData = false;   // True if a data field was present (even if empty)

            /// @brief Check if event is valid (has data)
            bool isValid() const { return hasData; }

            /// @brief Check if this is a comment event
            bool isComment() const { return !hasData && event.empty() && id.empty() && retry < 0; }
        };

        /// @brief SSE Event Parser
        /// Parses Server-Sent Events according to WHATWG spec
        /// https://html.spec.whatwg.org/multipage/server-sent-events.html
        class SSEParser
        {
        public:
            /// @brief Parse SSE chunk and extract complete events
            /// @param chunk Data chunk from stream
            /// @return Vector of complete events found in chunk
            std::vector<SSEEvent> parseChunk(const std::string& chunk)
            {
                std::vector<SSEEvent> events;
                
                // Append to buffer
                m_buffer += chunk;
                
                // Process complete events (terminated by \n\n or \r\n\r\n)
                size_t pos = 0;
                while (true)
                {
                    // Look for event terminator
                    size_t eventEnd = findEventEnd(m_buffer, pos);
                    if (eventEnd == std::string::npos)
                    {
                        break; // No complete event
                    }
                    
                    // Extract event block
                    std::string eventBlock = m_buffer.substr(pos, eventEnd - pos);
                    
                    // Parse event
                    SSEEvent event = parseEvent(eventBlock);
                    // Dispatch event if it has data field, id, or retry
                    // Per SSE spec, events should be dispatched unless they're pure comments
                    if (event.hasData || !event.id.empty() || event.retry >= 0)
                    {
                        events.push_back(event);
                    }
                    
                    // Move past event terminator
                    pos = eventEnd;
                    while (pos < m_buffer.size() && (m_buffer[pos] == '\n' || m_buffer[pos] == '\r'))
                    {
                        pos++;
                    }
                }
                
                // Remove processed events from buffer
                m_buffer.erase(0, pos);
                
                return events;
            }

            /// @brief Reset parser state
            void reset()
            {
                m_buffer.clear();
            }

            /// @brief Get current buffer size (for debugging)
            size_t getBufferSize() const { return m_buffer.size(); }

        private:
            std::string m_buffer;

            /// @brief Find end of current event (\n\n or \r\n\r\n)
            size_t findEventEnd(const std::string& str, size_t start) const
            {
                size_t pos = start;
                bool prevNewline = false;
                
                while (pos < str.size())
                {
                    char c = str[pos];
                    
                    if (c == '\n')
                    {
                        if (prevNewline)
                        {
                            return pos - 1; // Found \n\n
                        }
                        prevNewline = true;
                    }
                    else if (c != '\r')
                    {
                        prevNewline = false;
                    }
                    
                    pos++;
                }
                
                return std::string::npos;
            }

            /// @brief Parse single event from event block
            SSEEvent parseEvent(const std::string& eventBlock)
            {
                SSEEvent event;
                std::string dataLines;
                bool hasDataField = false;
                
                std::istringstream iss(eventBlock);
                std::string line;
                
                while (std::getline(iss, line))
                {
                    // Remove \r if present
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    
                    // Skip empty lines
                    if (line.empty())
                    {
                        continue;
                    }
                    
                    // Comment line (starts with :)
                    if (line[0] == ':')
                    {
                        continue;
                    }
                    
                    // Parse field: value
                    size_t colonPos = line.find(':');
                    if (colonPos == std::string::npos)
                    {
                        // Field with no value
                        std::string field = line;
                        if (field == "data") hasDataField = true;
                        processField(field, "", event, dataLines);
                    }
                    else
                    {
                        std::string field = line.substr(0, colonPos);
                        std::string value = line.substr(colonPos + 1);
                        
                        // Remove leading space from value (spec requirement)
                        if (!value.empty() && value[0] == ' ')
                        {
                            value = value.substr(1);
                        }
                        
                        if (field == "data") hasDataField = true;
                        processField(field, value, event, dataLines);
                    }
                }
                
                // Finalize data (remove trailing newline if present)
                if (!dataLines.empty() && dataLines.back() == '\n')
                {
                    dataLines.pop_back();
                }
                event.data = dataLines;
                
                // Store whether a data field was present
                // (needed to differentiate between no data field vs empty data field)
                event.hasData = hasDataField;
                
                return event;
            }

            /// @brief Process individual field
            void processField(const std::string& field, const std::string& value, 
                            SSEEvent& event, std::string& dataLines)
            {
                if (field == "id")
                {
                    event.id = value;
                }
                else if (field == "event")
                {
                    event.event = value;
                }
                else if (field == "data")
                {
                    dataLines += value;
                    dataLines += '\n';
                }
                else if (field == "retry")
                {
                    // Parse retry as integer
                    try
                    {
                        event.retry = std::stoi(value);
                    }
                    catch (...)
                    {
                        // Invalid retry value, ignore
                    }
                }
                // Unknown fields are ignored per spec
            }
        };

        /// @brief SSE Client for consuming Server-Sent Events streams
        class SSEClient : public HttpClient
        {
        public:
            /// @brief Event callback type
            using EventCallback = std::function<void(const SSEEvent&)>;
            
            /// @brief Error callback type
            using ErrorCallback = std::function<void(const std::string&)>;

            /// @brief Connect to SSE endpoint and start receiving events
            /// @param url SSE endpoint URL
            /// @param onEvent Callback for each event
            /// @param onError Optional error callback
            /// @return true if connection successful
            bool connect(const std::string& url, EventCallback onEvent, ErrorCallback onError = nullptr)
            {
                m_eventCallback = onEvent;
                m_errorCallback = onError;
                m_url = url;
                
                return reconnect();
            }

            /// @brief Reconnect with Last-Event-ID
            /// @return true if reconnection successful
            bool reconnect()
            {
                HttpClientRequest request;
                request.method = METHOD_GET;
                request.uri = m_url;
                request.setAccept("text/event-stream");
                
                // Add Last-Event-ID header if available
                if (!m_lastEventId.empty())
                {
                    request.setHeader("Last-Event-Id", m_lastEventId);
                }
                
                HttpClientResponse response;
                
                // Setup chunk callback to parse SSE events
                response.chunkCallback = [this](const std::string& chunk) {
                    handleChunk(chunk);
                };
                
                response.onComplete = [this]() {
                    // Stream ended
                    if (m_autoReconnect)
                    {
                        // Auto-reconnect with exponential backoff
                        std::this_thread::sleep_for(std::chrono::milliseconds(m_reconnectDelay));
                        reconnect();
                    }
                };
                
                // Send request (blocking until stream completes)
                bool success = send(request, response);
                
                if (!success && m_errorCallback)
                {
                    m_errorCallback("Failed to connect to SSE endpoint");
                }
                
                return success;
            }

            /// @brief Set Last-Event-ID for resumable streams
            /// @param id Last event ID received
            void setLastEventId(const std::string& id)
            {
                m_lastEventId = id;
            }

            /// @brief Get current Last-Event-ID
            const std::string& getLastEventId() const
            {
                return m_lastEventId;
            }

            /// @brief Enable/disable automatic reconnection
            /// @param enable Enable auto-reconnect
            /// @param delay Reconnection delay in milliseconds
            void setAutoReconnect(bool enable, int delay = 3000)
            {
                m_autoReconnect = enable;
                m_reconnectDelay = delay;
            }

            /// @brief Close SSE connection
            void close()
            {
                m_autoReconnect = false;
                // Connection will close when current stream ends
            }

        private:
            std::string m_url;
            std::string m_lastEventId;
            EventCallback m_eventCallback;
            ErrorCallback m_errorCallback;
            bool m_autoReconnect = false;
            int m_reconnectDelay = 3000;  // ms
            SSEParser m_parser;

            /// @brief Handle incoming chunk
            void handleChunk(const std::string& chunk)
            {
                // Parse chunk for events
                auto events = m_parser.parseChunk(chunk);
                
                // Process each event
                for (const auto& event : events)
                {
                    // Update Last-Event-ID
                    if (!event.id.empty())
                    {
                        m_lastEventId = event.id;
                    }
                    
                    // Update retry delay if specified
                    if (event.retry >= 0)
                    {
                        m_reconnectDelay = event.retry;
                    }
                    
                    // Invoke callback
                    if (m_eventCallback && event.isValid())
                    {
                        m_eventCallback(event);
                    }
                }
            }
        };

    }  // namespace client
}  // namespace http
SOCKETSHPP_NS_END
