cmake_minimum_required(VERSION 3.14)
project(TypeScriptInterop CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Provides SocketsHpp::SocketsHpp (also when this example is built on its own)
include(${CMAKE_CURRENT_LIST_DIR}/../SocketsHppExample.cmake)

# C++ MCP Server
add_executable(cpp_server cpp_server.cpp)
socketshpp_example(cpp_server)

# C++ MCP Client
add_executable(cpp_client cpp_client.cpp)
socketshpp_example(cpp_client)
