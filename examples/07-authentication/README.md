# Authenticated API Server

Protects routes with Bearer tokens and API keys. The checks are written directly in
the route handlers (a `checkAuth()` helper looks the credential up in two in-memory
maps); the example does not use the library's `authentication.h` strategies.

## Features

- Public page at `/` (which, as a prefix route, also answers any unknown path)
- `GET /api/user` and `GET /api/service`, each accepting **either** a valid
  `Authorization: Bearer <token>` **or** a valid `X-API-Key: <key>` header
- 401 with a JSON error body otherwise (`/api/user` also sends
  `WWW-Authenticate: Bearer`)

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target authenticated-api
```

## Running

```bash
./build/examples/07-authentication/authenticated-api
```

The server listens on port 8080 on all IPv4 interfaces.

## Testing

```bash
# Public page
curl http://localhost:8080/

# Bearer token
curl -H "Authorization: Bearer secret_token_123" http://localhost:8080/api/user
# {"user": "user1", "endpoint": "/api/user"}

# API key
curl -H "X-API-Key: api_key_abc" http://localhost:8080/api/service
# {"service": "service1", "endpoint": "/api/service"}

# No credentials: 401
curl -i http://localhost:8080/api/user
```

## Valid credentials

| Kind | Value | Identity |
|------|-------|----------|
| Bearer token | `secret_token_123` | `user1` |
| Bearer token | `admin_token_456` | `admin` |
| API key | `api_key_abc` | `service1` |
| API key | `api_key_xyz` | `service2` |

There is no Basic authentication and no admin-only route in this example.

## Using the library's authentication helpers

`SocketsHpp/http/server/authentication.h` provides `BearerTokenAuth`, `ApiKeyAuth`,
`BasicAuth` and `AuthenticationMiddleware`, which tries strategies in order and fills
in the 401 response (JSON body and `WWW-Authenticate` challenges). The
[main README](../../README.md#authentication) shows how to call it from a route.
