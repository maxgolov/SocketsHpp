---
description: 'SocketsHpp - Cross-platform C++17 header-only networking library with HTTP/1.1 and MCP protocol support'
tools: ['edit', 'runNotebooks', 'search', 'new', 'runCommands', 'runTasks', 'github/github-mcp-server/*', 'perplexity/*', 'Microsoft Docs/*', 'usages', 'vscodeAPI', 'problems', 'changes', 'testFailure', 'openSimpleBrowser', 'fetch', 'githubRepo', 'extensions', 'todos']
---

# SocketsHpp Development Chat Mode

## Project Overview

**SocketsHpp** is a lean, minimalist, header-only C++17 networking library designed for seamless integration into large C++ projects with minimal impact. The library provides:

- **Cross-platform support**: Windows (x64/ARM64), Linux (x64/ARM64), macOS (x64/ARM64)
- **Single-threaded reactor pattern**: epoll (Linux), kqueue (BSD/macOS), IOCP (Windows)
- **HTTP/1.1 server**: ~75% RFC compliance, middleware support (auth, compression, proxy-aware)
- **Model Context Protocol (MCP)**: JSON-RPC server for AI agent integration
- **Optional multi-threading**: BS::thread_pool integration for CPU-intensive request processing
- **TCP/UDP/Unix Domain sockets**: Full socket abstraction layer

## Core Philosophy

**MINIMALIST & PRACTICAL**: 
- Header-only library - zero build dependencies for users
- No external runtime dependencies (except optional BS thread pool for multi-threading)
- Lean and mean - can be dropped into existing projects as a plugin
- Pragmatic over perfect - 75% HTTP compliance is sufficient for real-world use
- Performance matters - designed for <10K concurrent connections with low overhead

**WHEN IN DOUBT, ASK PERPLEXITY**: Use Perplexity research tools for:
- RFC compliance questions (HTTP/1.1, WebSocket, MCP protocol specs)
- Cross-platform socket API differences (Windows vs POSIX)
- C++17 best practices and standard library usage
- Thread safety patterns and concurrency primitives
- Performance optimization strategies

## Build System & Testing

### Multi-Platform Build Structure
```
build/
├── windows-x64/     # Windows x64 builds (vcpkg + MSVC/Clang)
├── linux-x64/       # Linux x64 builds (vcpkg manifest mode)
└── linux-arm64/     # ARM64 cross-compilation (QEMU + aarch64-linux-gnu)
```

### Build Commands
- **All platforms**: `.\build-all.cmd` (orchestrates all three builds from Windows)
- **Windows x64**: `powershell -ExecutionPolicy Bypass -File scripts\Build-Windows.ps1`
- **Linux x64**: `wsl bash -c "cd /mnt/c/build/maxgolov/SocketsHpp && ./scripts/build-linux.sh"`
- **Linux ARM64**: `wsl bash -c "cd /mnt/c/build/maxgolov/SocketsHpp && ./scripts/build-arm64.sh"`

### Test Coverage
- **231 tests on Windows** (48 Windows-specific tests)
- **183 tests on Linux/ARM64** (cross-platform core)
- **Unit tests**: Socket APIs, URL parsing, base64, auth, compression, middleware
- **Functional tests**: TCP/UDP echo, HTTP server lifecycle, streaming (SSE, chunked), HTTP methods

### Dependencies
- **vcpkg.json**: gtest, nlohmann-json, cpp-jwt, bshoshany-thread-pool
- **Git submodule**: simple-uri-parser
- **NO MANUAL INSTALLATIONS** - all dependencies managed via vcpkg or submodules

## Architecture & Design Patterns

### Reactor Pattern (Core)
- Single-threaded event loop using platform-specific I/O multiplexing
- Non-blocking sockets with edge-triggered events
- Connection state machine: `Idle → Readable → Processing → SendingHeaders → SendingBody → Idle`

### Multi-Threading (Optional)
- **Design**: `std::mutex` protecting connection map + optional `BS::thread_pool<>`
- **Flow**: Reactor handles I/O → Workers process requests → Signal reactor via `addSocket(Writable)`
- **New state**: `ProcessingAsync` for tracking worker-threaded requests
- **API**: `enableThreadPool(size_t numThreads)`, `disableThreadPool()`

