# HTTP Server Example

A simple HTTP/1.1 server with routing, query parameters, and JSON responses.

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Running

```bash
./http-server
```

The server will start on http://localhost:8080

## Testing

Open a browser and visit:
- http://localhost:8080 - HTML homepage
- http://localhost:8080/hello - Plain text response
- http://localhost:8080/api/info - JSON response
- http://localhost:8080/echo?msg=test - Echo with query parameter

Or use curl:

```bash
# GET requests
curl http://localhost:8080/
curl http://localhost:8080/api/info
curl http://localhost:8080/echo?msg=Hello

# POST request
curl -X POST http://localhost:8080/api/data -d '{"test":"data"}'
```

## What it demonstrates

- Creating an HTTP server with `HttpServer(port)`
- Route registration with `server.route(path, handler)`
- Request handling with `HttpRequest` and `HttpResponse`
- Query parameter parsing with `req.parse_query()`
- Setting response headers with `res.set_header()`
- Sending responses with `res.send()`
- Handling different HTTP methods (GET, POST)
- Serving HTML, JSON, and plain text content

## Expected output

```
HTTP Server starting on http://localhost:8080
Server running! Press Ctrl+C to stop
Visit http://localhost:8080 in your browser
```
