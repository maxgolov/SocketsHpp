// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file stdio_server.h
/// @brief MCP STDIO transport for MCPServer: mcp::server::StdioServerTransport.
///
/// Runs an MCPServer over newline-delimited JSON-RPC on stdin/stdout (MCP stdio
/// transport): one UTF-8 message per line, no embedded newlines, nothing but protocol
/// messages on stdout. Logs belong on stderr.

#include <SocketsHpp/config.h>
#include <SocketsHpp/mcp/common/mcp_config.h>
#include <SocketsHpp/mcp/server/mcp_server.h>
#include <SocketsHpp/utils/process.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <iostream>
#include <istream>
#include <map>
#include <mutex>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <fcntl.h>
#  include <io.h>
#endif

SOCKETSHPP_NS_BEGIN
namespace mcp
{
    namespace server
    {
        /// @brief Serves an MCPServer over the MCP stdio transport.
        ///
        /// The transport reads newline-delimited JSON-RPC messages, hands each one to
        /// MCPServer::processMessage() and writes every non-empty reply as one line.
        ///
        /// - Requests run on a small worker pool (Options::maxConcurrentRequests), so a
        ///   long-running tool does not block other requests and a
        ///   `notifications/cancelled` can reach it (register it with
        ///   MCPServer::registerCancellable()). A cancelled request gets no response.
        ///   Notifications and client responses are handled on the reading thread, in
        ///   order; a cancellation for a request that is still queued removes it.
        /// - Writes are serialized: each message is written as one line (with "\n").
        /// - Server-initiated messages: MCPServer::push_log() / push_progress() target
        ///   HTTP sessions only, so use notify(), log() and progress() of this class
        ///   instead (e.g. capture the transport in the tool handler). log() honours the
        ///   level the client set with `logging/setLevel` (default "warning").
        /// - End of input: requests already received are completed and answered, then
        ///   run() returns. stop() instead drops queued requests, cancels running ones
        ///   and returns as soon as the running handlers finish.
        ///
        /// Keep everything else off stdout: do not print to std::cout / stdout, and do
        /// not build a stdio server with HAVE_CONSOLE_LOG (it logs to stdout); log to
        /// stderr or with log().
        ///
        /// @code
        /// ServerConfig config;                   // transport defaults to STDIO
        /// MCPServer server(config);
        /// StdioServerTransport stdio(server);
        /// server.registerMethod("tools/call", [&](const json& p) -> json {
        ///     stdio.log("info", "demo", "calling " + p.value("name", ""));
        ///     return {{"content", json::array()}};
        /// });
        /// stdio.runStdio();                      // until stdin closes
        /// @endcode
        class StdioServerTransport
        {
        public:
            /// @brief Tuning knobs for StdioServerTransport.
            struct Options
            {
                /// @brief Worker threads, i.e. requests processed concurrently (default 4;
                ///        0 means 1).
                size_t maxConcurrentRequests = 4;
                /// @brief Requests waiting for a worker before the reader stops reading
                ///        input (default 256; 0 means 1).
                size_t maxQueuedRequests = 256;
                /// @brief Longest accepted line in bytes (default 4 MiB). Longer lines are
                ///        discarded and answered with a -32600 error (id null). Enforced
                ///        by the handle-based run() and runStdio(); with std::istream the
                ///        line is read first and then rejected.
                size_t maxMessageSize = 4 * 1024 * 1024;
            };

            /// @brief Create a transport for @p server with default Options.
            /// @param server Server whose processMessage() handles the messages; must
            ///        outlive this object.
            explicit StdioServerTransport(MCPServer& server)
                : m_server(server)
            {
            }

            /// @brief Create a transport for @p server.
            /// @param server  Server whose processMessage() handles the messages; must
            ///        outlive this object.
            /// @param options Worker, queue and message-size limits.
            StdioServerTransport(MCPServer& server, const Options& options)
                : m_server(server)
                , m_options(options)
            {
            }

            /// @brief Non-copyable.
            StdioServerTransport(const StdioServerTransport&) = delete;
            /// @brief Non-copyable.
            StdioServerTransport& operator=(const StdioServerTransport&) = delete;

            /// @brief Calls stop(). A run() in progress on another thread must have
            ///        returned before the object is destroyed.
            ~StdioServerTransport() { stop(); }

