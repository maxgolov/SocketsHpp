# Full-Featured HTTP Server

Combines two of the server helpers in one program:

1. **Proxy awareness**: `TrustProxyConfig` + `ProxyAwareHelpers` recover the real
   client IP, scheme and host behind a trusted reverse proxy.
2. **Authentication**: Bearer tokens and API keys, checked in the handlers by a
   `checkAuth()` helper (as in [example 07](../07-authentication/)).

It does not use compression or Basic authentication.

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target full-featured-server
```

## Running

```bash
./build/examples/09-full-featured/full-featured-server
```

The server listens on port 8080 on all IPv4 interfaces.

## Routes

| Route | Auth | Response |
|-------|------|----------|
| `/` (and any unknown path) | none | HTML page with the proxy-aware client IP, scheme, host and URI |
| `/api/protected` | Bearer token or API key | JSON with the user, the proxy-aware client IP and `"authenticated": true`; 401 with `WWW-Authenticate: Bearer` otherwise |
| `/api/service` | Bearer token or API key | JSON with the identity; 401 otherwise |

## Testing

```bash
curl http://localhost:8080/

curl -H "Authorization: Bearer secret_token_123" http://localhost:8080/api/protected

curl -H "X-API-Key: api_key_abc" http://localhost:8080/api/service

# Proxy headers + auth. Honoured because the request comes from 127.0.0.1,
# which is a trusted proxy in this example.
curl -H "X-Forwarded-For: 203.0.113.42" \
     -H "X-Forwarded-Proto: https" \
     -H "Authorization: Bearer secret_token_123" \
     http://localhost:8080/api/protected
# {"user": "user1","endpoint": "/api/protected","clientIP": "203.0.113.42","authenticated": true}

curl -i http://localhost:8080/api/protected   # 401
```

## Request flow

```
[Client] -> [nginx / HAProxy] -> [This server]
                 |
          X-Forwarded-For, X-Forwarded-Proto, X-Forwarded-Host
                 |
          ProxyAwareHelpers (only if the peer is a trusted proxy)
                 |
          checkAuth() in the /api/* handlers
                 |
          JSON / HTML response
```

## Configuration

- Trusted proxies: `127.0.0.1`, `10.0.0.1`, `172.16.0.1` (`TrustMode::TrustSpecific`).
  Forwarded headers from any other peer are ignored.
- Credentials: Bearer `secret_token_123` (user1) and `admin_token_456` (admin);
  API key `api_key_abc` (service1).

To add compression, see [example 08](../08-compression/); for the library's
authentication strategies, see the [main README](../../README.md#authentication).
