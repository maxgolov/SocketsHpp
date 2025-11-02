# Integration Guide: Adding SocketsHpp to Your C++ Project

This guide provides detailed instructions for integrating SocketsHpp into existing C++ projects using four different approaches, ranked by complexity and ecosystem integration.

## Table of Contents

1. [Quick Decision Matrix](#quick-decision-matrix)
2. [Option 1: vcpkg Overlay Ports](#option-1-vcpkg-overlay-ports)
3. [Option 2: Git Submodules](#option-2-git-submodules)
4. [Option 3: CMake FetchContent](#option-3-cmake-fetchcontent)
5. [Option 4: Manual Installation](#option-4-manual-installation)
6. [Comparison and Selection Guide](#comparison-and-selection-guide)
7. [Common Issues and Solutions](#common-issues-and-solutions)

## Quick Decision Matrix

| Use Case | Recommended Approach |
|----------|---------------------|
| Already using vcpkg | **Option 1: Overlay Ports** |
| Need reproducible builds across history | **Option 2: Git Submodules** |
| Want CMake-native solution | **Option 3: FetchContent** |
| Simple project, minimal tooling | **Option 4: Manual** |
| CI/CD with binary caching | **Option 1: Overlay Ports** |
| Modifying library frequently | **Option 4: Manual** or **Option 2** |
| Multi-repository development | **Option 2: Git Submodules** |

## Option 1: vcpkg Overlay Ports

**Best for:** Projects already using vcpkg, teams wanting sophisticated dependency management

### Advantages
✅ Seamless integration with vcpkg ecosystem  
✅ Binary caching support  
✅ Automatic transitive dependency resolution  
✅ Version management  
✅ Best for CI/CD pipelines  

### Step-by-Step Setup

#### 1. Create Overlay Port Directory Structure

```bash
mkdir -p my-vcpkg-overlay/ports/socketshpp
cd my-vcpkg-overlay/ports/socketshpp
```

#### 2. Create vcpkg.json Manifest

Create `my-vcpkg-overlay/ports/socketshpp/vcpkg.json`:

```json
{
  "name": "socketshpp",
  "version": "1.0.0",
  "description": "Modern C++17 header-only networking library",
  "homepage": "https://github.com/maxgolov/SocketsHpp",
  "license": "Apache-2.0",
  "supports": "!(uwp | emscripten)",
  "dependencies": [
    {
      "name": "nlohmann-json",
      "version>=": "3.11.0"
    },
    {
      "name": "bshoshany-thread-pool",
      "version>=": "4.0.0"
    },
    {
      "name": "vcpkg-cmake",
      "host": true
    }
  ]
}
```

#### 3. Create Portfile

Create `my-vcpkg-overlay/ports/socketshpp/portfile.cmake`:

```cmake
# Download from GitHub
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO maxgolov/SocketsHpp
    REF main  # Or specific version tag like "v1.0.0"
    SHA512 0  # vcpkg will report correct hash on first run
    HEAD_REF main
)

# Header-only library - just copy headers
file(INSTALL "${SOURCE_PATH}/include/" 
     DESTINATION "${CURRENT_PACKAGES_DIR}/include"
     FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

# Install external dependencies bundled as submodules
if(EXISTS "${SOURCE_PATH}/external/simple-uri-parser")
    file(INSTALL "${SOURCE_PATH}/external/simple-uri-parser/" 
         DESTINATION "${CURRENT_PACKAGES_DIR}/include/simple-uri-parser"
         FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
endif()

# Install copyright/license
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

# Create usage file
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" [[
SocketsHpp provides a header-only networking library.

Usage in CMakeLists.txt:

    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(bshoshany-thread-pool CONFIG REQUIRED)
    
    target_include_directories(myapp PRIVATE ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include)
    target_link_libraries(myapp PRIVATE nlohmann_json::nlohmann_json)
    target_compile_features(myapp PRIVATE cxx_std_17)

Then include in your code:

    #include <sockets.hpp>
]])
```

#### 4. Configure Project to Use Overlay

Create `vcpkg-configuration.json` in your project root:

```json
{
  "default-registry": {
    "kind": "git",
    "repository": "https://github.com/microsoft/vcpkg",
    "baseline": "3426db05b996481ca31e95fff3734cf23e0f51bc"
  },
  "overlay-ports": [
    "${env:VCPKG_ROOT}/../my-vcpkg-overlay/ports"
  ]
}
```

Update your project's `vcpkg.json`:

```json
{
  "name": "my-application",
  "version": "1.0.0",
  "dependencies": [
    "socketshpp"
  ],
  "builtin-baseline": "3426db05b996481ca31e95fff3734cf23e0f51bc"
}
```

#### 5. CMake Integration

```cmake
cmake_minimum_required(VERSION 3.21)

# Set vcpkg toolchain BEFORE project() call
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "vcpkg toolchain file")

project(MyApplication CXX)

# vcpkg automatically installs dependencies
find_package(nlohmann_json CONFIG REQUIRED)
find_package(bshoshany-thread-pool CONFIG REQUIRED)

add_executable(myapp src/main.cpp)

target_include_directories(myapp PRIVATE 
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include)

target_link_libraries(myapp PRIVATE 
    nlohmann_json::nlohmann_json
    bshoshany-thread-pool::bshoshany-thread-pool)

target_compile_features(myapp PRIVATE cxx_std_17)
```

#### 6. Build

```bash
# Configure and build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### Updating SHA512 Hash

On first run, vcpkg will fail with:

```
Expected hash: 0
Actual hash: a1b2c3d4e5f6...
```

Copy the actual hash and update `portfile.cmake`:

```cmake
vcpkg_from_github(
    # ...
    SHA512 a1b2c3d4e5f6...  # Paste actual hash here
)
```

---

## Option 2: Git Submodules

**Best for:** Reproducible builds, version control integration, multi-repository workflows

### Advantages
✅ Exact version tracking in repository history  
✅ Works offline after initial clone  
✅ No external package manager required  
✅ Developers can work on dependencies locally  
✅ Perfect reproducibility across historical commits  

### Step-by-Step Setup

#### 1. Add Submodules

```bash
# Navigate to project root
cd /path/to/your/project

# Create external directory
mkdir -p external

# Add SocketsHpp
git submodule add https://github.com/maxgolov/SocketsHpp.git external/SocketsHpp

# Add dependencies
git submodule add https://github.com/nlohmann/json.git external/nlohmann-json
git submodule add https://github.com/bshoshany/thread-pool.git external/thread-pool

# Commit submodule configuration
git add .gitmodules external/
git commit -m "Add SocketsHpp and dependencies as submodules"
```

#### 2. Pin Specific Versions (Optional but Recommended)

```bash
# Enter each submodule and checkout specific tag
cd external/nlohmann-json
git checkout v3.11.3
cd ../thread-pool
git checkout v4.1.0
cd ../SocketsHpp
git checkout v1.0.0
cd ../..

# Commit pinned versions
git add external/
git commit -m "Pin dependency versions"
```

#### 3. CMake Integration

Create or update your `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyApplication CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add nlohmann-json (has CMakeLists.txt)
option(JSON_BuildTests "Build nlohmann_json tests" OFF)
add_subdirectory(external/nlohmann-json)

# Add thread-pool (check if it has CMakeLists.txt)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/thread-pool/CMakeLists.txt")
    add_subdirectory(external/thread-pool)
else()
    # Create interface library manually
    add_library(bshoshany-thread-pool INTERFACE)
    target_include_directories(bshoshany-thread-pool INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/external/thread-pool/include)
endif()

# Add SocketsHpp (header-only)
add_library(socketshpp INTERFACE)
add_library(socketshpp::socketshpp ALIAS socketshpp)

target_include_directories(socketshpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/SocketsHpp/include>
    $<INSTALL_INTERFACE:include>)

# Include bundled submodules (simple-uri-parser)
target_include_directories(socketshpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/SocketsHpp/external/simple-uri-parser>)

target_compile_features(socketshpp INTERFACE cxx_std_17)

target_link_libraries(socketshpp INTERFACE 
    nlohmann_json::nlohmann_json
    bshoshany-thread-pool)

# Your application
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE socketshpp::socketshpp)
```

#### 4. Initialize Submodules When Cloning

For new clones:

```bash
# Clone with submodules
git clone --recursive https://github.com/youruser/yourproject.git

# Or if already cloned
git submodule update --init --recursive
```

#### 5. Updating Submodules

```bash
# Update all submodules to latest
git submodule update --remote

# Or update specific submodule
cd external/SocketsHpp
git pull origin main
cd ../..
git add external/SocketsHpp
git commit -m "Update SocketsHpp to latest"
```

### Best Practices for Submodules

1. **Always use `--recursive` when cloning**
2. **Pin to specific tags/commits** for stability
3. **Document submodule update procedures** in README
4. **Use `.gitmodules` to track submodule URLs**
5. **Test after updating submodules** before committing

---

## Option 3: CMake FetchContent

**Best for:** CMake-native projects, minimal external dependencies, flexible configuration

### Advantages
✅ No external package manager  
✅ Clean CMake-only workflow  
✅ Automatic download at configure time  
✅ Easy to switch between versions  
✅ Works with any CMake-compatible library  

### Step-by-Step Setup

#### Basic FetchContent Integration

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyApplication CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Declare all dependencies
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(thread_pool
    GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
    GIT_TAG v4.1.0
    GIT_SHALLOW TRUE
)

FetchContent_Declare(socketshpp
    GIT_REPOSITORY https://github.com/maxgolov/SocketsHpp.git
    GIT_TAG main  # Or specific version tag
    GIT_SHALLOW TRUE
)

# Make dependencies available
FetchContent_MakeAvailable(nlohmann_json thread_pool)

# Manually handle SocketsHpp (header-only)
FetchContent_GetProperties(socketshpp)
if(NOT socketshpp_POPULATED)
    FetchContent_Populate(socketshpp)
    
    add_library(socketshpp INTERFACE)
    add_library(socketshpp::socketshpp ALIAS socketshpp)
    
    target_include_directories(socketshpp INTERFACE
        ${socketshpp_SOURCE_DIR}/include
        ${socketshpp_SOURCE_DIR}/external/simple-uri-parser)
    
    target_compile_features(socketshpp INTERFACE cxx_std_17)
    
    target_link_libraries(socketshpp INTERFACE 
        nlohmann_json::nlohmann_json)
endif()

# Your application
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE socketshpp::socketshpp)
```

#### Advanced: Local Caching for CI

```cmake
# Optimize for CI - use local cache
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps" 
    CACHE PATH "FetchContent base directory")

# Enable verbose output for debugging
set(FETCHCONTENT_QUIET OFF CACHE BOOL "FetchContent verbose")

include(FetchContent)

# Rest of configuration...
```

#### Using with CMake Presets

Create `CMakePresets.json`:

```json
{
  "version": 3,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 24,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "default",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_CXX_STANDARD": "17",
        "FETCHCONTENT_QUIET": "OFF",
        "FETCHCONTENT_BASE_DIR": "${sourceDir}/build/_deps"
      }
    }
  ]
}
```

Build with preset:

```bash
cmake --preset default
cmake --build --preset default
```

### Offline Development

FetchContent downloads on every fresh configure. For offline work:

1. **Option A: Preserve build directory**
   - Keep `build/_deps` directory across rebuilds

2. **Option B: Use local mirror**
```cmake
FetchContent_Declare(socketshpp
    GIT_REPOSITORY file:///local/mirror/SocketsHpp
    GIT_TAG main
)
```

---

## Option 4: Manual Installation

**Best for:** Simple projects, frequent library modifications, minimal tooling

### Advantages
✅ Simplest setup  
✅ No external tools required  
✅ Easy to modify library source  
✅ Fast iteration during development  

### Disadvantages
❌ No automatic updates  
❌ Manual dependency management  
❌ Reproducibility challenges  
❌ Tedious for complex projects  

### Step-by-Step Setup

#### 1. Download and Copy Headers

```bash
# Clone SocketsHpp
git clone https://github.com/maxgolov/SocketsHpp.git /tmp/SocketsHpp

# Create project structure
mkdir -p /path/to/your/project/external/include

# Copy SocketsHpp headers
cp -r /tmp/SocketsHpp/include/SocketsHpp /path/to/your/project/external/include/
cp /tmp/SocketsHpp/include/sockets.hpp /path/to/your/project/external/include/

# Copy bundled dependencies (simple-uri-parser)
cp -r /tmp/SocketsHpp/external/simple-uri-parser /path/to/your/project/external/include/

# Download and copy nlohmann-json
git clone https://github.com/nlohmann/json.git /tmp/nlohmann-json
cp -r /tmp/nlohmann-json/include/nlohmann /path/to/your/project/external/include/

# Download and copy thread-pool (if using multi-threading)
git clone https://github.com/bshoshany/thread-pool.git /tmp/thread-pool
cp -r /tmp/thread-pool/include/BS_thread_pool.hpp /path/to/your/project/external/include/
```

#### 2. CMake Integration (Modern)

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyApplication CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create interface libraries for better abstraction
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/include)

add_library(socketshpp INTERFACE)
add_library(socketshpp::socketshpp ALIAS socketshpp)

target_include_directories(socketshpp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/include)

target_compile_features(socketshpp INTERFACE cxx_std_17)

target_link_libraries(socketshpp INTERFACE nlohmann_json)

# Your application
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE socketshpp::socketshpp)
```

#### 3. Simple CMake (Legacy Style)

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyApplication CXX)

set(CMAKE_CXX_STANDARD 17)

# Add include directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/include)

# Your application
add_executable(myapp src/main.cpp)
```

#### 4. Non-CMake Build Systems

**Makefile:**
```makefile
CXX = g++
CXXFLAGS = -std=c++17 -I./external/include
LDFLAGS = 

myapp: src/main.cpp
	$(CXX) $(CXXFLAGS) -o myapp src/main.cpp $(LDFLAGS)
```

**Visual Studio:**
- Project Properties → C/C++ → General → Additional Include Directories
- Add: `$(ProjectDir)external\include`

**Xcode:**
- Build Settings → Search Paths → Header Search Paths
- Add: `external/include`

### Updating Dependencies

```bash
# Re-download and copy new version
cd /tmp
git clone --depth 1 https://github.com/maxgolov/SocketsHpp.git SocketsHpp-new
cp -r SocketsHpp-new/include/SocketsHpp /path/to/your/project/external/include/
rm -rf SocketsHpp-new
```

---

## Comparison and Selection Guide

### Feature Matrix

| Feature | Overlay Ports | Git Submodules | FetchContent | Manual |
|---------|--------------|----------------|--------------|---------|
| Binary caching | ✅ | ❌ | ❌ | ❌ |
| Offline after setup | ✅ | ✅ | ❌ | ✅ |
| Version tracking | ✅ | ✅ | ⚠️ | ❌ |
| Transitive deps | ✅ | ⚠️ | ⚠️ | ❌ |
| CI/CD friendly | ✅ | ✅ | ⚠️ | ⚠️ |
| Easy updates | ✅ | ⚠️ | ✅ | ❌ |
| Learning curve | High | Medium | Low | Minimal |
| Setup complexity | High | Medium | Low | Minimal |
| Build time (first) | Fast* | Fast | Slow | Fast |
| Build time (rebuild) | Fast* | Fast | Slow | Fast |

*With binary cache enabled

### Decision Tree

```
Start here: Do you use vcpkg?
├─ YES → Use Overlay Ports
│         Already familiar with ecosystem
│         Best integration
│
└─ NO → Do you need reproducible builds across history?
    ├─ YES → Use Git Submodules
    │         Perfect for long-term projects
    │         Git-native solution
    │
    └─ NO → Is your project CMake-based?
        ├─ YES → Use FetchContent
        │         Clean CMake workflow
        │         No external tools
        │
        └─ NO → Use Manual Installation
                  Simplest approach
                  Works with any build system
```

### Team Size Recommendations

| Team Size | Recommended | Why |
|-----------|-------------|-----|
| Solo developer | Manual or FetchContent | Simplicity |
| 2-5 developers | Git Submodules | Version sync |
| 6-20 developers | Overlay Ports or Submodules | Consistency |
| 20+ developers | Overlay Ports | Binary cache benefits |

---

## Common Issues and Solutions

### Issue: vcpkg Can't Find Overlay Port

**Symptom:**
```
error: package 'socketshpp' not found
```

**Solution:**
1. Check `vcpkg-configuration.json` exists in project root
2. Verify overlay path is correct (use absolute path if needed)
3. Ensure port name matches exactly (case-sensitive)

```json
{
  "overlay-ports": [
    "C:/absolute/path/to/my-vcpkg-overlay/ports"
  ]
}
```

### Issue: Git Submodule Not Initialized

**Symptom:**
```
fatal: Not a git repository
```

**Solution:**
```bash
git submodule update --init --recursive
```

Add to project README:
```markdown
## Building

After cloning, initialize submodules:
\`\`\`bash
git submodule update --init --recursive
\`\`\`
```

### Issue: FetchContent Slow Downloads

**Symptom:** Configure takes minutes on clean builds

**Solution 1:** Use shallow clones
```cmake
FetchContent_Declare(socketshpp
    GIT_REPOSITORY https://github.com/maxgolov/SocketsHpp.git
    GIT_TAG main
    GIT_SHALLOW TRUE  # Only download latest commit
)
```

**Solution 2:** Cache _deps directory in CI

```yaml
# GitHub Actions
- uses: actions/cache@v3
  with:
    path: build/_deps
    key: ${{ runner.os }}-cmake-deps-${{ hashFiles('CMakeLists.txt') }}
```

### Issue: Multiple nlohmann-json Versions

**Symptom:**
```
error: conflicting declaration of nlohmann_json
```

**Solution:** Ensure only one version is included

**For FetchContent:**
```cmake
# Declare at top level only
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(nlohmann_json ...)
    FetchContent_MakeAvailable(nlohmann_json)
endif()
```

**For Submodules:**
```cmake
# Use single submodule for shared dependency
add_subdirectory(external/nlohmann-json)
# All projects use this instance
```

### Issue: Header Not Found

**Symptom:**
```cpp
#include <sockets.hpp>  // error: file not found
```

**Solution:** Check include directories

```cmake
# Add diagnostic
message(STATUS "Include dirs: ${CMAKE_INCLUDE_PATH}")
message(STATUS "SocketsHpp dir: ${socketshpp_SOURCE_DIR}")

# Verify correct path
target_include_directories(myapp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/SocketsHpp/include)
```

### Issue: C++17 Not Enabled

**Symptom:**
```
error: 'if constexpr' requires C++17
```

**Solution:** Set C++ standard
```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Or per-target
target_compile_features(myapp PRIVATE cxx_std_17)
```

### Issue: Thread Pool Not Found (Windows)

**Symptom:**
```cpp
#include <BS_thread_pool.hpp>  // error: file not found
```

**Solution:** Install via vcpkg or manually copy

**vcpkg:**
```bash
vcpkg install bshoshany-thread-pool
```

**Manual:**
```bash
# Download single header
curl -o external/include/BS_thread_pool.hpp \
  https://raw.githubusercontent.com/bshoshany/thread-pool/master/include/BS_thread_pool.hpp
```

---

## Testing Your Integration

After setup, verify with this test program:

```cpp
// test.cpp
#include <sockets.hpp>
#include <iostream>

using namespace SocketsHpp;

int main() {
    std::cout << "SocketsHpp integration test\n";
    
    // Test socket creation
    try {
        net::utils::Socket sock(AF_INET, SOCK_STREAM, 0);
        std::cout << "✓ Socket created successfully\n";
        sock.close();
    } catch (const std::exception& e) {
        std::cerr << "✗ Socket creation failed: " << e.what() << "\n";
        return 1;
    }
    
    // Test HTTP server creation
    try {
        http::server::HttpServer server("127.0.0.1", 8080);
        std::cout << "✓ HTTP server created successfully\n";
    } catch (const std::exception& e) {
        std::cerr << "✗ HTTP server creation failed: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "\n✅ All tests passed!\n";
    return 0;
}
```

Build and run:
```bash
# CMake
cmake -B build -S .
cmake --build build
./build/test

# g++ (manual)
g++ -std=c++17 -I./external/include test.cpp -o test
./test
```

---

## Next Steps

1. Review [examples/](../examples/) for usage patterns
2. Read [FEATURES.md](FEATURES.md) for complete API reference
3. Check [MCP_IMPLEMENTATION.md](MCP_IMPLEMENTATION.md) for MCP protocol details
4. See [BUILD_SUMMARY.md](BUILD_SUMMARY.md) for advanced build configurations

## Support

- **Issues:** [GitHub Issues](https://github.com/maxgolov/SocketsHpp/issues)
- **Discussions:** [GitHub Discussions](https://github.com/maxgolov/SocketsHpp/discussions)
- **Documentation:** [docs/](../docs/)
