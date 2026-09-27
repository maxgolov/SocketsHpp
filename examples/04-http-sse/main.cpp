// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Server-Sent Events (SSE) example
///
/// This example demonstrates:
/// - Streaming responses with send_chunk_stream()
/// - text/event-stream and SSEEvent formatting (default and custom event types)
/// - Running blocking stream callbacks on the worker pool
/// - A browser EventSource client
///
/// Usage: http-sse [port]   (default 8080)

#include <sockets.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>

using namespace SOCKETSHPP_NS::http::server;
using json = nlohmann::json;

namespace
{
    std::atomic<bool> g_running{true};
    void onSignal(int) { g_running = false; }

    const char* kPage = R"(<!DOCTYPE html>
<html>
<head><title>SSE Demo</title></head>
<body>
  <h1>Server-Sent Events Demo</h1>
  <div id="messages"></div>
  <script>
    const eventSource = new EventSource('/events');
    const div = document.getElementById('messages');
    function show(text, color) {
      const p = document.createElement('p');
      p.style.color = color;
      p.textContent = text;
      div.appendChild(p);
    }
    eventSource.onmessage = (event) => show('Message: ' + event.data, 'black');
    eventSource.addEventListener('custom', (event) => show('Custom event: ' + event.data, 'blue'));
    // The server sends "done" last. Without close() EventSource would reconnect
    // when the stream ends and replay it forever.
    eventSource.addEventListener('done', (event) => {
      show('Done: ' + event.data, 'green');
      eventSource.close();
    });
    eventSource.onerror = () => show('Connection error (the browser retries automatically)', 'red');
  </script>
</body>
</html>
)";
}  // namespace

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        const int port = argc > 1 ? std::stoi(argv[1]) : 8080;
        HttpServer server("0.0.0.0", port);
        // Handlers and stream callbacks run on this pool. The callbacks below sleep
        // between events, so each open stream keeps a worker busy: size the pool for
        // the number of concurrent streams you expect, plus room for other requests.
        server.enableThreadPool(8);

        server.route("/", [](const HttpRequest& req, HttpResponse& res) -> int {
            if (req.uri.substr(0, req.uri.find('?')) != "/")
            {
                res.set_content("Not found\n", "text/plain");
                return 404;
            }
            res.set_content(kPage, "text/html; charset=utf-8");
            return 200;
        });

        // The server calls the stream callback repeatedly and sends each returned
        // string right away; returning "" ends the stream. (send_chunk() in a loop
        // would only buffer everything into one response.)
        server.route("/events", [](const HttpRequest&, HttpResponse& res) -> int {
            // A stream has no body to pass to set_content(), so set the type directly.
            res.set_header("Content-Type", "text/event-stream");
            std::cout << "Client connected to /events" << std::endl;

            int sent = 0;
            res.send_chunk_stream(
                [sent]() mutable -> std::string {
                    if (sent == 10)
                    {
                        ++sent;
                        return SSEEvent::custom("done", "Stream complete").format();
                    }
                    if (sent > 10)
                    {
                        return "";  // end of stream
                    }
                    if (sent > 0)
                    {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    ++sent;
                    // SSEEvent::message(data, id): the id lets a reconnecting client
                    // resume (it is sent back in the Last-Event-ID header).
                    std::string chunk = SSEEvent::message("Event #" + std::to_string(sent) + " at " +
                                                              std::to_string(std::time(nullptr)),
                                                          std::to_string(sent))
                                            .format();
                    if (sent % 3 == 0)
                    {
                        // A named event type; browsers deliver it to addEventListener('custom').
                        chunk += SSEEvent::custom("custom", "This is a custom event type!").format();
                    }
                    return chunk;
                },
                // Runs when the callback returns "", not when the client disconnects early.
                []() { std::cout << "/events stream completed" << std::endl; });
            return 200;
        });

        // Structured data: JSON in the data field
        server.route("/json-events", [](const HttpRequest&, HttpResponse& res) -> int {
            res.set_header("Content-Type", "text/event-stream");

            int count = 0;
            res.send_chunk_stream([count]() mutable -> std::string {
                if (count == 5)
                {
                    return "";  // end of stream
                }
                if (count > 0)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                SSEEvent event;
                event.event = "json-update";
                event.id = std::to_string(count);
                event.data = json{{"type", "update"}, {"count", count}, {"timestamp", std::time(nullptr)}}.dump();
                ++count;
                return event.format();
            });
            return 200;
        });

        server.start();
        std::cout << "SSE server running - open http://localhost:" << port << " or run:" << std::endl;
        std::cout << "  curl -N http://localhost:" << port << "/events" << std::endl;

        while (g_running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "Shutting down..." << std::endl;
        server.stop();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