### HTTP Server Features
- HTTP/1.1 with Keep-Alive, chunked transfer encoding
- Server-Sent Events (SSE) streaming
- Middleware: authentication (Bearer, API Key, Basic), compression (RLE, identity), proxy-aware headers
- Request/Response abstraction with query params, headers, cookies

### Known Limitations
- ~75% HTTP/1.1 compliance (sufficient for practical use)
- Designed for <10K concurrent connections
- Single-threaded reactor (multi-threading only for request processing)
- No HTTPS/TLS support (use reverse proxy like nginx)
- No WebSocket support yet (MCP uses HTTP + SSE)

## Critical Constraints

### Header-Only Requirements
- All implementation in headers (no .cpp files)
- Template-heavy code is acceptable for header-only design
- Inline functions to avoid ODR violations
- Guard against multiple includes with `#pragma once`

### Cross-Platform Compatibility
- Use `#ifdef _WIN32` for Windows-specific code (WinSock2, IOCP)
- Use `#ifdef __linux__` or `#ifdef __APPLE__` for POSIX (epoll/kqueue)
- Test on all platforms before merging (Windows, Linux x64, Linux ARM64)
- Handle platform differences in socket types (`SOCKET` vs `int`)

### Performance Guidelines
- Non-blocking I/O everywhere - no blocking `recv()`/`send()` in reactor
- Minimize memory allocations in hot paths
- Use `std::string_view` for zero-copy string operations when possible
- Avoid dynamic dispatch in critical paths (virtual calls okay for server callbacks)

### Code Quality Standards
- C++17 standard - no C++20 features (maintain compatibility)
- Comprehensive tests for all new features (unit + functional)
- No external runtime dependencies (except opt-in BS thread pool)
- Follow existing code style (no explicit style guide - match surrounding code)

## Recent Critical Fixes

### UDP Server Non-Blocking Socket (CRITICAL)
**Issue**: UDP tests hung on shutdown - reactor thread stuck in blocking `recvfrom()`
**Root Cause**: UDP server socket not set to non-blocking mode
**Fix**: Added `server_socket.setNonBlocking()` in `SocketServer` constructor for UDP sockets
**Impact**: Affected ALL platforms (not WSL2-specific as initially suspected)
**Location**: `include/SocketsHpp/net/common/socket_server.h` line 115

## Development Workflow

1. **Research First**: Use Perplexity for RFC specs, best practices, platform APIs
2. **Design Review**: Consider minimalist approach - is this feature essential?
3. **Header-Only**: Keep all code in headers, test on all platforms
4. **Test Coverage**: Add unit + functional tests for new features
5. **Build Verification**: Run `.\build-all.cmd` to validate all platforms
6. **Documentation**: Update README.md with examples and API docs

## AI Assistant Behavior

- **Be concise**: Direct answers, minimal fluff
- **Be practical**: 75% solution that works > 100% theoretical perfection
- **Use Perplexity**: Research RFCs, platform APIs, C++ standards before making assumptions
- **Test everything**: Add tests for all new code, run full build suite
- **Cross-platform first**: Consider Windows, Linux, ARM64 in all designs
- **No hacks**: Use proper dependency management (vcpkg), no manual installations
- **Performance aware**: Profile hot paths, minimize allocations, use non-blocking I/O

## Current Focus Areas

1. **Multi-threading**: Extend optional thread pool support to MCP server
2. **Test coverage**: Add unit tests for thread pool lifecycle, functional tests for concurrent requests
3. **Documentation**: Update README with multi-threading examples and performance characteristics
4. **MCP protocol**: Implement full JSON-RPC 2.0 spec for Model Context Protocol
5. **Performance**: Benchmark and optimize hot paths (connection handling, HTTP parsing)

## Resources & References

- **Build scripts**: `scripts/Build-Windows.ps1`, `scripts/build-linux.sh`, `scripts/build-arm64.sh`
- **Main headers**: `include/sockets.hpp`, `include/SocketsHpp/http/server/http_server.h`
- **Test structure**: `test/unit/`, `test/functional/`
- **Dependencies**: `vcpkg.json`, `.gitmodules` (simple-uri-parser)
- **CI/CD**: `build-all.cmd` (orchestrates all platform builds)