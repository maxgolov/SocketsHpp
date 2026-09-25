// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Server-Sent Events (SSE) example
///
/// This example demonstrates:
/// - Streaming responses with SSE
/// - Real-time event broadcasting
/// - text/event-stream content type
/// - SSEEvent formatting

#include <sockets.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace SOCKETSHPP_NS::http::server;

int main()
{
    try
    {
        HttpServer server("0.0.0.0", 8080);
        // Stream callbacks may block (they sleep between events); running them on a
        // worker pool keeps the server responsive to other clients.
        server.enableThreadPool(4);
        std::cout << "SSE Server starting on http://localhost:8080" << std::endl;

        // Serve a simple HTML page with SSE client
        server.route("/", [](const HttpRequest& req, HttpResponse& res) -> int {
            res.set_header("Content-Type", "text/html");
            res.set_content(R"(
<!DOCTYPE html>
<html>
<head><title>SSE Demo</title></head>
<body>
    <h1>Server-Sent Events Demo</h1>
    <div id="messages"></div>
    <script>
        const eventSource = new EventSource('/events');
        const div = document.getElementById('messages');
        
        eventSource.onmessage = (event) => {
            const p = document.createElement('p');
            p.textContent = 'Message: ' + event.data;
            div.appendChild(p);
        };
        
        eventSource.addEventListener('custom', (event) => {
            const p = document.createElement('p');
            p.style.color = 'blue';
            p.textContent = 'Custom event: ' + event.data;
            div.appendChild(p);
        });
        
        eventSource.onerror = () => {
            const p = document.createElement('p');
            p.style.color = 'red';
            p.textContent = 'Connection lost!';
            div.appendChild(p);
        };
    </script>
</body>
</html>
            )");
            return 200;
        });

        // SSE endpoint - streams events to clients.
        //
        // Streaming works by handing the server a callback via send_chunk_stream():
        // it is invoked repeatedly, each returned string is sent to the client right
        // away, and returning an empty string ends the stream. (Writing events with
        // send_chunk() in a loop would only buffer them into one response.)
        server.route("/events", [](const HttpRequest&, HttpResponse& res) -> int {
            res.set_header("Content-Type", "text/event-stream");
            std::cout << "Client connected to SSE stream" << std::endl;

            int sent = 0;
            res.send_chunk_stream(
                [sent]() mutable -> std::string {
                    if (sent == 10)
                    {
                        SSEEvent done = SSEEvent::custom("done", "Stream complete");
                        ++sent;
                        return done.format();
                    }
                    if (sent > 10)
                    {
                        return "";  // end of stream
                    }
                    if (sent > 0)
                    {
                        // Runs on a thread-pool worker, so waiting here does not
                        // block other clients.
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    ++sent;
                    std::string chunk = SSEEvent::message("Event #" + std::to_string(sent) + " at " +
                                                              std::to_string(std::time(nullptr)),
                                                          std::to_string(sent))
                                            .format();
                    // Every 3rd event, also send a custom event type
                    if (sent % 3 == 0)
                    {
                        chunk += SSEEvent::custom("custom", "This is a custom event type!",
                                                  std::to_string(sent) + "-custom")
                                     .format();
                    }
                    std::cout << "Sent event #" << sent << std::endl;
                    return chunk;
                },
                []() { std::cout << "SSE stream completed" << std::endl; });
            return 200;
        });

        // JSON SSE endpoint - demonstrates structured data
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
                event.data = R"({"type":"update","count":)" + std::to_string(count) +
                             R"(,"timestamp":)" + std::to_string(std::time(nullptr)) + "}";
                event.id = std::to_string(count);
                event.event = "json-update";
                ++count;
                return event.format();
            });
            return 200;
        });

        std::cout << "Server running!" << std::endl;
        std::cout << "Open http://localhost:8080 to see SSE in action" << std::endl;
        std::cout << "Or use curl: curl -N http://localhost:8080/events" << std::endl;

        server.start();
        
        // Keep server running
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
