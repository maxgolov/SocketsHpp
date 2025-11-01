#!/bin/bash
# Build SocketsHpp for ARM64 architecture using cross-compilation

set -e

echo "========================================"
echo "ARM64 Cross-Compilation Build"
echo "========================================"
echo ""

cd "$(dirname "$0")/.."

# Install ARM64 cross-compilation tools if needed
if ! dpkg -l | grep -q gcc-aarch64-linux-gnu; then
    echo "Installing ARM64 cross-compilation toolchain..."
    sudo apt-get update
    sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu git
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
rm -rf build-arm64
mkdir -p build-arm64
cd build-arm64

# Configure for ARM64
echo "Configuring CMake for ARM64..."
cmake -B . -S .. \
    -GNinja \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_PREFIX_PATH=$GTEST_ARM64_DIR \
    -DCMAKE_FIND_ROOT_PATH="/usr/aarch64-linux-gnu;$GTEST_ARM64_DIR" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY

echo ""
echo "Building for ARM64 architecture..."
cmake --build . -j $(nproc)

# Verify build artifacts
echo ""
echo "========================================"
echo "ARM64 Build Artifacts"
echo "========================================"
file test/url_parser_test test/socket_addr_test test/socket_basic_test 2>/dev/null | head -3 || true
file test/sockets_test test/http_server_test 2>/dev/null | head -2 || true

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
    ctest --output-on-failure
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
echo "Build directory: build-arm64"
echo "Architecture: aarch64 (ARM64)"
echo "Compiler: aarch64-linux-gnu-g++"
echo ""
