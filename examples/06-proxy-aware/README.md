# Proxy-Aware HTTP Server

Uses `ProxyAwareHelpers` to recover the original client IP, scheme and host when the
server runs behind a reverse proxy (nginx, Apache, HAProxy, a load balancer).

```
[Client] -> [nginx / HAProxy] -> [This server]
```

The proxy describes the original request in `X-Forwarded-For`, `X-Forwarded-Proto`,
`X-Forwarded-Host`, `X-Real-IP` or RFC 7239 `Forwarded` headers. Anyone can send those
headers, so they are honoured only when the direct peer is a trusted proxy.

## Features

- `TrustProxyConfig` with an explicit list of trusted proxies (`127.0.0.1`,
  `10.0.0.1`, `172.16.0.1`)
- `GET /api/client`: the derived client IP, scheme, host, "secure" flag and the direct
  peer as JSON (built with nlohmann::json)
- `GET /` (and every other path): an HTML page with the same details plus the URI and
  all request headers. Every value taken from the request is HTML-escaped, otherwise a
  crafted header or URL would inject script into the page (reflected XSS).
- Each page request is logged to the console

## Building

From the repository root:

```bash
cmake -S . -B build -DSOCKETSHPP_BUILD_EXAMPLES=ON
cmake --build build --target proxy-aware-server
```

## Running

```bash
./build/examples/06-proxy-aware/proxy-aware-server          # port 8080
./build/examples/06-proxy-aware/proxy-aware-server 9000     # another port
```

The server listens on all IPv4 interfaces. Press Ctrl+C to stop it.

## Testing

```bash
# Direct request: reports your own address
curl http://localhost:8080/api/client
# {"clientIP":"127.0.0.1","directPeer":"127.0.0.1:47580","host":"localhost:8080","protocol":"http","secure":false}

# Simulated proxy headers. They are honoured because curl connects from 127.0.0.1,
# which is in the trusted list.
curl -H "X-Forwarded-For: 203.0.113.42" \
     -H "X-Forwarded-Proto: https" \
     -H "X-Forwarded-Host: example.com" \
     http://localhost:8080/api/client
# {"clientIP":"203.0.113.42","directPeer":"127.0.0.1:47586","host":"example.com","protocol":"https","secure":true}

# The HTML version
curl -i http://localhost:8080/
```

The same headers sent from an untrusted address (for example from another machine, to
the server's LAN address) are ignored and the direct connection is reported instead.

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

## Security notes

- Use `TrustMode::TrustSpecific` (what `addTrustedProxy()` selects) in production and
  list only your proxies. `TrustMode::TrustAll` lets any client forge its address and
  scheme.
- The values returned by `ProxyAwareHelpers` come from request headers: escape them
  before putting them in HTML. `getClientIP()` only ever returns a literal IPv4/IPv6
  address (forwarded values that are not addresses are skipped), but `getHost()`
  returns `X-Forwarded-Host` / `Host` verbatim.
