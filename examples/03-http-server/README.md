# HTTP Server Example

An HTTP/1.1 server on port 8080 with a few routes: an HTML page, plain text, JSON,
query-parameter parsing and a POST-only route.

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target http-server
```

## Running

```bash
./build/examples/03-http-server/http-server
```

`HttpServer("0.0.0.0", 8080)` listens on port 8080 on all IPv4 interfaces (the first
argument only ends up in the `Server` response header). Stop it with Ctrl+C.

## Routes

| Route | Response |
|-------|----------|
| `GET /hello` | `Hello from SocketsHpp HTTP Server!` (text/plain) |
| `GET /api/info` | A small JSON document |
| `GET /echo?msg=...` | `Echo: <msg>` (URL-decoded) |
| `POST /api/data` | Reports the number of body bytes received; other methods get 405 |
| `GET /` (and any other path) | The HTML home page: `/` is a prefix route, so it catches every URI not claimed by a longer route |

```bash
curl http://localhost:8080/
curl http://localhost:8080/hello
curl http://localhost:8080/api/info
curl "http://localhost:8080/echo?msg=Hello%20there"
curl -X POST http://localhost:8080/api/data -d '{"test":"data"}'
```

## What it demonstrates

- Creating a server with `HttpServer(name, port)` and starting it with `start()`
  (non-blocking; `main` then sleeps in a loop)
- Registering handlers with `server.route(path, handler)`; handlers return an `int`
  status (these return 0 and rely on the body or `set_status()` to mark the request as
  handled, see the routing rules in the [main README](../../README.md#http-server))
- `HttpRequest`: `method`, `content` (request body) and `parse_query()`
- `HttpResponse`: `set_header()`, `set_content()`, `set_status()`
- HTML, JSON and plain-text responses

Note: this example calls `parse_query()` without a `try`/`catch`. `parse_query()`
throws `std::invalid_argument` for malformed query strings (for example `?a=%zz`), and
an exception escaping a handler terminates the server, so real code should catch it
(see the README's `/hello` example).

## Expected output

```
HTTP Server starting on http://localhost:8080
Server running! Press Ctrl+C to stop
Visit http://localhost:8080 in your browser
```