            /// @brief Serve on the process's stdin and stdout until stdin reaches end of
            ///        file or stop() is called. Blocks the calling thread.
            ///
            /// Uses the raw handles (file descriptors 0/1; GetStdHandle() on Windows,
            /// where stdin/stdout are also switched to binary mode with _setmode()), so
            /// stop() interrupts a blocked read. Pending C stdio / std::cout output is
            /// flushed first. On POSIX, writing to a closed stdout fails with EPIPE
            /// (no SIGPIPE) and ends the loop.
            /// @throws std::logic_error if this transport is already running.
            void runStdio()
            {
                std::fflush(stdout);
                std::cout.flush();
#ifdef _WIN32
                _setmode(_fileno(stdin), _O_BINARY);
                _setmode(_fileno(stdout), _O_BINARY);
                run(::GetStdHandle(STD_INPUT_HANDLE), ::GetStdHandle(STD_OUTPUT_HANDLE));
#else
#  if defined(F_SETNOSIGPIPE)
                ::fcntl(1, F_SETNOSIGPIPE, 1);
#  endif
                run(0, 1);
#endif
            }

            /// @brief Serve on native handles (e.g. pipe ends) until @p in reaches end
            ///        of file or stop() is called. Blocks the calling thread.
            /// @param in  Readable handle (file descriptor / HANDLE); not closed.
            /// @param out Writable handle; not closed.
            /// @throws std::logic_error if this transport is already running;
            ///         std::runtime_error if the reader cannot be set up.
            void run(utils::NativeHandle in, utils::NativeHandle out)
            {
                std::string buffer;
                bool discarding = false;
                std::vector<char> chunk(64 * 1024);
                auto readLine = [this, &buffer, &discarding, &chunk](std::string& line, bool& tooLong) -> bool {
                    tooLong = false;
                    for (;;)
                    {
                        const size_t nl = buffer.find('\n');
                        if (nl != std::string::npos)
                        {
                            const bool wasDiscarding = discarding;
                            discarding = false;
                            if (wasDiscarding)
                            {
                                buffer.erase(0, nl + 1);
                                tooLong = true;
                                line.clear();
                                return true;
                            }
                            line.assign(buffer, 0, nl);
                            buffer.erase(0, nl + 1);
                            return true;
                        }
                        if (buffer.size() > m_options.maxMessageSize)
                        {
                            discarding = true;  // skip the rest of this line
                            buffer.clear();
                        }
                        const long n = m_reader.read(chunk.data(), chunk.size());
                        if (n <= 0)
                        {
                            if (n == 0 && !buffer.empty() && !discarding)
                            {
                                line.swap(buffer);  // last line without a newline
                                buffer.clear();
                                return true;
                            }
                            return false;
                        }
                        buffer.append(chunk.data(), static_cast<size_t>(n));
                    }
                };
                auto write = [out](const std::string& data) {
                    return utils::writeAll(out, data.data(), data.size());
                };
                beginRun();
                if (!m_reader.open(in))
                {
                    m_running = false;
                    throw std::runtime_error("StdioServerTransport: cannot set up the input reader");
                }
                if (m_stopRequested)
                    m_reader.interrupt();
                serve(readLine, write);
            }

            /// @brief Serve on C++ streams until @p in reaches end of file or fails
            ///        (useful for tests and embedding). Blocks the calling thread.
            /// @param in  Input stream, read with std::getline().
            /// @param out Output stream; each message is written and flushed.
            /// @note stop() cannot interrupt a blocked std::getline(): it takes effect
            ///       when the next line (or end of file) arrives.
            /// @throws std::logic_error if this transport is already running.
            void run(std::istream& in, std::ostream& out)
            {
                auto readLine = [this, &in](std::string& line, bool& tooLong) -> bool {
                    if (!std::getline(in, line))
                        return false;
                    tooLong = line.size() > m_options.maxMessageSize;
                    return true;
                };
                auto write = [&out](const std::string& data) {
                    out.write(data.data(), static_cast<std::streamsize>(data.size()));
                    out.flush();
                    return static_cast<bool>(out);
                };
                beginRun();
                serve(readLine, write);
            }

            /// @brief Stop serving: the read loop ends, queued requests are dropped,
            ///        running cancellable requests are cancelled (as if the client had
            ///        sent notifications/cancelled), and run() returns once running
            ///        handlers finish. Thread-safe (callable from a handler); no-op when
            ///        not running.
            void stop()
            {
                m_stopRequested = true;
                m_reader.interrupt();
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_queueCv.notify_all();
            }

