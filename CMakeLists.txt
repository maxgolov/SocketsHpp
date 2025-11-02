cmake_minimum_required (VERSION 3.8.0)

project ("SocketsHpp")

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Examples are standalone - build them separately from their own directories
