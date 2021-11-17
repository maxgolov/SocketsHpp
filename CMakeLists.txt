cmake_minimum_required (VERSION 3.8.0)

project ("SocketsHpp")

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

add_subdirectory(samples)
