cmake_minimum_required (VERSION 3.8.0)

project ("SocketsHpp" VERSION 1.0.0)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable strict compiler warnings
if(MSVC)
    add_compile_options(/W4)
    # Disable specific warnings that are too noisy for this codebase
    add_compile_options(/wd4267)  # conversion from size_t to int (WinSock vs POSIX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Disable specific warnings
    add_compile_options(-Wno-unused-parameter)
endif()

# Optional: Enable sanitizers for debug builds
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(BUILD_EXAMPLES "Build example programs" OFF)

if(ENABLE_ASAN AND NOT MSVC)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    message(STATUS "Address Sanitizer enabled")
endif()

if(ENABLE_UBSAN AND NOT MSVC)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
    message(STATUS "Undefined Behavior Sanitizer enabled")
endif()

# Import to detect the platform target architecture
include(cmake/target-arch.cmake)

# Autodetect dependencies from vcpkg if available
include(cmake/detect-vcpkg.cmake)

include(CTest)

include_directories(include)
include_directories(external/simple-uri-parser)

# Try to find nlohmann-json from vcpkg first
find_package(nlohmann_json CONFIG QUIET)

if(NOT TARGET nlohmann_json::nlohmann_json)
    # Fallback to git submodule if vcpkg not available
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann-json/single_include/nlohmann/json.hpp")
        message(STATUS "Using nlohmann-json from git submodule")
        include_directories(external/nlohmann-json/single_include)
        add_library(nlohmann_json INTERFACE)
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    else()
        message(WARNING "nlohmann-json not found via vcpkg or submodule - MCP tests will be skipped")
    endif()
else()
    message(STATUS "Using nlohmann-json from vcpkg")
endif()

# Find BS thread pool header
find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp")
if(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS)
    include_directories(${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS})
    message(STATUS "Found BS thread pool: ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}")
endif()

find_package(GTest)
if (GTest_FOUND)
  add_subdirectory(test)
endif()

# Build examples if requested
if(BUILD_EXAMPLES)
    message(STATUS "Building examples")
    
    # Set SOCKETSHPP_INCLUDE_DIR for examples that use find_path
    set(SOCKETSHPP_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
    
    add_subdirectory(examples/01-tcp-echo)
    add_subdirectory(examples/02-udp-echo)
    add_subdirectory(examples/03-http-server)
    add_subdirectory(examples/04-http-sse)
    add_subdirectory(examples/05-mcp-server)
    add_subdirectory(examples/06-proxy-aware)
    add_subdirectory(examples/07-authentication)
    add_subdirectory(examples/08-compression)
    add_subdirectory(examples/09-full-featured)
    add_subdirectory(examples/10-typescript-interop)
    
    # Example 11 is special - it demonstrates vcpkg consumption using find_package()
    # It's designed to be built standalone with: ./setup-windows.ps1 or ./setup-linux.sh
    # Skip it in the main build to avoid vcpkg toolchain conflicts
    # add_subdirectory(examples/11-vcpkg-consumption)
endif()

# Examples are standalone - build them separately from their own directories
