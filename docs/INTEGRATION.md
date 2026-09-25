# Integration Guide

SocketsHpp is header-only. Whatever the integration method, a consumer needs:

1. `include/` on the include path (`sockets.hpp` and `SocketsHpp/...`);
2. `BS_thread_pool.hpp` from [BS::thread_pool](https://github.com/bshoshany/thread-pool)
   **version 5.x** on the include path. `http_server.h` always includes it (it uses the
   v5 `BS::thread_pool<>` template), so everything built on the HTTP server needs it.
   A copy is bundled at `external/BS_thread_pool.hpp`;
3. [nlohmann/json](https://github.com/nlohmann/json) on the include path when you use
   `sockets.hpp`, `json_rpc.h` or any MCP header;
4. C++17, threads, and `ws2_32` on Windows;
5. optionally [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) plus the
   `SOCKETSHPP_HAS_JWT_CPP` definition for JWT validation in the MCP server.

The CMake target `SocketsHpp::SocketsHpp` covers 1, 2 and 4 (and 5 when CMake finds
jwt-cpp). The options below differ in where the sources come from and how
nlohmann/json is provided.

| Method | Good for |
|--------|----------|
| [add_subdirectory (git submodule)](#add_subdirectory-git-submodule) | Pinning the exact commit in your repository |
| [FetchContent](#fetchcontent) | CMake-only projects without a package manager |
| [Install + find_package](#install--find_package) | System-wide or prefix installs, other build systems via the installed headers |
| [vcpkg overlay port](#vcpkg-overlay-port) | Projects that already use vcpkg |
| [Manual include paths](#manual-include-paths-no-cmake) | Makefiles, IDE projects |

## add_subdirectory (git submodule)

```bash
git submodule add https://github.com/maxgolov/SocketsHpp.git external/SocketsHpp
git submodule update --init --recursive   # also fetches SocketsHpp's own submodules
```

```cmake
cmake_minimum_required(VERSION 3.14)
project(myapp CXX)

add_subdirectory(external/SocketsHpp)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE SocketsHpp::SocketsHpp)
```

Notes:

- Tests and examples are off by default (`SOCKETSHPP_BUILD_TESTS`, `BUILD_EXAMPLES`), so
  GoogleTest is not required. Install rules are off in subproject builds
  (`SOCKETSHPP_INSTALL` defaults to `ON` only for top-level builds).
- `SocketsHpp::SocketsHpp` carries nlohmann/json (needed by `sockets.hpp` and the MCP
  headers): an existing `nlohmann_json::nlohmann_json` target or package is used if
  present when SocketsHpp is added, otherwise the bundled `external/nlohmann-json`
  submodule.

## FetchContent

```cmake
cmake_minimum_required(VERSION 3.14)
project(myapp CXX)

include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_Declare(SocketsHpp
    GIT_REPOSITORY https://github.com/maxgolov/SocketsHpp.git
    GIT_TAG main)   # pin a tag or commit hash for reproducible builds
FetchContent_MakeAvailable(json SocketsHpp)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE SocketsHpp::SocketsHpp)
```

Declare nlohmann/json first so SocketsHpp picks up the same target.

## Install + find_package

```bash
git clone --recursive https://github.com/maxgolov/SocketsHpp.git
cmake -S SocketsHpp -B SocketsHpp/build -DCMAKE_BUILD_TYPE=Release
cmake --install SocketsHpp/build --prefix /opt/socketshpp
```

This installs `include/sockets.hpp`, `include/SocketsHpp/`, `include/BS_thread_pool.hpp`
(skip it with `-DSOCKETSHPP_INSTALL_BUNDLED_THREAD_POOL=OFF` if your system provides
the thread pool) and `lib/cmake/SocketsHpp/` (config, version and targets files).
nlohmann/json is not installed; install it separately if you need it.

```cmake
find_package(SocketsHpp 1.0 CONFIG REQUIRED)   # -DCMAKE_PREFIX_PATH=/opt/socketshpp
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE SocketsHpp::SocketsHpp)
```

The package config calls `find_dependency(Threads)`, finds jwt-cpp if SocketsHpp was
configured with it, and attaches `nlohmann_json::nlohmann_json` to
`SocketsHpp::SocketsHpp` whenever `find_package(nlohmann_json CONFIG)` succeeds.
The version file uses `SameMajorVersion` compatibility.

## vcpkg overlay port

The repository contains a port at [`ports/socketshpp`](../ports/socketshpp/README.md).
It is not part of the official vcpkg registry, so point vcpkg at the `ports`
directory as an overlay.

**Manifest mode.** Add the dependency to your `vcpkg.json`:

```json
{
  "name": "myapp",
  "version": "1.0.0",
  "dependencies": [ "socketshpp" ]
}
```

(write `{ "name": "socketshpp", "features": [ "jwt" ] }` instead to get JWT support),
then configure with the overlay:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_OVERLAY_PORTS=/path/to/SocketsHpp/ports
```

Alternatively list the directory in a `vcpkg-configuration.json` next to `vcpkg.json`:

```json
{
  "overlay-ports": [ "/path/to/SocketsHpp/ports" ]
}
```

**Classic mode:**

```bash
vcpkg install socketshpp --overlay-ports=/path/to/SocketsHpp/ports
```

Either way, use `find_package(SocketsHpp CONFIG REQUIRED)` and link
`SocketsHpp::SocketsHpp`. The port pulls in `nlohmann-json` and
`bshoshany-thread-pool` (>= 5.0.0). A complete project is in
[examples/11-vcpkg-consumption](../examples/11-vcpkg-consumption/).

## Manual include paths (no CMake)

Copy or reference `include/`, `external/BS_thread_pool.hpp` and the nlohmann/json
headers, then compile as C++17:

```bash
# Linux / macOS
g++ -std=c++17 -I SocketsHpp/include -I SocketsHpp/external \
    -I SocketsHpp/external/nlohmann-json/single_include main.cpp -o myapp -pthread

# MinGW-w64
x86_64-w64-mingw32-g++ -std=c++17 -I SocketsHpp/include -I SocketsHpp/external \
    -I SocketsHpp/external/nlohmann-json/single_include main.cpp -o myapp.exe -lws2_32
```

With MSVC add the same three directories to *Additional Include Directories* and set
`/std:c++17`; `ws2_32.lib` is linked through `#pragma comment(lib)` in the headers.

## Checking the integration

This program exercises the socket, HTTP and MCP headers (and therefore all required
dependencies):

```cpp
#include <sockets.hpp>
#include <iostream>

int main()
{
    using namespace SocketsHpp;

    net::utils::Socket sock(AF_INET, SOCK_STREAM, 0);
    sock.close();

    http::server::HttpServer server;
    int port = server.addListeningPort("127.0.0.1", 0);  // ephemeral port

    mcp::ServerConfig config;
    std::cout << "HTTP server bound to port " << port
              << ", MCP transport enum " << static_cast<int>(config.transport) << '\n';
    return port > 0 ? 0 : 1;
}
```

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| `nlohmann/json.hpp: No such file or directory` | You include `sockets.hpp` or an MCP header without nlohmann/json. Link `nlohmann_json::nlohmann_json` (see above) or add its include directory. |
| `BS_thread_pool.hpp: No such file or directory` | Add `external/` (or your installed thread-pool include directory) to the include path, or link `SocketsHpp::SocketsHpp`. |
| Errors around `BS::thread_pool<>` | An old (v3/v4) `BS_thread_pool.hpp` is found first. SocketsHpp needs v5. |
| Undefined references to `WSAStartup`, `socket`, ... with MinGW | Link `ws2_32` (the CMake target does this). |
| Undefined references to `pthread_*` | Link threads (`-pthread`, or `Threads::Threads`). |
| `Could not find a package configuration file provided by "SocketsHpp"` | Set `CMAKE_PREFIX_PATH` to the install prefix, or use the vcpkg toolchain file with the overlay port. |
| `SSRF guard: refusing to bind to non-loopback address` | `MCPServer` binds loopback only unless `ServerConfig::allowNonLoopback` is set. |

## Next steps

- [README](../README.md) for API examples
- [FEATURES.md](FEATURES.md) for what is and is not implemented
- [MCP_IMPLEMENTATION.md](MCP_IMPLEMENTATION.md) for the MCP server and client
- [examples/](../examples/README.md) for runnable programs
