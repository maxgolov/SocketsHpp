cmake_minimum_required(VERSION 3.14)
project(http-server-example)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_executable(http-server main.cpp)

if(WIN32)
    target_link_libraries(http-server ws2_32)
endif()
