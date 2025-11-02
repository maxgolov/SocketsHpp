cmake_minimum_required(VERSION 3.14)
project(tcp-echo-example)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the SocketsHpp include directory
# Adjust this path based on where you installed SocketsHpp
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)

# Create the executable
add_executable(tcp-echo main.cpp)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(tcp-echo ws2_32)
endif()
