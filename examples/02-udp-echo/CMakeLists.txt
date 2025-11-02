cmake_minimum_required(VERSION 3.14)
project(udp-echo-example)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_executable(udp-echo main.cpp)

if(WIN32)
    target_link_libraries(udp-echo ws2_32)
endif()
