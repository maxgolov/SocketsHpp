# SocketsHpp Test Suite

GoogleTest-based unit and functional tests, plus two compile/link checks for the
header-only guarantee. There are 500+ test cases; `ctest -N` lists the current set.

## Building and running

Tests are off by default. Enable them with `SOCKETSHPP_BUILD_TESTS=ON`; CMake then
requires GoogleTest (`find_package(GTest CONFIG REQUIRED)`).

```bash
# Linux / macOS (GoogleTest from apt: libgtest-dev, or brew: googletest)
cmake -S . -B build -DSOCKETSHPP_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

```powershell
# Windows (GoogleTest and nlohmann-json from the vcpkg manifest)
cmake -S . -B build -DSOCKETSHPP_BUILD_TESTS=ON `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

If GoogleTest is installed in a non-default prefix (Homebrew, a custom build), pass
`-DCMAKE_PREFIX_PATH=<prefix>`. Useful extras: `-DENABLE_ASAN=ON -DENABLE_UBSAN=ON`,
`-DSOCKETSHPP_WARNINGS_AS_ERRORS=ON`. For ARM64 under QEMU see
[docs/ARM64.md](../docs/ARM64.md); for MinGW-w64 under Wine see the
[README](../README.md#cross-compiling).

### Selecting tests

Each GoogleTest case becomes one ctest test named `unit.<Suite>.<Test>` or
`functional.<Suite>.<Test>`:

```bash
ctest --test-dir build -R '^unit\.'                        # all unit tests
ctest --test-dir build -R '^functional\.'                  # all functional tests
ctest --test-dir build -R 'unit\.UrlParserTest\.'          # one suite
ctest --test-dir build -R 'functional\.StreamableHttpTest' --output-on-failure
```

Every test binary is written to `build/test/<name>` (under `build/test/<Config>/` with
multi-config generators) and accepts the usual GoogleTest flags:

```bash
./build/test/url_parser_test --gtest_list_tests
./build/test/url_parser_test --gtest_filter='*IPv6*'
./build/test/http_client_test --gtest_filter='HttpClientRedirectTest.*'
```

## Test binaries

The lists come from `test/CMakeLists.txt`.

### Unit tests (`test/unit/`, ctest prefix `unit.`)

| Binary | Covers |
|--------|--------|
| `url_parser_test` | `UrlParser` (schemes, hosts, IPv6, ports, queries) |
| `url_security_test` | Query-string decoding limits (NUL and control characters, bad escapes, too many or too long parameters) |
| `socket_addr_test` | `SocketAddr` parsing and formatting (IPv4, IPv6, Unix domain) |
| `socket_basic_test` | `Socket` creation, options and loopback round trips |
| `base64_test` | `utils::Base64` |
| `proxy_aware_test` | `TrustProxyConfig`, `ProxyAwareHelpers` |
| `authentication_test` | Bearer / API key / Basic strategies and the middleware |
| `compression_test` | Registry, `Accept-Encoding` negotiation, middleware (plus the Windows codecs on Windows) |
| `sse_parser_test` | `SSEParser` (WHATWG parsing rules) |
| `thread_pool_server_test` | `net::server::ThreadPoolServer` |
| `http_server_hardening_test` | Query parsing limits, SSE formatting, session ids, file-server path containment, auth/compression/proxy hardening |
| `json_rpc_test`, `json_rpc_minimal_test` | JSON-RPC 2.0 types and ids (need nlohmann/json) |
| `mcp_config_test` | `ServerConfig` / `ClientConfig` parsing (needs nlohmann/json) |

### Functional tests (`test/functional/`, ctest prefix `functional.`)

| Binary | Covers |
|--------|--------|
| `sockets_test` | TCP echo servers over IPv4, IPv6 and Unix domain sockets |
| `sockets_udp_test` | UDP send/receive |
| `socket_server_test` | `SocketServer` lifecycle and connections |
| `tcp_server_test` | `net::tcp::TcpServer` |
| `http_server_test` | Server lifecycle and request dispatch rules |
| `http_streaming_test` | Chunked streaming and SSE responses |
| `http_methods_test` | Methods, HEAD, OPTIONS/CORS, DELETE |
| `http_file_server_test` | `HttpFileServer` |
| `http_client_test` (POSIX only) | `HttpClient` (redirects, schemes, IPv6, bodies) and `SSEClient` |
| `http_server_robustness_test` (POSIX only) | Request smuggling, pipelining, backpressure, error responses, thread pool, lifetime |
| `mcp_streamable_test` (POSIX only, needs nlohmann/json) | `MCPServer` on both HTTP transports and STDIO, sessions, auth (JWT with jwt-cpp), rate limiting, cancellation, resumability, `MCPClient` |

The POSIX-only tests drive the code over raw POSIX sockets and are not built on Windows.

### Header checks

| Target | What it checks |
|--------|----------------|
| `header_self_contained_check` | Static library with one generated source per public header, each including its header twice: every header compiles on its own and has working include guards. Built, not run. |
| `header_only_link_check` | Two translation units that each include every public header, linked into one executable and run as a ctest: catches non-`inline` definitions. |

MCP headers and `sockets.hpp` are left out of both when nlohmann/json is unavailable;
`compression_windows.h` is only checked on Windows.

## Writing tests

1. Add `<name>_test.cc` to `test/unit/` or `test/functional/`.
2. Add the name to `UNIT_TESTS` or `FUNCTIONAL_TESTS` in `test/CMakeLists.txt` (or add
   a dedicated block if the test is platform-specific or needs extra libraries).
3. Every test target links `SocketsHpp::SocketsHpp` and has `test/` on its include
   path, so `#include "common/test_utils.h"` and `#include <sockets.hpp>` work.

`test/common/test_utils.h` provides `GetTempDirectory()`, `GenerateBigString()`,
`GetRandomEphemeralPort()`, `GetUniqueSocketName()`, `TestDataGenerator` and
`TestAddresses`. Functional tests should bind to port 0 (for example
`HttpServer::addListeningPort("127.0.0.1", 0)`) and read the port back, rather than use
fixed ports.
