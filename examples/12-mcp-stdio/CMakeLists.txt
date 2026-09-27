cmake_minimum_required(VERSION 3.14)
project(mcp-stdio-example CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Provides SocketsHpp::SocketsHpp (also when this example is built on its own)
include(${CMAKE_CURRENT_LIST_DIR}/../SocketsHppExample.cmake)

# MCP server on stdin/stdout
add_executable(mcp-stdio-server server.cpp)
socketshpp_example(mcp-stdio-server)

# MCP client that launches mcp-stdio-server as a child process
add_executable(mcp-stdio-client client.cpp)
socketshpp_example(mcp-stdio-client)
add_dependencies(mcp-stdio-client mcp-stdio-server)
