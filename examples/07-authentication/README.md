# Authenticated API Server

Protects routes with the library's authentication helpers from
`<SocketsHpp/http/server/authentication.h>`:

- `AuthenticationMiddleware<HttpRequest, HttpResponse>` tries its strategies in order.
  On failure it fills in the 401 response itself: status, a `WWW-Authenticate` header
  listing every strategy's challenge, and the JSON body `{"error": "Unauthorized"}`.
- `BearerTokenAuth`, `BasicAuth` and `ApiKeyAuth` extract the credential from the
  request and pass it to a validator callback that returns an `AuthResult`
  (`AuthResult::success(user, claims)` or `AuthResult::failure(reason)`).

## Routes

| Route | Accepts | Response |
|-------|---------|----------|
| `GET /` | anyone | HTML page |
| `GET /api/user` | Bearer token or Basic credentials | `{"role":...,"user":...}`; 401 otherwise |
| `GET /api/admin` | Bearer token or Basic credentials with the `admin` role | `{"admin":true,"user":"admin"}`; 401 without valid credentials, 403 for another role |
| `GET /api/service` | `X-API-Key` header | `{"service":...}`; 401 otherwise |
| any other path | anyone | 404 |

## Building

From the repository root:

```bash
cmake -S . -B build -DSOCKETSHPP_BUILD_EXAMPLES=ON
cmake --build build --target authenticated-api
```

## Running

```bash
./build/examples/07-authentication/authenticated-api          # port 8080
./build/examples/07-authentication/authenticated-api 9000     # another port
```

The server listens on all IPv4 interfaces. Press Ctrl+C to stop it.

## Testing

```bash
# Public page
curl http://localhost:8080/

# Bearer token
curl -H "Authorization: Bearer secret_token_123" http://localhost:8080/api/user
# {"role":"user","user":"user1"}

# Basic authentication
curl -u alice:wonderland http://localhost:8080/api/user
# {"role":"user","user":"alice"}

# No or wrong credentials: 401
curl -i http://localhost:8080/api/user
# HTTP/1.1 401 Unauthorized
# WWW-Authenticate: Bearer realm="API", Basic realm="Restricted"
# {"error": "Unauthorized"}

# Role check
curl -H "Authorization: Bearer admin_token_456" http://localhost:8080/api/admin
# {"admin":true,"user":"admin"}
curl -i -H "Authorization: Bearer secret_token_123" http://localhost:8080/api/admin
# HTTP/1.1 403 Forbidden
# {"error":"Forbidden","message":"admin role required"}

# API key
curl -H "X-API-Key: api_key_abc" http://localhost:8080/api/service
# {"service":"service1"}
curl -i http://localhost:8080/api/service
# HTTP/1.1 401 Unauthorized
# WWW-Authenticate: API-Key header="X-API-Key"
```

## Valid credentials

| Kind | Value | Identity | Role |
|------|-------|----------|------|
| Bearer token | `secret_token_123` | `user1` | `user` |
| Bearer token | `admin_token_456` | `admin` | `admin` |
| Basic | `alice` / `wonderland` | `alice` | `user` |
| API key | `api_key_abc` | `service1` | |
| API key | `api_key_xyz` | `service2` | |

## Using it in a route

```cpp
AuthenticationMiddleware<HttpRequest, HttpResponse> userAuth;
userAuth.addStrategy(std::make_shared<BearerTokenAuth<HttpRequest>>(checkToken));
userAuth.addStrategy(std::make_shared<BasicAuth<HttpRequest>>(checkPassword));

server.route("/api/user", [&userAuth](const HttpRequest& req, HttpResponse& res) -> int {
    AuthResult auth;
    if (!userAuth.authenticate(req, res, &auth))
    {
        return 401;  // res already holds the 401 response
    }
    res.set_content(json{{"user", auth.userId}}.dump(), "application/json");
    return 200;
});
```

Validators may run concurrently when the server uses a thread pool, so they must be
thread-safe. The credentials here live in constant in-memory maps for brevity; a real
server keeps hashed secrets and compares them in constant time. Use HTTPS (for example
behind a TLS-terminating proxy): Bearer tokens, Basic credentials and API keys travel
in plain text otherwise.
