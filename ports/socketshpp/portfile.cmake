# Builds SocketsHpp from the public GitHub repository at a pinned commit, so the
# port behaves the same from an overlay directory, a copy of it, or the git
# registry in this repository (see ports/socketshpp/README.md).
# To release: point REF at the new tag or commit and update SHA512 (vcpkg prints
# the expected value when it does not match).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO maxgolov/SocketsHpp
    REF 614606f5290965e6372e33ce783562ea55e986de
    SHA512 b12beb4dd8f79b9c356a5e0bdf312b818da6f9a3d5e7e928008e7bea7d9bef80d3bca46fb5b7c4f8e097df975f86404c6189143d94e4065814ae4c9ff42e9d21
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
