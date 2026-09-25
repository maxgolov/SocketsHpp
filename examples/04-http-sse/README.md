# HTTP Server-Sent Events (SSE) Example

Real-time event streaming with Server-Sent Events, plus a small browser client.

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target http-sse
```

## Running

```bash
./build/examples/04-http-sse/http-sse
```

Then open http://localhost:8080 in a browser (the page subscribes to `/events` with
`EventSource`), or use curl:

```bash
curl -N http://localhost:8080/events        # 10 events, 1 s apart, then "done"
curl -N http://localhost:8080/json-events   # 5 JSON events, 0.5 s apart
```

## Routes

| Route | Response |
|-------|----------|
| `/events` | `text/event-stream`: events `#1`-`#10` (ids `1`-`10`) one second apart, an extra `custom` event after every third one, then a `done` event, then the stream ends |
| `/json-events` | `text/event-stream`: five `json-update` events carrying JSON data |
| `/` (and any other path) | The HTML/JavaScript demo page |

## What it demonstrates

- Streaming with `res.send_chunk_stream(callback, onEnd)`: the server calls the
  callback repeatedly, sends each returned chunk immediately (chunked transfer
  encoding), and ends the stream when it returns `""`. (`res.send_chunk()` only appends
  to a buffered response; it does not stream.)
- `server.enableThreadPool(4)`: the callbacks sleep between events, and on the worker
  pool that does not block other clients
- `SSEEvent::message()`, `SSEEvent::custom()` and setting `event`/`data`/`id` directly,
  then `format()`
- Headers the server adds for `text/event-stream`: `Cache-Control: no-cache` and
  `X-Accel-Buffering: no`
- A browser `EventSource` client handling default and custom event types

## Expected output (curl)

```
id: 1
data: Event #1 at 1790323626

id: 2
data: Event #2 at 1790323627

id: 3
data: Event #3 at 1790323628

id: 3-custom
event: custom
data: This is a custom event type!

id: 4
data: Event #4 at 1790323629
...
```

In the browser, messages appear as they arrive; custom events are shown in blue.
