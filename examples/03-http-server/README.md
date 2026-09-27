# HTTP Server Example

An HTTP/1.1 server with a few routes: an HTML page, plain text, JSON, query-parameter
parsing and a POST-only route that parses a JSON body.

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target http-server
```

## Running

```bash
./build/examples/03-http-server/http-server          # port 8080
./build/examples/03-http-server/http-server 9000     # another port
```

`HttpServer("0.0.0.0", port)` listens on all IPv4 interfaces (the first argument only
ends up in the `Server` response header). Press Ctrl+C to stop it: the signal handler
clears a flag, `main` leaves its wait loop and calls `server.stop()`.

## Routes

| Route | Response |
|-------|----------|
| `GET /` | The HTML home page (`text/html; charset=utf-8`) |
| `GET /hello` | `Hello from SocketsHpp HTTP Server!` (`text/plain`) |
| `GET /api/info` | A small JSON document (`application/json`) |
| `GET /echo?msg=...` | `Echo: <msg>` (URL-decoded, `text/plain`); 400 for a malformed query such as `?msg=%zz` |
| `POST /api/data` | JSON with the number of body bytes and the JSON type of the body; 400 if the body is not JSON; other methods get 405 with `Allow: POST` |
| any other path | 404 `Not found: <path>` |

```bash
curl -i http://localhost:8080/
curl http://localhost:8080/hello
curl http://localhost:8080/api/info
curl "http://localhost:8080/echo?msg=Hello%20there"
curl -i "http://localhost:8080/echo?msg=%zz"                  # 400
curl -X POST http://localhost:8080/api/data -H "Content-Type: application/json" -d '{"test":"data"}'
# {"receivedBytes":15,"status":"ok","type":"object"}
curl -i http://localhost:8080/api/data                        # 405, Allow: POST
curl -i http://localhost:8080/nope                            # 404
```

## Routing rules

Routes match by **prefix** of the request target, longest prefix first. So:

- `"/"` is a prefix of every URI and receives every request no longer route claims.
  Its handler checks the path and returns 404 unless it is exactly `/`:

  ```cpp
  server.route("/", [](const HttpRequest& req, HttpResponse& res) -> int {
      const std::string path = req.uri.substr(0, req.uri.find('?'));
      if (path != "/")
      {
          res.set_content("Not found: " + path + "\n", "text/plain");
          return 404;
      }
      ...
  });
  ```

  Without a `"/"` route the server answers an unmatched GET or POST with 404 itself.
- `"/hello"` also matches `/hello/x` and `/helloworld`; check `req.uri` in the handler
  if that matters.

A handler returns the status code (`return 200;`), or 0 after `set_status()` /
`set_content()`. See the [main README](../../README.md#http-server) for the details.

## What it demonstrates

- `server.route(path, handler)`, `server.start()` (non-blocking) and `server.stop()`
- `HttpRequest`: `method`, `uri`, `content` (request body) and `parse_query()`
- `HttpResponse::set_content(body, contentType)` and `set_header()` (for `Allow`)
- Building JSON with nlohmann::json (`sockets.hpp` already requires it)

## Errors in handlers

`parse_query()` throws `std::invalid_argument` for a malformed query string. The `/echo`
handler catches it and answers 400. An exception that escapes a handler does not stop
the server: the request gets a plain `500 Internal Server Error`.

## Expected output

```
HTTP server running on http://localhost:8080 - press Ctrl+C to stop
^CShutting down...
```
