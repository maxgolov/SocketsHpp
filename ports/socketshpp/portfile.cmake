# Used as an overlay port from a SocketsHpp checkout (vcpkg --overlay-ports or
# VCPKG_OVERLAY_PORTS=<repo>/ports), build the local source tree. When publishing
# the port to a registry, pin REF to a release tag and fill in its SHA512.
set(SOCKETSHPP_LOCAL_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../..")
if(EXISTS "${SOCKETSHPP_LOCAL_SOURCE}/include/sockets.hpp")
    set(SOURCE_PATH "${SOCKETSHPP_LOCAL_SOURCE}")
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO maxgolov/SocketsHpp
        REF main
        SHA512 0
        HEAD_REF main
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTING=OFF
        -DBUILD_EXAMPLES=OFF
        -DSOCKETSHPP_BUILD_TESTS=OFF
        # BS_thread_pool.hpp comes from the bshoshany-thread-pool port
        -DSOCKETSHPP_INSTALL_BUNDLED_THREAD_POOL=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/SocketsHpp)

# Header-only library - remove empty lib directories
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug" "${CURRENT_PACKAGES_DIR}/lib")

# Install license
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

# Copy usage file
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
