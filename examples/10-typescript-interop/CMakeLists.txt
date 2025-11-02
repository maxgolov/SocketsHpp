cmake_minimum_required(VERSION 3.21)
project(TypeScriptInterop CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add parent directory to find SocketsHpp headers
include_directories(../../include)
include_directories(../../external/simple-uri-parser)

# Try to find nlohmann_json
find_package(nlohmann_json CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    # Fallback to git submodule if available
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../external/nlohmann-json/single_include/nlohmann/json.hpp")
        message(STATUS "Using nlohmann-json from git submodule")
        include_directories(../../external/nlohmann-json/single_include)
    else()
        message(FATAL_ERROR "nlohmann-json not found. Please install via vcpkg or initialize git submodules.")
    endif()
endif()

# Try to find BS thread pool
find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp")
if(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS)
    include_directories(${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS})
    message(STATUS "Found BS thread pool: ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS}")
else()
    # Try to find in parent build directory
    set(PARENT_VCPKG_INCLUDE "../../build/windows-x64/vcpkg_installed/x64-windows/include")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${PARENT_VCPKG_INCLUDE}/BS_thread_pool.hpp")
        include_directories(${PARENT_VCPKG_INCLUDE})
        message(STATUS "Using BS thread pool from parent build: ${PARENT_VCPKG_INCLUDE}")
    else()
        message(WARNING "BS thread pool not found - multi-threading features will be disabled")
    endif()
endif()

# C++ MCP Server
add_executable(cpp_server cpp_server.cpp)
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(cpp_server PRIVATE nlohmann_json::nlohmann_json)
endif()

# Platform-specific socket libraries
if(WIN32)
    target_link_libraries(cpp_server PRIVATE ws2_32)
else()
    target_link_libraries(cpp_server PRIVATE pthread)
endif()

# C++ MCP Client
add_executable(cpp_client cpp_client.cpp)
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(cpp_client PRIVATE nlohmann_json::nlohmann_json)
endif()

# Platform-specific socket libraries
if(WIN32)
    target_link_libraries(cpp_client PRIVATE ws2_32)
else()
    target_link_libraries(cpp_client PRIVATE pthread)
endif()
