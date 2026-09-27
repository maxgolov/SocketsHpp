# Builds SocketsHpp from the public GitHub repository at a pinned commit, so the
# port behaves the same from an overlay directory, a copy of it, or the git
# registry in this repository (see ports/socketshpp/README.md).
# To release: point REF at the new tag or commit and update SHA512 (vcpkg prints
# the expected value when it does not match).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO maxgolov/SocketsHpp
    REF 99fd57c6b020f005c506fa0f8ebaacdcccf31a84 # v1.1.0
    SHA512 fd0398ac4cec3b38fa07bdf17adee3e63dd383bc7f05497da36cef566eb79e50b9fac705578a02f0ccd5cc2b8627413039a4640eea7aa0dd3f8332500b904610
    HEAD_REF main
)

# The library finds jwt-cpp on its own; tie that to the "jwt" feature so the package
# never picks up a jwt-cpp that happens to be installed without the feature.
vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    INVERTED_FEATURES
        jwt CMAKE_DISABLE_FIND_PACKAGE_jwt-cpp
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DBUILD_TESTING=OFF
        -DSOCKETSHPP_BUILD_EXAMPLES=OFF
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