            /// @brief Whether run() / runStdio() is executing. Thread-safe.
            /// @return true while serving.
            bool isRunning() const { return m_running.load(); }

            /// @brief Send a JSON-RPC notification to the client. Thread-safe.
            /// @param method Notification method, e.g. "notifications/tools/list_changed".
            /// @param params Parameters (omitted when null).
            /// @return true if written; false when not running or stdout is closed.
            bool notify(const std::string& method, const json& params = json::object())
            {
                json msg = {{"jsonrpc", "2.0"}, {"method", method}};
                if (!params.is_null())
                    msg["params"] = params;
                return writeMessage(msg.dump(-1, ' ', false, json::error_handler_t::replace));
            }

            /// @brief Send a `notifications/message` (log) notification. Thread-safe.
            ///
            /// Suppressed when @p level is below the level the client requested with
            /// `logging/setLevel` (default "warning"); unknown level names are never
            /// suppressed.
            /// @param level  debug, info, notice, warning, error, critical, alert or emergency.
            /// @param logger Logger name.
            /// @param data   Message (string or any JSON value).
            /// @return true if written; false if suppressed, not running or stdout is closed.
            bool log(const std::string& level, const std::string& logger, const json& data)
            {
                const int idx = logLevelIndex(level);
                if (idx >= 0 && idx < m_minLogLevel.load())
                    return false;
                return notify("notifications/message", {{"level", level}, {"logger", logger}, {"data", data}});
            }

            /// @brief Send a `notifications/progress` notification. Thread-safe.
            /// @param token    progressToken from the request's params._meta.
            /// @param progress Progress so far (increasing).
            /// @param total    Optional total.
            /// @param message  Optional human-readable message (omitted when empty).
            /// @return true if written; false when not running or stdout is closed.
            bool progress(const json& token, double progress, std::optional<double> total = std::nullopt,
                          const std::string& message = std::string())
            {
                json params = {{"progressToken", token}, {"progress", progress}};
                if (total.has_value())
                    params["total"] = *total;
                if (!message.empty())
                    params["message"] = message;
                return notify("notifications/progress", params);
            }

            /// @brief Minimum log level index currently applied by log() (0 = debug ...
            ///        7 = emergency; default 3 = warning). Thread-safe.
            /// @return The index.
            int logLevel() const { return m_minLogLevel.load(); }

        private:
            struct Task
            {
                std::string line;
                json id;            // request id (null for batches)
                std::string method;
                json params;
            };

            MCPServer& m_server;
            Options m_options;
            std::atomic<bool> m_running{false};
            std::atomic<bool> m_stopRequested{false};
            std::atomic<int> m_minLogLevel{3};  // "warning"
            utils::PipeReader m_reader;

            std::mutex m_outMutex;
            std::function<bool(const std::string&)> m_write;  // guarded by m_outMutex

            std::mutex m_queueMutex;
            std::condition_variable m_queueCv;
            std::deque<Task> m_queue;
            std::map<std::string, json> m_inFlight;  // id.dump() -> id
            bool m_inputDone = false;

            static int logLevelIndex(const std::string& level)
            {
                static const char* const LEVELS[] = {"debug", "info", "notice", "warning",
                                                     "error", "critical", "alert", "emergency"};
                for (int i = 0; i < 8; ++i)
                {
                    if (level == LEVELS[i])
                        return i;
                }
                return -1;
            }

            void beginRun()
            {
                if (m_running.exchange(true))
                    throw std::logic_error("StdioServerTransport is already running");
                m_stopRequested = false;
            }

            bool writeMessage(const std::string& message)
            {
                std::lock_guard<std::mutex> lock(m_outMutex);
                if (!m_write)
                    return false;
                if (!m_write(message + "\n"))
                {
                    m_write = nullptr;  // the client is gone
                    stop();
                    return false;
                }
                return true;
            }

