add_definitions(-DSOCKET_SERVER_NS=SocketsHpp)

add_executable(udp-client udp-client.cpp)
target_include_directories(udp-client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(tcp-client tcp-client.cpp)
target_include_directories(tcp-client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
