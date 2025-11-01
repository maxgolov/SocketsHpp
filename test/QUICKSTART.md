# Quick Test Guide

## Build and Run Tests Quickly

### Windows (PowerShell)
```powershell
# Configure and build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Debug

# Run all tests
cd build
ctest -C Debug --output-on-failure

# Run only unit tests (fast)
ctest -C Debug -R "^unit\." -V

# Run only functional tests
ctest -C Debug -R "^functional\." -V
```

### Linux/macOS (Bash)
```bash
# Configure and build
cmake -B build -S .
cmake --build build

# Run all tests
cd build
ctest --output-on-failure

# Run only unit tests (fast)
ctest -R "^unit\." -V

# Run only functional tests
ctest -R "^functional\." -V
```

## Test Organization

### Unit Tests (fast, isolated)
- `url_parser_test` - URL parsing validation
- `socket_addr_test` - Socket address parsing
- `socket_basic_test` - Basic socket operations

### Functional Tests (integration, end-to-end)
- `sockets_test` - TCP echo server scenarios
- `sockets_udp_test` - UDP communication
- `http_server_test` - HTTP server lifecycle

## Running Individual Tests

```bash
# Run specific test executable
./build/test/unit/url_parser_test
./build/test/functional/sockets_test

# Run with test filtering
./build/test/unit/url_parser_test --gtest_filter="*IPv6*"
./build/test/unit/socket_addr_test --gtest_filter="SocketAddrTest.*"
```

## Adding New Tests

1. **Unit test**: Create file in `test/unit/`, add to `UNIT_TESTS` in CMakeLists.txt
2. **Functional test**: Create file in `test/functional/`, add to `FUNCTIONAL_TESTS` in CMakeLists.txt

See `test/README.md` for detailed documentation.
