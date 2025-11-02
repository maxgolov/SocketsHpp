# Proxy-Aware HTTP Server

This example demonstrates how to use `ProxyAwareHelpers` to extract real client information when your server is behind a reverse proxy (nginx, Apache, HAProxy, load balancer).

## Features

- Trust proxy configuration (security-first)
- X-Forwarded-* header support
- RFC 7239 Forwarded header support
- Real client IP extraction
- Protocol detection (HTTP/HTTPS)
- Host header extraction

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Running

```bash
./proxy-aware-server
```

The server will listen on `http://localhost:8080`.

## Testing

### Basic request
```bash
curl http://localhost:8080/
```

### With proxy headers
```bash
curl -H "X-Forwarded-For: 203.0.113.42" \
     -H "X-Forwarded-Proto: https" \
     -H "X-Forwarded-Host: example.com" \
     http://localhost:8080/
```

### nginx configuration
Add this to your nginx config to forward headers:

```nginx
location / {
    proxy_pass http://localhost:8080;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_set_header X-Forwarded-Host $host;
    proxy_set_header X-Real-IP $remote_addr;
}
```

## Security Note

Always use `TrustMode::TrustSpecific` in production and whitelist only your trusted proxy IPs. Using `TrustMode::TrustAll` allows clients to forge headers.
