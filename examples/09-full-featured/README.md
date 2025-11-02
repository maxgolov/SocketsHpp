# Full-Featured HTTP Server

This example combines all three enterprise features into a single production-ready server:

1. **Proxy Awareness** - Extract real client information behind reverse proxies
2. **Authentication** - Multi-strategy authentication (Bearer, API Key, Basic)
3. **Compression** - Automatic content compression

## Features

- Trust proxy configuration with IP whitelisting
- Multi-strategy authentication chain
- Automatic compression with content negotiation
- Public and protected endpoints
- JSON API responses
- Comprehensive request logging

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Running

```bash
./full-featured-server
```

The server will listen on `http://localhost:8080`.

## Testing

### Public endpoint (no auth, no compression)
```bash
curl http://localhost:8080/public
```

### Authenticated API request
```bash
curl -H "Authorization: Bearer secret_token_123" \
     http://localhost:8080/api/user
```

### Authenticated + Compressed response
```bash
curl -H "Authorization: Bearer secret_token_123" \
     -H "Accept-Encoding: rle" \
     http://localhost:8080/api/data
```

### All features combined (proxy + auth + compression)
```bash
curl -H "X-Forwarded-For: 203.0.113.42" \
     -H "X-Forwarded-Proto: https" \
     -H "Authorization: Bearer secret_token_123" \
     -H "Accept-Encoding: rle" \
     http://localhost:8080/api/data
```

### API key authentication
```bash
curl -H "X-API-Key: api_key_abc" \
     http://localhost:8080/api/user
```

### Basic authentication
```bash
curl -u alice:password123 \
     http://localhost:8080/api/user
```

## Architecture

The server demonstrates a typical production deployment:

```
[Client] → [nginx/HAProxy] → [This Server]
            ↓
         X-Forwarded-For: client_ip
         X-Forwarded-Proto: https
            ↓
       [Proxy Awareness]
            ↓
       [Authentication]
            ↓
       [Route Handler]
            ↓
       [Compression]
            ↓
       [Response]
```

## Security Configuration

**Proxy Trust:**
- Only trusts specific proxy IPs (127.0.0.1 by default)
- Prevents header forgery attacks

**Authentication:**
- Tries all strategies in order
- Returns 401 for protected endpoints
- Logs successful authentications

**Compression:**
- Only compresses responses > 500 bytes
- Respects client Accept-Encoding preferences
- Adds Vary: Accept-Encoding header

## Valid Credentials

Same as authentication example:

**Bearer Tokens:**
- `secret_token_123` → user1
- `admin_token_456` → admin

**API Keys:**
- `api_key_abc` → service1

**Basic Auth:**
- alice:password123
