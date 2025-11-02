# Base64 Encoder/Decoder

Header-only RFC 4648 compliant base64 encoding and decoding implementation.

## Features

- **Header-only** - No compilation required, just include and use
- **RFC 4648 compliant** - Standard base64 encoding/decoding
- **Zero dependencies** - Uses only C++ standard library
- **Exception safety** - Throws `std::invalid_argument` for malformed input
- **Multiple interfaces** - Class-based and namespace-based APIs
- **Full test coverage** - 21 comprehensive unit tests

## Usage

### Basic Encoding/Decoding

```cpp
#include <SocketsHpp/utils/base64.h>

using namespace SocketsHpp::utils;

// Encode a string
std::string data = "Hello, World!";
std::string encoded = Base64::encode(data);
// encoded = "SGVsbG8sIFdvcmxkIQ=="

// Decode a base64 string
std::string decoded = Base64::decode(encoded);
// decoded = "Hello, World!"
```

### Binary Data

```cpp
// Encode binary data
const unsigned char binary[] = {0x00, 0x01, 0x02, 0xFF, 0xFE};
std::string encoded = Base64::encode(binary, sizeof(binary));

// Decode back to binary
std::string decoded = Base64::decode(encoded);
// decoded contains the original binary data
```

### Validation

```cpp
// Check if a string is valid base64
if (Base64::is_valid("SGVsbG8sIFdvcmxkIQ=="))
{
    // Valid base64, safe to decode
    std::string decoded = Base64::decode("SGVsbG8sIFdvcmxkIQ==");
}

// Invalid base64 throws exception
try
{
    Base64::decode("Invalid@Base64!");
}
catch (const std::invalid_argument& e)
{
    // Handle invalid base64
}
```

### Alternative API (namespace-based)

```cpp
using namespace SocketsHpp::utils::base64;

std::string encoded = encode("Hello");
std::string decoded = decode(encoded);
bool valid = is_valid(encoded);
```

## API Reference

### Class: `Base64`

#### Static Methods

```cpp
// Encode binary data to base64 string
static std::string encode(const unsigned char* data, size_t len);

// Encode string to base64
static std::string encode(const std::string& data);

// Decode base64 string to binary data
static std::string decode(const std::string& encoded_string);
// Throws: std::invalid_argument if input contains invalid base64 characters

// Validate base64 string
static bool is_valid(const std::string& str);
```

### Namespace: `SocketsHpp::utils::base64`

Convenience functions with identical signatures to the class methods:

```cpp
std::string encode(const unsigned char* data, size_t len);
std::string encode(const std::string& data);
std::string decode(const std::string& encoded_string);
bool is_valid(const std::string& str);
```

## Examples

### HTTP Basic Authentication

```cpp
std::string credentials = "username:password";
std::string encoded = Base64::encode(credentials);
std::string auth_header = "Authorization: Basic " + encoded;
```

### Data URLs

```cpp
unsigned char image_data[] = { /* ... */ };
std::string encoded = Base64::encode(image_data, sizeof(image_data));
std::string data_url = "data:image/png;base64," + encoded;
```

### WebSocket Protocol

```cpp
std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
// ... perform SHA-1 hash ...
std::string accept = Base64::encode(hash_result, 20);
```

## Performance

- Encoding: ~3-4 characters output per input byte (33% expansion)
- Decoding: ~3-4 bytes output per 4 input characters
- Memory: Minimal overhead, single pass processing
- Large data: Tested with 10KB+ payloads

## Test Coverage

The implementation includes 21 comprehensive tests covering:

- RFC 4648 test vectors
- Binary data encoding/decoding
- Round-trip verification
- Special characters and UTF-8
- All ASCII values (0-255)
- Edge cases (empty strings, padding variations)
- Invalid input handling
- Large data sets

All tests pass on Windows (MSVC), Linux (GCC), and ARM64 platforms.

## License

SPDX-License-Identifier: Apache-2.0
