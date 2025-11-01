# SocketsHpp Test Suite

This directory contains the test suite for SocketsHpp, organized into **unit tests** and **functional tests**.

## Test Framework

The test suite uses **Google Test (GTest)**, which is:
- ✅ Cross-platform (Windows, Linux, macOS)
- ✅ Industry standard for C++ testing
- ✅ Well-documented and widely supported
- ✅ Integrates seamlessly with CMake and various CI/CD systems

## Directory Structure

```
test/
├── common/           # Shared test utilities and helpers
│   └── test_utils.h  # Common functions for test data generation
├── unit/             # Unit tests - test individual components in isolation
│   ├── url_parser_test.cc      # URL parsing logic tests
│   ├── socket_addr_test.cc     # Socket address parsing and validation
│   └── socket_basic_test.cc    # Basic socket operations
├── functional/       # Functional tests - test end-to-end scenarios
│   ├── sockets_test.cc         # TCP echo server integration tests
│   ├── sockets_udp_test.cc     # UDP socket integration tests
│   ├── http_server_test.cc     # HTTP server functionality tests
│   └── utils.h                 # Functional test utilities
├── CMakeLists.txt    # Test build configuration
└── README.md         # This file
```

## Test Categories

### Unit Tests (`test/unit/`)

Unit tests focus on testing individual components in isolation:

- **url_parser_test.cc**: Tests URL parsing with various formats (HTTP, HTTPS, IPv4, IPv6, query strings)
- **socket_addr_test.cc**: Tests socket address parsing, validation, and string conversion
- **socket_basic_test.cc**: Tests basic socket creation, lifecycle, and parameter validation

These tests are:
- Fast to execute
- Don't require network resources
- Test one component at a time
- Easy to debug

### Functional Tests (`test/functional/`)

Functional tests verify end-to-end scenarios and component integration:

- **sockets_test.cc**: TCP echo server tests with various protocols (IPv4, IPv6, Unix domain)
- **sockets_udp_test.cc**: UDP socket communication tests
- **http_server_test.cc**: HTTP server lifecycle and basic operations

These tests:
- Verify complete workflows
- May create actual network connections
- Test multiple components working together
- Ensure real-world scenarios work correctly

## Building and Running Tests

### Prerequisites

The tests require Google Test to be installed. If using vcpkg:

```bash
vcpkg install gtest
```

### Build Tests

```bash
# Configure with CMake
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Build all tests
cmake --build build --target all

# Or build specific test targets
cmake --build build --target url_parser_test
cmake --build build --target sockets_test
```

### Run All Tests

```bash
# Using CTest (recommended)
cd build
ctest --output-on-failure

# Run tests with verbose output
ctest -V
```

### Run Specific Tests

```bash
# Run all unit tests
ctest -R "^unit\."

# Run all functional tests
ctest -R "^functional\."

# Run a specific test file
ctest -R url_parser_test

# Or run the test executable directly
./build/test/url_parser_test
```

### Run Tests with Filtering

Google Test supports runtime filtering:

```bash
# Run specific test cases
./build/test/url_parser_test --gtest_filter="*BasicHttp*"

# Run all tests except certain ones
./build/test/socket_addr_test --gtest_filter="-*IPv6*"

# List all tests without running
./build/test/url_parser_test --gtest_list_tests
```

## Test Naming Conventions

Tests follow this naming pattern:
```
TEST_F(TestFixtureName, TestName)
```

Example:
```cpp
TEST_F(UrlParserTest, BasicHttpUrl)  // Tests basic HTTP URL parsing
TEST_F(SocketAddrTest, IPv6_BasicParsing)  // Tests IPv6 address parsing
```

## Adding New Tests

### Adding a Unit Test

1. Create a new file in `test/unit/` with suffix `_test.cc`
2. Add the test file to `UNIT_TESTS` list in `test/CMakeLists.txt`
3. Follow the existing test structure with test fixtures

Example:
```cpp
#include <gtest/gtest.h>
#include "sockets.hpp"

class MyComponentTest : public ::testing::Test {
protected:
    void SetUp() override { /* setup code */ }
    void TearDown() override { /* cleanup code */ }
};

TEST_F(MyComponentTest, BasicFunctionality) {
    // Test code here
    EXPECT_TRUE(some_condition);
}
```

### Adding a Functional Test

1. Create a new file in `test/functional/` with suffix `_test.cc`
2. Add the test file to `FUNCTIONAL_TESTS` list in `test/CMakeLists.txt`
3. Include any necessary utilities from `common/` or `functional/utils.h`

## Common Test Utilities

The `test/common/test_utils.h` file provides shared utilities:

- `GetTempDirectory()` - Get platform-specific temp directory
- `GenerateBigString(size)` - Generate large test payloads
- `GetRandomEphemeralPort()` - Get random port for testing
- `GetUniqueSocketName(prefix)` - Generate unique Unix socket names
- `TestDataGenerator` - Pre-defined test data of various sizes
- `TestAddresses` - Common test addresses (loopback, any, etc.)

## Best Practices

1. **Keep unit tests fast** - Avoid I/O operations in unit tests
2. **Isolate tests** - Each test should be independent
3. **Use descriptive names** - Test names should clearly describe what they test
4. **Test edge cases** - Include boundary conditions and error cases
5. **Clean up resources** - Ensure sockets/files are properly closed
6. **Use fixtures** - Share setup/teardown code via test fixtures
7. **Avoid hardcoded ports** - Use ephemeral port range or random ports for functional tests

## Continuous Integration

Tests are designed to run in CI environments:
- Tests use localhost/loopback addresses
- No external dependencies required
- Cross-platform compatible
- Return proper exit codes for CI systems

## Troubleshooting

### Tests fail with "Address already in use"
- Change the port numbers in tests to avoid conflicts
- Use ephemeral port range (49152-65535)
- Ensure previous test runs cleaned up properly

### GTest not found
- Install GTest via package manager or vcpkg
- Set `CMAKE_PREFIX_PATH` to point to GTest installation
- Check that `find_package(GTest)` succeeds

### Tests timeout
- Increase timeout in CTest configuration
- Check for deadlocks in server/client code
- Verify network interfaces are available

## Contributing

When contributing tests:
1. Follow the existing structure and naming conventions
2. Add appropriate documentation
3. Ensure tests pass on multiple platforms
4. Update this README if adding new test categories
