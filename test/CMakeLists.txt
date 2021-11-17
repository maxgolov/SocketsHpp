find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_definitions(-DSOCKET_SERVER_NS=SocketsHpp)

set(GTEST_LIBRARIES PRIVATE GTest::gmock GTest::gtest GTest::gmock_main GTest::gtest_main)
foreach(testname sockets_test sockets_udp_test)
  add_executable(${testname} "${testname}.cc" "utils.h")
  target_link_libraries(${testname} ${GTEST_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  target_include_directories(${testname} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_add_tests(
    TARGET ${testname}
    TEST_PREFIX ext.sockets.
    TEST_LIST ${testname})
endforeach()
