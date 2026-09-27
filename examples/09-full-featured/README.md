# Full-Featured HTTP Server

A small notes API that combines the server features shown one at a time in examples
03-08:

| Feature | How |
|---------|-----|
| Worker thread pool | `server.enableThreadPool(4)`; the shared notes list is guarded by a mutex |
| CORS | `enableCors()`, `setCorsOrigin("http://localhost:3000")`, `setCorsHeaders(...)`; preflight `OPTIONS` requests get 204 from the server |
| Authentication | `AuthenticationMiddleware` with `BearerTokenAuth` and `ApiKeyAuth` (see [example 07](../07-authentication/)) |
| Proxy awareness | `TrustProxyConfig` + `ProxyAwareHelpers` (trusted proxy: `127.0.0.1`, see [example 06](../06-proxy-aware/)) |
| Compression | `CompressionMiddleware` with the toy `rle` codec (see [example 08](../08-compression/)) |
| JSON | request and response bodies with nlohmann::json |
| Graceful shutdown | Ctrl+C / SIGTERM stop the server |

## Building

From the repository root:

```bash
cmake -S . -B build -DSOCKETSHPP_BUILD_EXAMPLES=ON
cmake --build build --target full-featured-server
```

## Running

```bash
./build/examples/09-full-featured/full-featured-server          # port 8080
./build/examples/09-full-featured/full-featured-server 9000     # another port
```

The server listens on all IPv4 interfaces.

## Routes

| Route | Auth | Response |
|-------|------|----------|
| `GET /` | none | HTML page showing your (proxy-aware) address and scheme |
| `GET /api/notes` | Bearer token or API key | `{"notes":[...]}` |
| `POST /api/notes` | Bearer token or API key | Body `{"text": "..."}`; 201 with the stored note (id, text, author, proxy-aware client IP); 400 for another body |
| `OPTIONS` any path | none | 204 CORS preflight answer |
| other methods on `/api/notes` | | 405 with `Allow: GET, POST, OPTIONS` |
| any other path | none | 404 |

Every handler returns 0 for `OPTIONS` without touching the response, which declines the
request and lets the server answer the preflight. Preflights carry no credentials, so
this has to happen before authentication.

## Testing

```bash
curl http://localhost:8080/

# 401 with WWW-Authenticate: Bearer realm="API", API-Key header="X-API-Key"
curl -i http://localhost:8080/api/notes

# Add a note. The X-Forwarded-For header is honoured because the request comes from
# 127.0.0.1, a trusted proxy in this example.
curl -H "Authorization: Bearer secret_token_123" \
     -H "Content-Type: application/json" \
     -H "X-Forwarded-For: 203.0.113.42" \
     -d '{"text": "Hello from curl"}' \
     http://localhost:8080/api/notes
# {"author":"user1","clientIP":"203.0.113.42","id":1,"text":"Hello from curl"}

curl -H "X-API-Key: api_key_abc" http://localhost:8080/api/notes
# {"notes":[{"author":"user1","clientIP":"203.0.113.42","id":1,"text":"Hello from curl"}]}

# CORS preflight: 204 with Access-Control-Allow-Origin: http://localhost:3000
curl -i -X OPTIONS http://localhost:8080/api/notes \
     -H "Origin: http://localhost:3000" \
     -H "Access-Control-Request-Method: POST"

# Compression: "rle" only pays off for repetitive bodies of 256 bytes or more
curl -H "X-API-Key: api_key_abc" -H "Content-Type: application/json" \
     -d "{\"text\": \"$(printf '=%.0s' $(seq 300))\"}" \
     http://localhost:8080/api/notes > /dev/null
curl -si -H "X-API-Key: api_key_abc" -H "Accept-Encoding: rle" http://localhost:8080/api/notes -o /dev/null -D -
# ... Content-Encoding: rle ...
```

Every response carries the CORS headers (`Access-Control-Allow-Origin:
http://localhost:3000`, allowed headers `Content-Type, Authorization, X-API-Key`,
exposed header `Content-Encoding`).

## Request flow

```
[Client] -> [nginx / HAProxy] -> [This server]
                                      |
                        route handler on a worker thread
                          - OPTIONS: declined, the server answers the preflight (204)
                          - AuthenticationMiddleware (401)
                          - ProxyAwareHelpers (only for trusted peers)
                          - nlohmann::json body parsing (400)
                          - CompressionMiddleware (Content-Encoding)
                                      |
                        server adds CORS headers and sends the response
```

## Credentials

| Kind | Value | Identity |
|------|-------|----------|
| Bearer token | `secret_token_123` | `user1` |
| Bearer token | `admin_token_456` | `admin` |
| API key | `api_key_abc` | `service1` |

Notes are kept in memory (at most 100; more gives 507) and are lost when the server
stops.
