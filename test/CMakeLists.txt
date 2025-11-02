find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# Try to find optional dependencies for MCP tests
find_package(nlohmann_json CONFIG QUIET)
find_package(cpp-jwt CONFIG QUIET)

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
  socket_addr_test
  socket_basic_test
  base64_test
  proxy_aware_test
  authentication_test
  compression_test
)

# MCP unit tests (require nlohmann-json)
if(TARGET nlohmann_json::nlohmann_json)
  list(APPEND UNIT_TESTS json_rpc_test sse_parser_test json_rpc_minimal_test mcp_config_test)
endif()

foreach(testname ${UNIT_TESTS})
  add_executable(${testname} "unit/${testname}.cc")
  target_link_libraries(${testname} ${GTEST_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  
  # Link nlohmann-json for MCP tests
  if(testname MATCHES "json_rpc_test|sse_parser_test|json_rpc_minimal_test|mcp_config_test")
    target_link_libraries(${testname} PRIVATE nlohmann_json::nlohmann_json)
  endif()
  
  # Link Cabinet library for Windows compression tests
  if(WIN32 AND testname STREQUAL "compression_test")
    target_link_libraries(${testname} PRIVATE Cabinet)
  endif()
  
  target_include_directories(${testname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_discover_tests(${testname} TEST_PREFIX unit.)
endforeach()

# Functional tests - test end-to-end scenarios and integration
set(FUNCTIONAL_TESTS
  sockets_test
  sockets_udp_test
  http_server_test
  http_streaming_test
  http_methods_test
)

# MCP functional tests disabled until HttpServer API is implemented
# if(TARGET nlohmann_json::nlohmann_json)
#   list(APPEND FUNCTIONAL_TESTS mcp_integration_test)
# endif()

foreach(testname ${FUNCTIONAL_TESTS})
  add_executable(${testname} "functional/${testname}.cc" "functional/utils.h")
  target_link_libraries(${testname} ${GTEST_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  
  target_include_directories(${testname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_discover_tests(${testname} TEST_PREFIX functional.)
endforeach()
