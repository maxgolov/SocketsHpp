cmake_minimum_required(VERSION 3.15)
project(vcpkg-consumption-example CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find SocketsHpp package (installed via vcpkg)
find_package(SocketsHpp CONFIG REQUIRED)

# Create executable
add_executable(vcpkg-consumer main.cpp)

# Link SocketsHpp (header-only, brings in dependencies)
target_link_libraries(vcpkg-consumer PRIVATE SocketsHpp::SocketsHpp)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(vcpkg-consumer PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(vcpkg-consumer PRIVATE ws2_32)
else()
    target_link_libraries(vcpkg-consumer PRIVATE pthread)
endif()

# Installation (optional)
install(TARGETS vcpkg-consumer DESTINATION bin)

# Print configuration info
message(STATUS "===========================================")
message(STATUS "vcpkg Consumption Example Configuration")
message(STATUS "===========================================")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "SocketsHpp Found: ${SocketsHpp_FOUND}")
if(SocketsHpp_FOUND)
    message(STATUS "SocketsHpp Version: ${SocketsHpp_VERSION}")
endif()
message(STATUS "===========================================")
