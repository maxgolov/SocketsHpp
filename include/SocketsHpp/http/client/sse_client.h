// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/http/client/http_client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
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
            /// @brief Value of the "id" field in this event's block (meaningful only if hasId).
            std::string id;
            /// @brief Event type from the "event" field (empty means the default, "message").
            std::string event;
            /// @brief Event data; multiple "data" lines are joined with '\n'.
            std::string data;
            /// @brief Reconnection time in ms from a "retry" field, or -1 if not set.
            int retry = -1;
            /// @brief True if a data field was present (even if empty).
            bool hasData = false;
            /// @brief True if an id field was present (an empty id resets Last-Event-ID).
            bool hasId = false;

            /// @brief Check if the event should be dispatched (a data field was present).
            /// @return hasData
            bool isValid() const { return hasData; }

            /// @brief Check if the event carries no fields at all.
            /// @return true if no data, event, id or retry field is set.
            /// @note SSEParser never returns such events (comment lines are dropped).
            bool isComment() const { return !hasData && event.empty() && !hasId && retry < 0; }
        };

        /// @brief SSE Event Parser
        /// Parses Server-Sent Events according to the WHATWG spec
        /// https://html.spec.whatwg.org/multipage/server-sent-events.html
        ///
        /// Lines may end in CRLF, LF or a lone CR (also split across chunks). A leading
        /// UTF-8 BOM at the start of the stream is ignored.
        ///
        /// parseChunk() returns one SSEEvent per blank-line-terminated block that set a
        /// data, id or retry field. Only events with hasData (isValid()) are meant to be
        /// dispatched to applications; blocks with only id/retry are still returned so
        /// that callers can track Last-Event-ID and the reconnection time. Each event
        /// carries only the id set in its own block (Last-Event-ID tracking across
        /// events is left to the caller, e.g. SSEClient).
        ///
        /// @note Not thread-safe; state is kept between calls, so feed one stream per parser.
        class SSEParser
        {
        public:
            /// @brief Parse SSE chunk and extract complete events
            /// @param chunk Data chunk from stream (may split lines or events anywhere)
            /// @return Events completed by this chunk (possibly none); incomplete data is
            ///         buffered for the next call
            std::vector<SSEEvent> parseChunk(const std::string& chunk)
            {
                std::vector<SSEEvent> events;
                size_t pos = 0;
                const size_t n = chunk.size();

                if (!m_bomChecked)
                {
                    // Strip a UTF-8 BOM (EF BB BF) at the very start of the stream.
                    static const char kBom[] = "\xEF\xBB\xBF";
                    while (pos < n && m_bomMatched < 3)
                    {
                        if (chunk[pos] != kBom[m_bomMatched])
                        {
                            // Not a BOM: replay any partially matched bytes as content.
                            m_line.append(kBom, m_bomMatched);
                            m_bomMatched = 3;
                            break;
                        }
                        ++m_bomMatched;
                        ++pos;
                    }
                    if (m_bomMatched < 3)
                    {
                        return events;  // need more bytes to decide
                    }
                    m_bomChecked = true;
                }

                while (pos < n)
                {
                    if (m_skipLF)
                    {
                        // Previous line ended in CR (possibly at the end of the previous
                        // chunk); an immediately following LF belongs to that CRLF.
                        m_skipLF = false;
                        if (chunk[pos] == '\n')
                        {
                            ++pos;
                            continue;
                        }
                    }

                    const size_t eol = chunk.find_first_of("\r\n", pos);
                    if (eol == std::string::npos)
                    {
                        m_line.append(chunk, pos, n - pos);
                        break;
                    }
                    m_line.append(chunk, pos, eol - pos);
                    m_skipLF = (chunk[eol] == '\r');
                    pos = eol + 1;
                    processLine(events);
                    m_line.clear();
                }

                return events;
            }

            /// @brief Reset parser state (e.g. before reconnecting): drops buffered line and
            /// event data and re-enables BOM detection
            void reset()
            {
                m_line.clear();
                m_skipLF = false;
                m_bomChecked = false;
                m_bomMatched = 0;
                resetEvent();
            }

            /// @brief Get size of buffered, not yet terminated line data (for debugging)
            /// @return Number of bytes in the pending partial line
            size_t getBufferSize() const { return m_line.size(); }

        private:
            std::string m_line;          // current (incomplete) line
            bool m_skipLF = false;       // last line ended with CR; swallow a following LF
            bool m_bomChecked = false;
            size_t m_bomMatched = 0;

            // Event being accumulated
            std::string m_data;
            std::string m_eventType;
            std::string m_id;
            bool m_hasData = false;
            bool m_hasId = false;
            int m_retry = -1;

            void resetEvent()
            {
                m_data.clear();
                m_eventType.clear();
                m_id.clear();
                m_hasData = false;
                m_hasId = false;
                m_retry = -1;
            }

            void processLine(std::vector<SSEEvent>& events)
            {
                if (m_line.empty())
                {
                    dispatch(events);
                    return;
                }
                if (m_line[0] == ':')
                {
                    return;  // comment
                }

                const size_t colon = m_line.find(':');
                if (colon == std::string::npos)
                {
                    processField(m_line, std::string());
                    return;
                }
                size_t valueStart = colon + 1;
                if (valueStart < m_line.size() && m_line[valueStart] == ' ')
                {
                    ++valueStart;  // strip exactly one leading space
                }
                processField(m_line.substr(0, colon), m_line.substr(valueStart));
            }

            void processField(const std::string& field, const std::string& value)
            {
                if (field == "data")
                {
                    m_data += value;
                    m_data += '\n';
                    m_hasData = true;
                }
                else if (field == "event")
                {
                    m_eventType = value;
                }
                else if (field == "id")
                {
                    // Ignore ids containing NULL (spec)
                    if (value.find('\0') == std::string::npos)
                    {
                        m_id = value;
                        m_hasId = true;
                    }
                }
                else if (field == "retry")
                {
                    // Only ASCII digits are accepted; anything else is ignored.
                    if (value.empty())
                    {
                        return;
                    }
                    int64_t ms = 0;
                    for (char c : value)
                    {
                        if (c < '0' || c > '9')
                        {
                            return;
                        }
                        ms = ms * 10 + (c - '0');
                        if (ms > std::numeric_limits<int>::max())
                        {
                            return;  // out of range: ignore
                        }
                    }
                    m_retry = static_cast<int>(ms);
                }
                // Unknown fields are ignored per spec
            }

            void dispatch(std::vector<SSEEvent>& events)
            {
                if (m_hasData || m_hasId || m_retry >= 0)
                {
                    SSEEvent event;
                    event.id = m_id;
                    event.hasId = m_hasId;
                    event.event = m_eventType;
                    event.retry = m_retry;
                    event.hasData = m_hasData;
                    event.data = m_data;
                    if (!event.data.empty() && event.data.back() == '\n')
                    {
                        event.data.pop_back();
                    }
                    events.push_back(std::move(event));
                }
                resetEvent();
            }
        };

        /// @brief SSE Client for consuming Server-Sent Events streams
        ///
        /// connect() blocks the calling thread for the lifetime of the stream (including
        /// automatic reconnects). close() may be called from any thread (or from inside a
        /// callback) and makes connect() return promptly.
        ///
        /// Unlike HttpClient, the read timeout defaults to 0 (none) so that idle streams
        /// are not dropped; call setReadTimeout() to enable one. Other HttpClient settings
        /// (connect timeout, redirects, User-Agent) apply; only http:// URLs work.
        ///
        /// Each (re)connection sends a GET with "Accept: text/event-stream",
        /// "Cache-Control: no-cache", the headers from setRequestHeader() and, when known,
        /// "Last-Event-ID". Callbacks run on the thread that called connect().
        class SSEClient : public HttpClient
        {
        public:
            /// @brief Event callback type; called for each event with data (SSEEvent::isValid()).
            using EventCallback = std::function<void(const SSEEvent&)>;

            /// @brief Error callback type; receives a short description of the failure.
            using ErrorCallback = std::function<void(const std::string&)>;

            /// @brief Create a client with no read timeout and auto-reconnect disabled.
            SSEClient() { m_readTimeoutMs = 0; }

            /// @brief Connect to SSE endpoint and receive events until the stream ends
            /// (or, with auto-reconnect enabled, until close() is called or the server
            /// answers with a non-200 status). Blocks the calling thread.
            /// @param url SSE endpoint URL (http:// only)
            /// @param onEvent Callback for each event with data
            /// @param onError Optional error callback: called on a non-200 status, a failed
            ///        connection or an interrupted stream (not after close())
            /// @return true if the last connection attempt got status 200 and its stream
            ///         ended without error, or if the stream was stopped by close() after
            ///         connecting at least once; false otherwise (always false on a non-200
            ///         status, which is never retried).
            /// @note Resets a previous close() and any server "retry:" value from an
            ///       earlier stream; the auto-reconnect setting is kept.
            bool connect(const std::string& url, EventCallback onEvent, ErrorCallback onError = nullptr)
            {
                m_eventCallback = onEvent;
                m_errorCallback = onError;
                m_url = url;
                m_serverRetry = -1;
                m_closed = false;
                clearCancel();
                return run();
            }

            /// @brief Reconnect to the URL and callbacks of the last connect(), sending
            /// Last-Event-ID when known (same semantics as connect()).
            /// @return As connect(); false immediately if close() has been called.
            bool reconnect()
            {
                if (m_closed)
                {
                    return false;
                }
                return run();
            }

            /// @brief Set Last-Event-ID for resumable streams. Thread-safe; sent on the next
            /// (re)connection if non-empty. Updated automatically from received "id:" fields.
            /// @param id Last event id (empty: send no Last-Event-ID header)
            void setLastEventId(const std::string& id)
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_lastEventId = id;
            }

            /// @brief Add a header sent with every stream request (including reconnects),
            /// e.g. Authorization or Mcp-Session-Id. Replaces an existing header of the
            /// same name (compared case-insensitively). Accept and Cache-Control are always set by the client,
            /// and Last-Event-ID is replaced whenever a last event id is known.
            /// @param name Header field name
            /// @param value Header field value
            /// @note Thread-safe; takes effect on the next (re)connection.
            void setRequestHeader(const std::string& name, const std::string& value)
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                detail::eraseHeader(m_requestHeaders, name);
                m_requestHeaders[name] = value;
            }

            /// @brief Remove all headers added with setRequestHeader(). Thread-safe.
            void clearRequestHeaders()
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_requestHeaders.clear();
            }

            /// @brief Get current Last-Event-ID. Thread-safe.
            /// @return A copy of the last id received (or set with setLastEventId())
            std::string getLastEventId() const
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                return m_lastEventId;
            }

            /// @brief Enable/disable automatic reconnection (disabled by default). Thread-safe.
            ///
            /// With auto-reconnect, a stream that ends or fails is reopened after the delay;
            /// consecutive connection failures double the delay up to
            /// setMaxReconnectDelay(). A non-200 status or close() stops reconnecting.
            /// @param enable Enable auto-reconnect
            /// @param delay Base reconnection delay in milliseconds. A server "retry:" field
            ///        overrides it for the rest of the current connect() call.
            void setAutoReconnect(bool enable, int delay = 3000)
            {
                m_autoReconnect = enable;
                m_reconnectDelay = delay;
            }

            /// @brief Set maximum reconnection delay used by the exponential backoff after
            /// consecutive connection failures (milliseconds, default 60000). Thread-safe.
            /// @param delay Upper bound in ms (a larger base delay is still honoured)
            void setMaxReconnectDelay(int delay) { m_maxReconnectDelay = delay; }

            /// @brief Close SSE connection. Thread-safe (also callable from a callback);
            /// unblocks a connect() in progress, including one waiting to reconnect.
            ///
            /// Stops reconnecting, shuts down the active socket and makes further requests
            /// on this client fail until the next connect() (the auto-reconnect setting is
            /// kept for that call). No more events or errors are delivered after it takes
            /// effect.
            void close()
            {
                m_closed = true;
                shutdownActiveSocket(true);
                {
                    std::lock_guard<std::mutex> lock(m_waitMutex);
                }
                m_waitCv.notify_all();
            }

        private:
            std::string m_url;
            std::string m_lastEventId;
            std::map<std::string, std::string> m_requestHeaders;  // guarded by m_stateMutex
            EventCallback m_eventCallback;
            ErrorCallback m_errorCallback;
            std::atomic<bool> m_autoReconnect{false};
            std::atomic<bool> m_closed{false};
            std::atomic<int> m_reconnectDelay{3000};  // ms, from setAutoReconnect()
            std::atomic<int> m_serverRetry{-1};       // ms from the server's "retry:", -1 = none
            std::atomic<int> m_maxReconnectDelay{60000};  // ms
            SSEParser m_parser;
            mutable std::mutex m_stateMutex;
            std::mutex m_waitMutex;
            std::condition_variable m_waitCv;

            void reportError(const std::string& message)
            {
                if (m_errorCallback && !m_closed)
                {
                    m_errorCallback(message);
                }
            }

            /// Wait for `ms` milliseconds or until close() is called. Returns false if closed.
            bool waitBeforeReconnect(int ms)
            {
                std::unique_lock<std::mutex> lock(m_waitMutex);
                m_waitCv.wait_for(lock, std::chrono::milliseconds(std::max(0, ms)), [this] { return m_closed.load(); });
                return !m_closed;
            }

            bool run()
            {
                bool everConnected = false;
                bool lastOk = false;
                int consecutiveFailures = 0;

                while (!m_closed)
                {
                    m_parser.reset();

                    HttpClientRequest request;
                    request.method = METHOD_GET;
                    request.uri = m_url;
                    {
                        std::lock_guard<std::mutex> lock(m_stateMutex);
                        // Custom headers first, so the SSE-managed ones below win.
                        for (const auto& header : m_requestHeaders)
                        {
                            request.setHeader(header.first, header.second);
                        }
                        request.setAccept("text/event-stream");
                        request.setHeader("Cache-Control", "no-cache");
                        if (!m_lastEventId.empty())
                        {
                            request.setHeader("Last-Event-ID", m_lastEventId);
                        }
                    }

                    HttpClientResponse response;
                    bool streaming = false;
                    response.chunkCallback = [this, &response, &streaming](const std::string& chunk) {
                        if (!streaming)
                        {
                            // Only feed the parser for a successful event stream.
                            if (response.code != 200)
                            {
                                return;
                            }
                            streaming = true;
                        }
                        handleChunk(chunk);
                    };

                    const bool ok = send(request, response);
                    if (m_closed)
                    {
                        return everConnected || (ok && response.code == 200);
                    }

                    if (ok && response.code == 200)
                    {
                        everConnected = true;
                        lastOk = true;
                        consecutiveFailures = 0;
                    }
                    else if (ok)
                    {
                        // Per spec, a non-200 response fails the connection without reconnecting.
                        reportError("SSE endpoint returned HTTP status " + std::to_string(response.code));
                        return false;
                    }
                    else
                    {
                        lastOk = false;
                        ++consecutiveFailures;
                        reportError(response.code == 200 ? "SSE stream interrupted"
                                                         : "Failed to connect to SSE endpoint");
                        if (response.code == 200)
                        {
                            everConnected = true;
                            consecutiveFailures = 0;  // stream was up; retry at the base delay
                        }
                    }

                    if (!m_autoReconnect)
                    {
                        return lastOk;
                    }

                    // Base delay honors the server's retry: field; back off exponentially
                    // on consecutive connection failures.
                    const int baseDelay = m_serverRetry >= 0 ? m_serverRetry.load() : m_reconnectDelay.load();
                    int64_t delay = std::max(0, baseDelay);
                    for (int i = 1; i < consecutiveFailures && delay < m_maxReconnectDelay; ++i)
                    {
                        delay *= 2;
                    }
                    delay = std::min<int64_t>(delay, std::max(baseDelay, m_maxReconnectDelay.load()));
                    if (!waitBeforeReconnect(static_cast<int>(delay)))
                    {
                        break;
                    }
                }
                return everConnected || lastOk;
            }

            /// @brief Handle incoming chunk
            void handleChunk(const std::string& chunk)
            {
                auto events = m_parser.parseChunk(chunk);

                for (const auto& event : events)
                {
                    if (m_closed)
                    {
                        return;
                    }
                    // Update Last-Event-ID (an empty id resets it)
                    if (event.hasId)
                    {
                        std::lock_guard<std::mutex> lock(m_stateMutex);
                        m_lastEventId = event.id;
                    }

                    // Update retry delay if specified
                    if (event.retry >= 0)
                    {
                        m_serverRetry = event.retry;
                    }

                    // Dispatch only events with data (empty data buffer => no dispatch)
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
