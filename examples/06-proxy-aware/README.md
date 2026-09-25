# Proxy-Aware HTTP Server

Uses `ProxyAwareHelpers` to recover the original client IP, scheme and host when the
server runs behind a reverse proxy (nginx, Apache, HAProxy, a load balancer).

## Features

- `TrustProxyConfig` with an explicit list of trusted proxies (`127.0.0.1`,
  `10.0.0.1`, `172.16.0.1`)
- `X-Forwarded-For`, `X-Forwarded-Proto`, `X-Forwarded-Host`, `X-Real-IP` and RFC 7239
  `Forwarded` headers, honoured only when the direct peer is a trusted proxy
- One route (`/`, which also catches every other path) returning an HTML page with the
  derived client IP, scheme, host, "secure" flag, the direct peer address and all
  request headers; each request is also logged to the console

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target proxy-aware-server
```

## Running

```bash
./build/examples/06-proxy-aware/proxy-aware-server
```

The server listens on port 8080 on all IPv4 interfaces (the `"localhost"` passed to
`HttpServer` is only the `Server` header name).

## Testing

```bash
# Direct request: shows your own address
curl http://localhost:8080/

# Simulated proxy headers. They are honoured because curl connects from 127.0.0.1,
# which is in the trusted list.
curl -H "X-Forwarded-For: 203.0.113.42" \
     -H "X-Forwarded-Proto: https" \
     -H "X-Forwarded-Host: example.com" \
     http://localhost:8080/
```

A request from an untrusted address with the same headers reports the direct
connection instead.

### nginx configuration

```nginx
location / {
    proxy_pass http://localhost:8080;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_set_header X-Forwarded-Host $host;
    proxy_set_header X-Real-IP $remote_addr;
}
```

## Security note

Use `TrustMode::TrustSpecific` (what `addTrustedProxy()` selects) in production and list
only your proxies. `TrustMode::TrustAll` lets any client forge its address and scheme.
