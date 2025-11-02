#!/bin/bash
# Setup ARM64 cross-compilation environment with GTest support
# This script builds GTest for ARM64 and configures the build environment

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
ARM64_ENV_DIR="$PROJECT_ROOT/.arm64-env"

echo "Setting up ARM64 build environment..."
echo "Project root: $PROJECT_ROOT"
echo "ARM64 env dir: $ARM64_ENV_DIR"

# Create ARM64 environment directory
mkdir -p "$ARM64_ENV_DIR"
cd "$ARM64_ENV_DIR"

# Check if ARM64 cross-compiler is installed
if ! command -v aarch64-linux-gnu-g++ &> /dev/null; then
    echo "Error: ARM64 cross-compiler not found"
    echo "Install with: sudo apt-get install g++-aarch64-linux-gnu"
    exit 1
fi

# Check if QEMU ARM64 is installed
if ! command -v qemu-aarch64-static &> /dev/null; then
    echo "Error: QEMU ARM64 not found"
    echo "Install with: sudo apt-get install qemu-user-static"
    exit 1
fi

# Build GTest for ARM64 if not already built
if [ ! -f "$ARM64_ENV_DIR/lib/libgtest.a" ]; then
    echo "Building GTest for ARM64..."
    
    # Clone GTest if needed
    if [ ! -d "googletest" ]; then
        git clone --depth 1 --branch v1.14.0 https://github.com/google/googletest.git
    fi
    
    cd googletest
    
    # Configure GTest for ARM64
    CC=aarch64-linux-gnu-gcc \
    CXX=aarch64-linux-gnu-g++ \
    cmake -B build-arm64 \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DCMAKE_INSTALL_PREFIX="$ARM64_ENV_DIR" \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        -DINSTALL_GTEST=ON
    
    # Build and install
    cmake --build build-arm64 -j$(nproc)
    cmake --install build-arm64
    
    cd ..
    echo "GTest for ARM64 built successfully!"
else
    echo "GTest for ARM64 already built, skipping..."
fi

# Create CMake toolchain file
echo "Creating ARM64 toolchain file..."
cat > "$ARM64_ENV_DIR/arm64-toolchain.cmake" << 'EOF'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Add ARM64 env to CMAKE_PREFIX_PATH for find_package
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}")

# Set GTest paths for manual finding if find_package fails
set(GTEST_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(GTEST_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/lib/libgtest.a")
set(GTEST_MAIN_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/lib/libgtest_main.a")
set(GTEST_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")
EOF

# Create build helper script
cat > "$PROJECT_ROOT/build-arm64.sh" << EOF
#!/bin/bash
# Helper script to build for ARM64

set -e

PROJECT_ROOT="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
ARM64_ENV_DIR="\$PROJECT_ROOT/.arm64-env"

if [ ! -f "\$ARM64_ENV_DIR/arm64-toolchain.cmake" ]; then
    echo "ARM64 environment not set up. Run: ./scripts/setup-arm64-env.sh"
    exit 1
fi

echo "Building for ARM64..."
CC=aarch64-linux-gnu-gcc \\
CXX=aarch64-linux-gnu-g++ \\
cmake -B build/linux-arm64 -S . \\
    -DCMAKE_TOOLCHAIN_FILE="\$ARM64_ENV_DIR/arm64-toolchain.cmake" \\
    -DCMAKE_BUILD_TYPE=Debug \\
    -DCMAKE_CROSSCOMPILING_EMULATOR="/usr/bin/qemu-aarch64-static;-L;/usr/aarch64-linux-gnu"

cmake --build build/linux-arm64 -j\$(nproc)

echo "ARM64 build complete!"
echo "Test executables in: build/linux-arm64/test/"
EOF

chmod +x "$PROJECT_ROOT/build-arm64.sh"

# Create test runner script
cat > "$PROJECT_ROOT/test-arm64.sh" << EOF
#!/bin/bash
# Helper script to run ARM64 tests with QEMU

set -e

PROJECT_ROOT="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="\$PROJECT_ROOT/build/linux-arm64"

if [ ! -d "\$BUILD_DIR/test" ]; then
    echo "ARM64 tests not built. Run: ./build-arm64.sh"
    exit 1
fi

echo "Running ARM64 tests with QEMU..."
cd "\$BUILD_DIR"

# Set library path for ARM64 libraries
export QEMU_LD_PREFIX=/usr/aarch64-linux-gnu

# Run tests with CTest
ctest --output-on-failure "\$@"

echo "ARM64 tests complete!"
EOF

chmod +x "$PROJECT_ROOT/test-arm64.sh"

echo ""
echo "✅ ARM64 environment setup complete!"
echo ""
echo "GTest ARM64 installed to: $ARM64_ENV_DIR"
echo "Toolchain file: $ARM64_ENV_DIR/arm64-toolchain.cmake"
echo ""
echo "To build for ARM64:"
echo "  ./build-arm64.sh"
echo ""
echo "To run ARM64 tests with QEMU:"
echo "  ./test-arm64.sh"
echo ""
