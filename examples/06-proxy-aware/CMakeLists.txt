cmake_minimum_required(VERSION 3.20)
project(proxy-aware-example)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SocketsHpp
find_path(SOCKETSHPP_INCLUDE_DIR sockets.hpp PATHS ${CMAKE_SOURCE_DIR}/../../include)
include_directories(${SOCKETSHPP_INCLUDE_DIR})

# Create executable
add_executable(proxy-aware-server main.cpp)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(proxy-aware-server ws2_32)
endif()
