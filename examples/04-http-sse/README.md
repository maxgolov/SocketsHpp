# HTTP Server-Sent Events (SSE) Example

Real-time event streaming with Server-Sent Events, plus a small browser client.

## Building

From the repository root:

```bash
cmake -S . -B build -DSOCKETSHPP_BUILD_EXAMPLES=ON
cmake --build build --target http-sse
```

## Running

```bash
./build/examples/04-http-sse/http-sse          # port 8080
./build/examples/04-http-sse/http-sse 9000     # another port
```

Then open http://localhost:8080 in a browser (the page subscribes to `/events` with
`EventSource`), or use curl:

```bash
curl -N http://localhost:8080/events        # 10 events, 1 s apart, then "done"
curl -N http://localhost:8080/json-events   # 5 JSON events, 0.5 s apart
```

Press Ctrl+C to stop the server.

## Routes

| Route | Response |
|-------|----------|
| `/events` | `text/event-stream`: events `#1`-`#10` (ids `1`-`10`) one second apart, an extra `custom` event after every third one, then a `done` event, then the stream ends |
| `/json-events` | `text/event-stream`: five `json-update` events whose data is a JSON object |
| `/` | The HTML/JavaScript demo page (`text/html; charset=utf-8`) |
| any other path | 404 |

## What it demonstrates

- Streaming with `res.send_chunk_stream(callback, onEnd)`: the server calls the
  callback repeatedly, sends each returned chunk immediately (chunked transfer
  encoding), and ends the stream when it returns `""`. (`res.send_chunk()` only appends
  to a buffered response; it does not stream.) A stream has no body, so the route sets
  `Content-Type: text/event-stream` with `set_header()`.
- `onEnd` runs only when the callback ends the stream. It does **not** run when the
  client disconnects early (close the curl command after a few events: nothing is
  printed), so do not rely on it for cleanup of per-client state.
- `server.enableThreadPool(8)`: handlers and stream callbacks run on the worker pool.
  The callbacks sleep between events, so each open stream keeps a worker busy for most
  of its lifetime. With more open streams than workers, events are delayed and every
  other request waits in the queue behind the sleeping callbacks. Size the pool for
  the number of concurrent streams you expect, plus room for other requests. Without a pool the callbacks would run on the
  reactor thread and block every other connection while they sleep.
- `SSEEvent::message(data, id)`, `SSEEvent::custom(event, data)` and setting
  `event` / `data` / `id` directly, then `format()`. The `id` is what a reconnecting
  browser sends back in `Last-Event-ID`; the `custom` events carry no id.
- Headers the server adds for `text/event-stream`: `Cache-Control: no-cache` and
  `X-Accel-Buffering: no`
- A browser `EventSource` client: default messages, a `custom` event listener, and a
  `done` listener that calls `eventSource.close()`. Without it the browser would
  reconnect when the stream ends and replay it forever.

## Expected output (curl -N /events)

```
id: 1
data: Event #1 at 1790466882

id: 2
data: Event #2 at 1790466883

id: 3
data: Event #3 at 1790466884

event: custom
data: This is a custom event type!

id: 4
data: Event #4 at 1790466885
...
id: 10
data: Event #10 at 1790466891

event: done
data: Stream complete

```

In the browser, messages appear as they arrive, custom events in blue and the final
`done` event in green.