            template <typename ReadLine, typename Write>
            void serve(ReadLine& readLine, Write& write)
            {
                {
                    std::lock_guard<std::mutex> lock(m_outMutex);
                    m_write = write;
                }
                {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    m_queue.clear();
                    m_inFlight.clear();
                    m_inputDone = false;
                }

                const size_t workerCount = std::max<size_t>(1, m_options.maxConcurrentRequests);
                std::vector<std::thread> workers;
                for (size_t i = 0; i < workerCount; ++i)
                    workers.emplace_back([this] { workerLoop(); });

                std::string line;
                bool tooLong = false;
                while (!m_stopRequested && readLine(line, tooLong))
                {
                    if (m_stopRequested)
                        break;
                    if (tooLong)
                    {
                        writeMessage(JsonRpcResponse::failure(nullptr, JsonRpcError::invalidRequest("Message too large"))
                                         .serialize());
                        continue;
                    }
                    handleLine(line);
                }

                if (m_stopRequested)
                {
                    // stop(): drop queued work and cancel what is running.
                    std::vector<json> running;
                    {
                        std::lock_guard<std::mutex> lock(m_queueMutex);
                        m_queue.clear();
                        for (const auto& kv : m_inFlight)
                            running.push_back(kv.second);
                    }
                    for (const auto& id : running)
                    {
                        json cancel = {{"jsonrpc", "2.0"},
                                       {"method", "notifications/cancelled"},
                                       {"params", {{"requestId", id}, {"reason", "server shutting down"}}}};
                        m_server.processMessage(cancel.dump());
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    m_inputDone = true;
                }
                m_queueCv.notify_all();
                for (auto& t : workers)
                    t.join();

                {
                    std::lock_guard<std::mutex> lock(m_outMutex);
                    m_write = nullptr;
                }
                m_running = false;
            }

            void handleLine(std::string line)
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.find_first_not_of(" \t") == std::string::npos)
                    return;

                const json msg = json::parse(line, nullptr, false);
                const bool isRequest = msg.is_object() && msg.contains("method") && msg.contains("id");
                if (!isRequest && !msg.is_array())
                {
                    // Parse errors, notifications and client responses: handled in order
                    // on this thread (a cancellation must not wait behind busy workers).
                    if (msg.is_object() && msg.value("method", json()) == json("notifications/cancelled"))
                        dropQueued(msg);
                    const std::string reply = m_server.processMessage(line);
                    if (!reply.empty())
                        writeMessage(reply);
                    return;
                }

                Task task;
                task.line = std::move(line);
                if (isRequest)
                {
                    task.id = msg["id"];
                    if (msg["method"].is_string())
                        task.method = msg["method"].get<std::string>();
                    if (task.method == "logging/setLevel" && msg.contains("params"))
                        task.params = msg["params"];
                }
                std::unique_lock<std::mutex> lock(m_queueMutex);
                const size_t maxQueued = std::max<size_t>(1, m_options.maxQueuedRequests);
                m_queueCv.wait(lock, [&] { return m_stopRequested.load() || m_queue.size() < maxQueued; });
                if (m_stopRequested)
                    return;
                m_queue.push_back(std::move(task));
                m_queueCv.notify_all();
            }

            /// Remove a queued (not yet started) request named by a notifications/cancelled.
            void dropQueued(const json& cancelMsg)
            {
                const json params = cancelMsg.value("params", json::object());
                if (!params.is_object() || !params.contains("requestId"))
                    return;
                const json& id = params["requestId"];
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
                                             [&id](const Task& t) { return !t.id.is_null() && t.id == id; }),
                              m_queue.end());
                m_queueCv.notify_all();
            }

            void workerLoop()
            {
                for (;;)
                {
                    Task task;
                    std::string key;
                    {
                        std::unique_lock<std::mutex> lock(m_queueMutex);
                        m_queueCv.wait(lock, [this] { return !m_queue.empty() || m_inputDone; });
                        if (m_queue.empty())
                            return;  // input finished and nothing left to do
                        task = std::move(m_queue.front());
                        m_queue.pop_front();
                        if (!task.id.is_null())
                        {
                            key = task.id.dump();
                            m_inFlight[key] = task.id;
                        }
                        m_queueCv.notify_all();  // room in the queue for the reader
                    }

                    const std::string reply = m_server.processMessage(task.line);

                    if (task.method == "logging/setLevel" && task.params.is_object() &&
                        task.params.contains("level") && task.params["level"].is_string())
                    {
                        const json r = json::parse(reply, nullptr, false);
                        const int idx = logLevelIndex(task.params["level"].get<std::string>());
                        if (r.is_object() && r.contains("result") && idx >= 0)
                            m_minLogLevel = idx;
                    }
                    if (!key.empty())
                    {
                        std::lock_guard<std::mutex> lock(m_queueMutex);
                        m_inFlight.erase(key);
                    }
                    if (!reply.empty())
                        writeMessage(reply);
                }
            }
        };

    }  // namespace server
}  // namespace mcp
SOCKETSHPP_NS_END
