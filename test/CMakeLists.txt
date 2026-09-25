find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# Try to find optional dependencies for MCP tests
find_package(nlohmann_json CONFIG QUIET)

# Every test target gets the library's usage requirements (include paths,
# threads, ws2_32 on Windows).
link_libraries(SocketsHpp::SocketsHpp)

add_definitions(-DSOCKET_SERVER_NS=SocketsHpp)

# Handle different GTest installations (vcpkg vs apt)
if(TARGET GTest::gmock)
    set(GTEST_LIBRARIES PRIVATE GTest::gmock GTest::gtest GTest::gmock_main GTest::gtest_main)
else()
    set(GTEST_LIBRARIES PRIVATE GTest::gtest GTest::gtest_main)
endif()

# Unit tests - test individual components in isolation
set(UNIT_TESTS
  url_parser_test
  url_security_test
  socket_addr_test
  socket_basic_test
  base64_test
  proxy_aware_test
  authentication_test
  compression_test
  sse_parser_test
  thread_pool_server_test
  http_server_hardening_test
)

# MCP unit tests (require nlohmann-json)
if(TARGET nlohmann_json::nlohmann_json)
  list(APPEND UNIT_TESTS json_rpc_test json_rpc_minimal_test mcp_config_test)
endif()

foreach(testname ${UNIT_TESTS})
  add_executable(${testname} "unit/${testname}.cc")
  target_link_libraries(${testname} ${GTEST_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  
  # Link nlohmann-json for MCP tests
  if(testname MATCHES "json_rpc_test|json_rpc_minimal_test|mcp_config_test")
    target_link_libraries(${testname} PRIVATE nlohmann_json::nlohmann_json)
  endif()
  
  # Link cabinet library for Windows compression tests
  if(WIN32 AND testname STREQUAL "compression_test")
    target_link_libraries(${testname} PRIVATE cabinet)
  endif()
  
  target_include_directories(${testname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_discover_tests(${testname} TEST_PREFIX unit.)
endforeach()

# Functional tests - test end-to-end scenarios and integration
set(FUNCTIONAL_TESTS
  sockets_test
  sockets_udp_test
  socket_server_test
  tcp_server_test
  http_server_test
  http_streaming_test
  http_methods_test
)

foreach(testname ${FUNCTIONAL_TESTS})
  add_executable(${testname} "functional/${testname}.cc")
  target_link_libraries(${testname} ${GTEST_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  
  target_include_directories(${testname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_discover_tests(${testname} TEST_PREFIX functional.)
endforeach()

# Streamable HTTP functional tests (MCP 2025-03-26) — require nlohmann-json.
# This test drives the server over raw POSIX sockets (arpa/inet.h, sys/socket.h,
# unistd.h), so it is POSIX-only and excluded from the Windows build.
if(NOT WIN32 AND TARGET nlohmann_json::nlohmann_json)
  add_executable(mcp_streamable_test "functional/mcp_streamable_test.cc")
  target_link_libraries(mcp_streamable_test
    ${GTEST_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    nlohmann_json::nlohmann_json)
  target_include_directories(mcp_streamable_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
  gtest_discover_tests(mcp_streamable_test TEST_PREFIX functional.)
endif()

# HttpClient / SSEClient functional tests. They drive the client against a scripted
# raw POSIX-socket server (arpa/inet.h, sys/socket.h, unistd.h), so they are
# POSIX-only and excluded from the Windows build.
if(NOT WIN32)
  add_executable(http_client_test "functional/http_client_test.cc")
  target_link_libraries(http_client_test ${GTEST_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  target_include_directories(http_client_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
  gtest_discover_tests(http_client_test TEST_PREFIX functional.)
endif()

# Header self-containment check: every public header must compile on its own
# (and twice, to verify include guards). One generated translation unit per
# header is compiled into a static library; nothing is executed.
file(GLOB_RECURSE SOCKETSHPP_PUBLIC_HEADERS
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/../include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/*.hpp")
set(SOCKETSHPP_HEADER_CHECK_SOURCES)
foreach(header ${SOCKETSHPP_PUBLIC_HEADERS})
  # MCP headers depend on nlohmann-json; the Windows compression backend is
  # Windows-only.
  if(header MATCHES "^SocketsHpp/mcp/|^sockets\\.hpp$" AND NOT TARGET nlohmann_json::nlohmann_json)
    continue()
  endif()
  if(header MATCHES "compression_windows\\.h$" AND NOT WIN32)
    continue()
  endif()
  string(MAKE_C_IDENTIFIER "${header}" header_id)
  set(check_source "${CMAKE_CURRENT_BINARY_DIR}/header_check/${header_id}.cc")
  file(WRITE "${check_source}.in"
    "#include <${header}>\n#include <${header}>\n")
  configure_file("${check_source}.in" "${check_source}" COPYONLY)
  list(APPEND SOCKETSHPP_HEADER_CHECK_SOURCES "${check_source}")
endforeach()
add_library(header_self_contained_check STATIC ${SOCKETSHPP_HEADER_CHECK_SOURCES})
target_include_directories(header_self_contained_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
if(TARGET nlohmann_json::nlohmann_json)
  target_link_libraries(header_self_contained_check PRIVATE nlohmann_json::nlohmann_json)
endif()
if(WIN32)
  target_link_libraries(header_self_contained_check PRIVATE cabinet)
endif()

# HttpServer robustness tests (request smuggling, pipelining, backpressure,
# error responses). Drives the server over raw POSIX sockets, so it is
# POSIX-only and excluded from the Windows build.
if(NOT WIN32)
  add_executable(http_server_robustness_test "functional/http_server_robustness_test.cc")
  target_link_libraries(http_server_robustness_test ${GTEST_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  target_include_directories(http_server_robustness_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_discover_tests(http_server_robustness_test TEST_PREFIX functional.)
endif()

# Header-only guarantee: two translation units that each include every public
# header must link into one executable (catches non-inline definitions, which
# would be multiply defined).
set(SOCKETSHPP_ALL_HEADERS_INCLUDES "")
foreach(header ${SOCKETSHPP_PUBLIC_HEADERS})
  if(header MATCHES "^SocketsHpp/mcp/|^sockets\\.hpp$" AND NOT TARGET nlohmann_json::nlohmann_json)
    continue()
  endif()
  if(header MATCHES "compression_windows\\.h$" AND NOT WIN32)
    continue()
  endif()
  string(APPEND SOCKETSHPP_ALL_HEADERS_INCLUDES "#include <${header}>\n")
endforeach()
set(_odr_dir "${CMAKE_CURRENT_BINARY_DIR}/header_only_check")
foreach(tu 1 2)
  file(WRITE "${_odr_dir}/tu${tu}.cc.in" "${SOCKETSHPP_ALL_HEADERS_INCLUDES}int socketshpp_tu${tu}() { return 0; }\n")
  configure_file("${_odr_dir}/tu${tu}.cc.in" "${_odr_dir}/tu${tu}.cc" COPYONLY)
endforeach()
file(WRITE "${_odr_dir}/main.cc.in"
  "int socketshpp_tu1();\nint socketshpp_tu2();\nint main() { return socketshpp_tu1() + socketshpp_tu2(); }\n")
configure_file("${_odr_dir}/main.cc.in" "${_odr_dir}/main.cc" COPYONLY)
add_executable(header_only_link_check "${_odr_dir}/tu1.cc" "${_odr_dir}/tu2.cc" "${_odr_dir}/main.cc")
if(TARGET nlohmann_json::nlohmann_json)
  target_link_libraries(header_only_link_check PRIVATE nlohmann_json::nlohmann_json)
endif()
if(WIN32)
  target_link_libraries(header_only_link_check PRIVATE cabinet)
endif()
add_test(NAME header_only_link_check COMMAND header_only_link_check)
