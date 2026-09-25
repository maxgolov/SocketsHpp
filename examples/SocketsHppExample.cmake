# Shared setup for the examples. Each example can be built either from the
# top-level project (-DBUILD_EXAMPLES=ON) or on its own:
#   cmake -S examples/03-http-server -B build-example && cmake --build build-example
# In the standalone case the SocketsHpp project is pulled in with add_subdirectory
# so the example gets the same SocketsHpp::SocketsHpp target (include paths,
# bundled BS::thread_pool, threads, ws2_32 on Windows) as any other consumer.
if(NOT TARGET SocketsHpp::SocketsHpp)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." "${CMAKE_BINARY_DIR}/socketshpp" EXCLUDE_FROM_ALL)
endif()

# Link an example executable against SocketsHpp (and nlohmann-json, which the
# MCP headers pulled in by <sockets.hpp> need).
function(socketshpp_example target)
    target_link_libraries(${target} PRIVATE SocketsHpp::SocketsHpp)
    if(TARGET nlohmann_json::nlohmann_json)
        target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json)
    endif()
endfunction()
