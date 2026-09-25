cmake_minimum_required (VERSION 3.14)

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

option(SOCKETSHPP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(SOCKETSHPP_WARNINGS_AS_ERRORS)
    if(MSVC)
        add_compile_options(/WX)
    else()
        add_compile_options(-Werror)
    endif()
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

# Tests are opt-in and require GTest. Embedded consumers (add_subdirectory) and
# plain header-only builds are never forced to provide GTest; CI enables this
# explicitly with -DSOCKETSHPP_BUILD_TESTS=ON.
option(SOCKETSHPP_BUILD_TESTS "Build SocketsHpp tests" OFF)
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
        target_include_directories(nlohmann_json INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann-json/single_include>)
    else()
        message(WARNING "nlohmann-json not found via vcpkg or submodule - MCP tests will be skipped")
    endif()
else()
    message(STATUS "Using nlohmann-json from vcpkg")
    # The submodule path above adds a global include directory. Do the same for
    # the vcpkg imported target so that examples and tests which do not link
    # nlohmann_json::nlohmann_json explicitly still resolve <nlohmann/json.hpp>
    # (notably on Windows/MSVC, where there is no global submodule include path).
    get_target_property(_socketshpp_njson_inc
        nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
    if(_socketshpp_njson_inc)
        include_directories(${_socketshpp_njson_inc})
    endif()
endif()

# Find BS thread pool header
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/BS_thread_pool.hpp")
    set(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/external")
else()
    find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp")
endif()
if(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS)
    include_directories(${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS})
    message(STATUS "Found BS thread pool: ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}")
endif()

# ---------------------------------------------------------------------------
# SocketsHpp::SocketsHpp - header-only interface target for consumers.
# Works both in-tree (add_subdirectory) and installed (find_package).
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
find_package(Threads REQUIRED)
add_library(SocketsHpp INTERFACE)
add_library(SocketsHpp::SocketsHpp ALIAS SocketsHpp)
target_compile_features(SocketsHpp INTERFACE cxx_std_17)
target_include_directories(SocketsHpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS)
    target_include_directories(SocketsHpp INTERFACE
        $<BUILD_INTERFACE:${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}>)
endif()
target_link_libraries(SocketsHpp INTERFACE Threads::Threads)
if(WIN32)
    # MSVC also picks these up via #pragma comment(lib); MinGW needs them here.
    target_link_libraries(SocketsHpp INTERFACE ws2_32)
endif()

# Optional JWT validation in the MCP server (jwt-cpp, https://github.com/Thalhammer/jwt-cpp)
find_package(jwt-cpp CONFIG QUIET)
if(TARGET jwt-cpp::jwt-cpp)
    message(STATUS "Using jwt-cpp for MCP JWT validation")
    target_link_libraries(SocketsHpp INTERFACE jwt-cpp::jwt-cpp)
    target_compile_definitions(SocketsHpp INTERFACE SOCKETSHPP_HAS_JWT_CPP)
    set(SOCKETSHPP_WITH_JWT_CPP ON)
else()
    set(SOCKETSHPP_WITH_JWT_CPP OFF)
endif()

# Install rules (headers + CMake package). Enabled by default for top-level builds.
include(CMakePackageConfigHelpers)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_socketshpp_is_top_level ON)
else()
    set(_socketshpp_is_top_level OFF)
endif()
option(SOCKETSHPP_INSTALL "Generate install rules for SocketsHpp" ${_socketshpp_is_top_level})
# The bundled BS_thread_pool.hpp is installed alongside the headers unless the
# consumer provides it (e.g. the vcpkg port depends on bshoshany-thread-pool).
option(SOCKETSHPP_INSTALL_BUNDLED_THREAD_POOL "Install the bundled BS_thread_pool.hpp" ON)

if(SOCKETSHPP_INSTALL)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    if(SOCKETSHPP_INSTALL_BUNDLED_THREAD_POOL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/BS_thread_pool.hpp")
        install(FILES external/BS_thread_pool.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
    install(TARGETS SocketsHpp EXPORT SocketsHppTargets)
    set(SOCKETSHPP_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/SocketsHpp)
    install(EXPORT SocketsHppTargets
        NAMESPACE SocketsHpp::
        DESTINATION ${SOCKETSHPP_CMAKE_DIR})
    configure_package_config_file(
        cmake/SocketsHppConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/SocketsHppConfig.cmake
        INSTALL_DESTINATION ${SOCKETSHPP_CMAKE_DIR})
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/SocketsHppConfigVersion.cmake
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/SocketsHppConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/SocketsHppConfigVersion.cmake
        DESTINATION ${SOCKETSHPP_CMAKE_DIR})
endif()

if(SOCKETSHPP_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
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
