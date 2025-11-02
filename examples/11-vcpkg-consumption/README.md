# Example 11: vcpkg Consumption

This example demonstrates how to consume SocketsHpp in your own project using vcpkg.

## Prerequisites

- CMake 3.15 or higher
- C++17 compatible compiler (MSVC, GCC, Clang)
- vcpkg installed and configured

## Quick Start

### Option 1: Using vcpkg Overlay (Recommended for development)

This allows you to use SocketsHpp directly from this repository without waiting for official vcpkg integration.

#### Windows (PowerShell)

```powershell
# Clone this repository (if not already done)
git clone https://github.com/maxgolov/SocketsHpp
cd SocketsHpp/examples/11-vcpkg-consumption

# Install SocketsHpp with overlay ports
vcpkg install socketshpp --overlay-ports=../../ports

# Configure and build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release

# Run the example
.\build\Release\vcpkg-consumer.exe
```

#### Linux / macOS (Bash)

```bash
# Clone this repository (if not already done)
git clone https://github.com/maxgolov/SocketsHpp
cd SocketsHpp/examples/11-vcpkg-consumption

# Install SocketsHpp with overlay ports
vcpkg install socketshpp --overlay-ports=../../ports

# Configure and build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release

# Run the example
./build/vcpkg-consumer
```

### Option 2: Using vcpkg from Git URL

You can also install SocketsHpp directly from the GitHub repository using vcpkg's Git support:

#### Create a vcpkg-configuration.json in your project:

```json
{
  "default-registry": {
    "kind": "git",
    "baseline": "latest",
    "repository": "https://github.com/microsoft/vcpkg"
  },
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/maxgolov/SocketsHpp",
      "baseline": "main",
      "packages": ["socketshpp"]
    }
  ]
}
```

Then install:

```bash
vcpkg install socketshpp
```

### Option 3: Using vcpkg Manifest Mode (Modern Approach)

The example includes a `vcpkg.json` manifest file. Simply configure with the vcpkg toolchain:

```bash
# vcpkg will automatically install dependencies
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

## What This Example Demonstrates

1. **Minimal vcpkg integration**: Shows the smallest possible setup
2. **Automatic dependency management**: vcpkg handles nlohmann-json, cpp-jwt, and bshoshany-thread-pool
3. **CMake configuration**: Proper `find_package()` usage
4. **Header-only consumption**: No linking required, just include headers

## Project Structure

```
11-vcpkg-consumption/
├── CMakeLists.txt          # CMake configuration with find_package
├── vcpkg.json              # vcpkg manifest (automatic dependency install)
├── main.cpp                # Simple HTTP server using SocketsHpp
├── setup-windows.ps1       # Windows setup script
├── setup-linux.sh          # Linux/macOS setup script
└── README.md               # This file
```

## Troubleshooting

### vcpkg not found

Set the `VCPKG_ROOT` environment variable:

**Windows:**
```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

**Linux/macOS:**
```bash
export VCPKG_ROOT=/path/to/vcpkg
```

### Overlay ports not working

Make sure the overlay path is correct relative to your current directory:
```bash
vcpkg install socketshpp --overlay-ports=../../ports
```

### Dependencies not found

Ensure vcpkg is bootstrapped:
```bash
# Windows
.\vcpkg\bootstrap-vcpkg.bat

# Linux/macOS
./vcpkg/bootstrap-vcpkg.sh
```

## Integration into Your Own Project

To use SocketsHpp in your own project:

1. **Copy the port overlay** (for development):
   ```bash
   cp -r /path/to/SocketsHpp/ports /path/to/your-project/
   ```

2. **Create vcpkg.json** in your project root:
   ```json
   {
     "name": "your-project",
     "version": "1.0.0",
     "dependencies": ["socketshpp"]
   }
   ```

3. **Update CMakeLists.txt**:
   ```cmake
   find_package(SocketsHpp CONFIG REQUIRED)
   target_link_libraries(your-target PRIVATE SocketsHpp::SocketsHpp)
   ```

4. **Build with vcpkg toolchain**:
   ```bash
   cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake --overlay-ports=./ports
   ```

## Notes

- SocketsHpp is **header-only**, so no runtime libraries are required
- All dependencies are automatically handled by vcpkg
- The overlay port allows using the latest development version
- For production, wait for official vcpkg registry integration

## See Also

- [vcpkg Documentation](https://vcpkg.io/)
- [SocketsHpp GitHub](https://github.com/maxgolov/SocketsHpp)
- [vcpkg Manifest Mode](https://learn.microsoft.com/en-us/vcpkg/users/manifests)
