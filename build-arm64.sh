#!/bin/bash
# Helper script to build for ARM64

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARM64_ENV_DIR="$PROJECT_ROOT/.arm64-env"

if [ ! -f "$ARM64_ENV_DIR/arm64-toolchain.cmake" ]; then
    echo "ARM64 environment not set up. Run: ./scripts/setup-arm64-env.sh"
    exit 1
fi

echo "Building for ARM64..."
CC=aarch64-linux-gnu-gcc \
CXX=aarch64-linux-gnu-g++ \
cmake -B build/linux-arm64 -S . \
    -DCMAKE_TOOLCHAIN_FILE="$ARM64_ENV_DIR/arm64-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CROSSCOMPILING_EMULATOR="/usr/bin/qemu-aarch64-static;-L;/usr/aarch64-linux-gnu"

cmake --build build/linux-arm64 -j$(nproc)

echo "ARM64 build complete!"
echo "Test executables in: build/linux-arm64/test/"
