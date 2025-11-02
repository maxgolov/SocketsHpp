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

find_package(GTest)
if (GTest_FOUND)
  add_subdirectory(test)
endif()

# Examples are standalone - build them separately from their own directories
