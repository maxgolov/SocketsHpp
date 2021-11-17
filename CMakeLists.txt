cmake_minimum_required (VERSION 3.20.0)

project ("SocketsHpp")

# Import to detect the platform target architecture
include(cmake/target-arch.cmake)

# Autodetect dependencies from vcpkg if available
include(cmake/detect-vcpkg.cmake)

include(CTest)

include_directories(include)

add_subdirectory(test)
add_subdirectory(samples)
