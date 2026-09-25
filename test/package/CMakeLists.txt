# Consumer smoke test for the installed CMake package:
#   cmake --install build --prefix <prefix>
#   cmake -S test/package -B build-package -DCMAKE_PREFIX_PATH=<prefix>
#   cmake --build build-package && ctest --test-dir build-package
cmake_minimum_required(VERSION 3.14)
project(socketshpp_package_test CXX)

find_package(SocketsHpp 1.0 CONFIG REQUIRED)

add_executable(package_consumer main.cpp)
target_link_libraries(package_consumer PRIVATE SocketsHpp::SocketsHpp)

enable_testing()
add_test(NAME package_consumer COMMAND package_consumer)
