# About Sockets.HPP

Simple no-frills cross-platform C++ header-only client-server sockets library (TCP, UDP, Unix Domain, HTTP).

This code contains incremental improvements on top of Apache License 2.0-licensed opensource
[Microsoft 1DS C/C++ Telemetry](https://github.com/microsoft/cpp_client_telemetry/blob/main/tests/common/HttpServer.hpp)
and [OpenTelemetry C++ SDK]() socket libraries. It is intended to be used in simple test apps
and unit tests. Code is not intended for production use. Use at own risk.

See [LICENSE](./LICENSE) for details.

# Getting started

1. [Install vcpkg](https://vcpkg.io/en/getting-started.html)

This step is only required if you intend to build tests. You may skip it if you intend to
provide your own source of [Google Test](https://github.com/google/googletest).

2. How to build

- Windows build: use `build.cmd`
- Pther OS build (Linux, Mac, Android, etc.): use `cmake` for your platform.

# Header-only design

Library may be included using `#include <sockets.hpp>`. It consists of the following components:

| File      | Description |
| --------- | ----------- |
| `http/common/url_parser.h` | Parser of URLs in format `http://host:port|host:port`. |
| `http/server/http_server.h` | HTTP server. |
| `http/server/http_file_server.h` | HTTP file server. |
| `net/common/socket_server.h` | Socket server that supports TCP, UDP and Unix Domain. |
| `net/common/socket_tools.h` | C++ socket client abstraction on top of BSD sockets and WinSock. |
| `config.h` | Configurable namespace definition. |
| `macros.h` | Common macros for debugging. |

The goal is to keep the library as lean as possible, to require only the standard system C++
runtime and the standard system socket library (POSIX sockets or WinSock). The code is known
to work well on Windows, Linux (including embedded), Android, iOS and Mac.
