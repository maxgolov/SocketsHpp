#!/bin/bash
# Build SocketsHpp for ARM64 architecture using cross-compilation

set -e

# Default values
BUILD_EXAMPLES=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-examples)
            BUILD_EXAMPLES=true
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --build-examples Build example programs"
            echo "  --help           Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "========================================"
echo "ARM64 Cross-Compilation Build"
echo "========================================"
echo ""

cd "$(dirname "$0")/.."

# Initialize git submodules (simple-uri-parser, nlohmann-json)
echo "Initializing git submodules..."
git submodule update --init --recursive

# Install ARM64 cross-compilation tools if needed
if ! dpkg -l | grep -q gcc-aarch64-linux-gnu; then
    echo "Installing ARM64 cross-compilation toolchain..."
    sudo apt-get update
    sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu git wget
fi

# Check for QEMU user-mode emulation
if ! which qemu-aarch64-static >/dev/null 2>&1; then
    echo "Installing QEMU ARM64 emulation..."
    sudo apt-get install -y qemu-user-static binfmt-support
    sudo update-binfmts --enable
    sudo systemctl restart systemd-binfmt
fi

# Install ARM64 runtime libraries for testing
if ! test -f /usr/aarch64-linux-gnu/lib/ld-linux-aarch64.so.1; then
    echo "Installing ARM64 runtime libraries..."
    sudo apt-get install -y libc6:arm64
fi

# Build GTest for ARM64 if needed
GTEST_ARM64_DIR="/tmp/gtest-arm64"
if [ ! -d "$GTEST_ARM64_DIR/lib" ]; then
    echo "Building GTest for ARM64..."
    mkdir -p /tmp/gtest-build-arm64
    cd /tmp/gtest-build-arm64
    
    # Download GTest if needed
    if [ ! -d "googletest" ]; then
        git clone --depth 1 --branch v1.14.0 https://github.com/google/googletest.git
    fi
    
    cd googletest
    mkdir -p build-arm64
    cd build-arm64
    
    cmake -B . -S .. \
        -GNinja \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
        -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
        -DCMAKE_INSTALL_PREFIX=$GTEST_ARM64_DIR \
        -DBUILD_SHARED_LIBS=OFF
    
    cmake --build . -j $(nproc)
    cmake --install .
    
    echo "GTest ARM64 installed to: $GTEST_ARM64_DIR"
fi

# Create ARM64 build directory
cd /mnt/c/build/maxgolov/SocketsHpp
echo "Creating ARM64 build directory..."
rm -rf build/linux-arm64
mkdir -p build/linux-arm64/external/include

# Copy BS thread pool header (header-only library from Windows vcpkg)
echo "Copying BS thread pool header..."
if [ -f build/windows-x64/vcpkg_installed/x64-windows/include/BS_thread_pool.hpp ]; then
    cp build/windows-x64/vcpkg_installed/x64-windows/include/BS_thread_pool.hpp build/linux-arm64/external/include/
    echo "BS thread pool header copied successfully"
else
    echo "Warning: BS thread pool header not found in Windows build, attempting to continue..."
fi

# Configure for ARM64
echo "Configuring CMake for ARM64..."
# Set QEMU_LD_PREFIX for test discovery during build
export QEMU_LD_PREFIX=/usr/aarch64-linux-gnu

CMAKE_ARGS=(
    -B build/linux-arm64
    -S .
    -GNinja
    -DCMAKE_SYSTEM_NAME=Linux
    -DCMAKE_SYSTEM_PROCESSOR=aarch64
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++
    -DCMAKE_PREFIX_PATH=$GTEST_ARM64_DIR
    -DCMAKE_FIND_ROOT_PATH="/usr/aarch64-linux-gnu;$GTEST_ARM64_DIR;$(pwd)/build/linux-arm64/external"
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
)

# Enable examples if requested
if [ "$BUILD_EXAMPLES" = true ]; then
    CMAKE_ARGS+=(-DBUILD_EXAMPLES=ON)
fi

cmake "${CMAKE_ARGS[@]}"

echo ""
echo "Building for ARM64 architecture..."
cmake --build build/linux-arm64 -j $(nproc)

# Verify examples if built
if [ "$BUILD_EXAMPLES" = true ]; then
    echo ""
    echo "========================================"
    echo "Verifying ARM64 Examples"
    echo "========================================"
    
    EXAMPLES=(
        "examples/01-tcp-echo/tcp-echo"
        "examples/02-udp-echo/udp-echo"
        "examples/10-typescript-interop/cpp_server"
        "examples/10-typescript-interop/cpp_client"
    )
    
    BUILT_COUNT=0
    MISSING_COUNT=0
    
    for EXAMPLE in "${EXAMPLES[@]}"; do
        FULL_PATH="build/linux-arm64/$EXAMPLE"
        if [ -f "$FULL_PATH" ]; then
            echo "  [✓] $EXAMPLE"
            BUILT_COUNT=$((BUILT_COUNT + 1))
        else
            echo "  [✗] $EXAMPLE (not found)"
            MISSING_COUNT=$((MISSING_COUNT + 1))
        fi
    done
    
    echo ""
    echo "Examples built: $BUILT_COUNT / ${#EXAMPLES[@]}"
    
    if [ $MISSING_COUNT -gt 0 ]; then
        echo "Warning: Some examples were not built. Check CMakeLists.txt configuration."
    fi
    
    echo ""
    echo "Note: Examples 03-09 require API updates and are temporarily disabled."
    echo "Note: Examples require manual testing. See examples/README.md for usage."
fi

# Verify build artifacts
echo ""
echo "========================================"
echo "ARM64 Build Artifacts"
echo "========================================"
file build/linux-arm64/test/url_parser_test build/linux-arm64/test/socket_addr_test build/linux-arm64/test/socket_basic_test 2>/dev/null | head -3 || true
file build/linux-arm64/test/sockets_test build/linux-arm64/test/http_server_test 2>/dev/null | head -2 || true

echo ""
echo "========================================"
echo "Running Tests on ARM64 (QEMU Emulation)"
echo "========================================"
echo ""

# Run tests with QEMU emulation
if which qemu-aarch64-static >/dev/null 2>&1; then
    echo "Running ARM64 tests via QEMU emulation..."
    echo ""
    
    # Use QEMU directly with library path
    export QEMU_LD_PREFIX=/usr/aarch64-linux-gnu
    cd build/linux-arm64
    ctest --output-on-failure
    cd ../..
else
    echo "Warning: QEMU ARM64 emulation not available."
    echo "Tests were built but cannot be executed."
    echo ""
    echo "To enable test execution:"
    echo "  sudo apt-get install qemu-user-static binfmt-support"
fi

echo ""
echo "========================================"
echo "ARM64 Build Complete!"
echo "========================================"
echo "Build directory: build/linux-arm64"
echo "Architecture: aarch64 (ARM64)"
echo "Compiler: aarch64-linux-gnu-g++"
echo ""
