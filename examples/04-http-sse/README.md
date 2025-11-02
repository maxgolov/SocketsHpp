# HTTP Server-Sent Events (SSE) Example

Demonstrates real-time event streaming using Server-Sent Events.

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Running

```bash
./http-sse
```

Then open http://localhost:8080 in a browser.

## Testing with curl

```bash
# Stream events (use -N to disable buffering)
curl -N http://localhost:8080/events

# Stream JSON events
curl -N http://localhost:8080/json-events
```

## What it demonstrates

- Server-Sent Events (SSE) with `text/event-stream`
- `SSEEvent` helper class for formatting events
- Event fields: `data`, `event`, `id`
- Streaming chunks with `res.send_chunk()`
- Custom event types
- JSON data in SSE events
- HTML/JavaScript SSE client

## SSE Format

Each event is formatted as:

```
id: 1
event: custom
data: Event message here
<blank line>
```

## Expected output (curl)

```
id: 1
data: Event #1 at 1704110400

id: 2
data: Event #2 at 1704110401

id: 3
data: Event #3 at 1704110402

id: 3-custom
event: custom
data: This is a custom event type!
...
```

## Browser output

The HTML page will display messages as they arrive in real-time, with custom events shown in blue.
