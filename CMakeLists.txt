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
