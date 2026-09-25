cmake_minimum_required(VERSION 3.14)
project(http-sse-example CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Provides SocketsHpp::SocketsHpp (also when this example is built on its own)
include(${CMAKE_CURRENT_LIST_DIR}/../SocketsHppExample.cmake)

add_executable(http-sse main.cpp)
socketshpp_example(http-sse)
