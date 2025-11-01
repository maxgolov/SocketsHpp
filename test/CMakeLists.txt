find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

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
)

foreach(testname ${UNIT_TESTS})
  add_executable(${testname} "unit/${testname}.cc")
  target_link_libraries(${testname} ${GTEST_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  target_include_directories(${testname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_add_tests(
    TARGET ${testname}
    TEST_PREFIX unit.
    TEST_LIST ${testname})
endforeach()

# Functional tests - test end-to-end scenarios and integration
set(FUNCTIONAL_TESTS
  sockets_test
  sockets_udp_test
  http_server_test
)

foreach(testname ${FUNCTIONAL_TESTS})
  add_executable(${testname} "functional/${testname}.cc" "functional/utils.h")
  target_link_libraries(${testname} ${GTEST_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  target_include_directories(${testname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_add_tests(
    TARGET ${testname}
    TEST_PREFIX functional.
    TEST_LIST ${testname})
endforeach()
