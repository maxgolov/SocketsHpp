# Authenticated API Server

This example demonstrates the authentication framework with multiple strategies working together.

## Features

- Bearer token authentication (OAuth 2.0 style)
- API key authentication (custom headers)
- HTTP Basic authentication
- Multi-strategy support (tries all strategies)
- Authentication middleware
- Protected and public endpoints

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Running

```bash
./authenticated-api
```

The server will listen on `http://localhost:8080`.

## Testing

### Public endpoint (no auth)
```bash
curl http://localhost:8080/public
```

### Bearer token authentication
```bash
curl -H "Authorization: Bearer secret_token_123" \
     http://localhost:8080/api/user
```

### API key authentication
```bash
curl -H "X-API-Key: api_key_abc" \
     http://localhost:8080/api/user
```

### HTTP Basic authentication
```bash
curl -u alice:password123 \
     http://localhost:8080/api/user
```

### Admin endpoint (requires admin token)
```bash
curl -H "Authorization: Bearer admin_token_456" \
     http://localhost:8080/api/admin
```

### Unauthorized request (should fail with 401)
```bash
curl http://localhost:8080/api/user
```

## Valid Credentials

**Bearer Tokens:**
- `secret_token_123` → user1
- `admin_token_456` → admin

**API Keys:**
- `api_key_abc` → service1
- `api_key_xyz` → service2

**Basic Auth:**
- alice:password123
- bob:securepass
