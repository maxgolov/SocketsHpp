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
        HttpServer server(8080);
        std::cout << "SSE Server starting on http://localhost:8080" << std::endl;

        // Serve a simple HTML page with SSE client
        server.route("/", [](const HttpRequest& req, HttpResponse& res) {
            res.set_header("Content-Type", "text/html");
            res.send(R"(
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
        });

        // SSE endpoint - streams events to clients
        server.route("/events", [](const HttpRequest& req, HttpResponse& res) {
            // Set SSE headers
            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");

            std::cout << "Client connected to SSE stream" << std::endl;

            // Send events periodically
            for (int i = 0; i < 10; i++)
            {
                // Standard event
                SSEEvent event;
                event.data = "Event #" + std::to_string(i + 1) + 
                           " at " + std::to_string(std::time(nullptr));
                event.id = std::to_string(i + 1);
                
                res.send_chunk(event.format());
                std::cout << "Sent event #" << (i + 1) << std::endl;

                // Every 3rd event, send a custom event
                if ((i + 1) % 3 == 0)
                {
                    SSEEvent custom;
                    custom.event = "custom";
                    custom.data = "This is a custom event type!";
                    custom.id = std::to_string(i + 1) + "-custom";
                    res.send_chunk(custom.format());
                }

                // Wait 1 second between events
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            // Send completion message
            SSEEvent done;
            done.data = "Stream complete";
            done.event = "done";
            res.send_chunk(done.format());

            std::cout << "SSE stream completed" << std::endl;
        });

        // JSON SSE endpoint - demonstrates structured data
        server.route("/json-events", [](const HttpRequest& req, HttpResponse& res) {
            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");

            for (int i = 0; i < 5; i++)
            {
                SSEEvent event;
                event.data = R"({"type":"update","count":)" + std::to_string(i) + 
                           R"(,"timestamp":)" + std::to_string(std::time(nullptr)) + "}";
                event.id = std::to_string(i);
                event.event = "json-update";
                
                res.send_chunk(event.format());
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });

        std::cout << "Server running!" << std::endl;
        std::cout << "Open http://localhost:8080 to see SSE in action" << std::endl;
        std::cout << "Or use curl: curl -N http://localhost:8080/events" << std::endl;

        server.listen();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
