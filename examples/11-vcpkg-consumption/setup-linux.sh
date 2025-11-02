#!/bin/bash
# Copyright Max Golovanov.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# Configuration
EXAMPLE_NAME="11-vcpkg-consumption"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
EXAMPLE_DIR="$SCRIPT_DIR"
BUILD_DIR="$EXAMPLE_DIR/build"
OVERLAY_PORTS_DIR="$REPO_ROOT/ports"

# Parse arguments
SKIP_BUILD=0
CLEAN_BUILD=0

for arg in "$@"; do
    case $arg in
        --skip-build)
            SKIP_BUILD=1
            ;;
        --clean-build)
            CLEAN_BUILD=1
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [--skip-build] [--clean-build]"
            exit 1
            ;;
    esac
done

echo "========================================"
echo " SocketsHpp vcpkg Consumption Setup"
echo "========================================"
echo ""
echo "Example Directory: $EXAMPLE_DIR"
echo "Repository Root: $REPO_ROOT"
echo "Overlay Ports: $OVERLAY_PORTS_DIR"
echo ""

# Step 1: Check for vcpkg
echo "[1/5] Checking for vcpkg..."
if ! command -v vcpkg &> /dev/null; then
    echo "ERROR: vcpkg not found in PATH"
    echo ""
    echo "Please install vcpkg:"
    echo "  git clone https://github.com/microsoft/vcpkg.git"
    echo "  cd vcpkg"
    echo "  ./bootstrap-vcpkg.sh"
    echo "  export PATH=\"\$PWD:\$PATH\""
    exit 1
fi

VCPKG_CMD=$(command -v vcpkg)
VCPKG_ROOT="$(cd "$(dirname "$VCPKG_CMD")" && pwd)"
echo "Found vcpkg at: $VCPKG_ROOT"
echo ""

# Step 2: Verify overlay ports directory
echo "[2/5] Verifying overlay ports..."
if [ ! -d "$OVERLAY_PORTS_DIR" ]; then
    echo "ERROR: Overlay ports directory not found: $OVERLAY_PORTS_DIR"
    exit 1
fi

SOCKETSHPP_PORT="$OVERLAY_PORTS_DIR/socketshpp"
if [ ! -d "$SOCKETSHPP_PORT" ]; then
    echo "ERROR: SocketsHpp port not found: $SOCKETSHPP_PORT"
    exit 1
fi

echo "Found SocketsHpp port at: $SOCKETSHPP_PORT"
echo ""

# Step 3: Install SocketsHpp via vcpkg overlay
echo "[3/5] Installing SocketsHpp via vcpkg..."
echo "Command: vcpkg install socketshpp --overlay-ports=$OVERLAY_PORTS_DIR"

if vcpkg install socketshpp --overlay-ports="$OVERLAY_PORTS_DIR"; then
    echo "SocketsHpp installed successfully!"
else
    echo "ERROR: Failed to install SocketsHpp"
    exit 1
fi
echo ""

# Step 4: Build the example
if [ $SKIP_BUILD -eq 1 ]; then
    echo "[4/5] Skipping build (--skip-build flag set)"
    echo ""
    echo "Setup complete! To build manually:"
    echo "  cd $EXAMPLE_DIR"
    echo "  cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    echo "  cmake --build build --config Release"
    exit 0
fi

echo "[4/5] Building example..."

# Clean build if requested
if [ $CLEAN_BUILD -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    echo "Removing old build directory..."
    rm -rf "$BUILD_DIR"
fi

# Configure with CMake
echo "Configuring CMake..."
TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
if ! cmake -B "$BUILD_DIR" -S "$EXAMPLE_DIR" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"; then
    echo "ERROR: CMake configuration failed"
    exit 1
fi

# Build
echo "Building..."
if ! cmake --build "$BUILD_DIR" --config Release; then
    echo "ERROR: Build failed"
    exit 1
fi
echo "Build successful!"
echo ""

# Step 5: Run the example
echo "[5/5] Running example..."
echo "Starting HTTP server on http://localhost:9000"
echo "Press Ctrl+C to stop"
echo ""

EXE_PATH="$BUILD_DIR/vcpkg-consumer"
if [ ! -f "$EXE_PATH" ]; then
    echo "ERROR: Executable not found: $EXE_PATH"
    exit 1
fi

"$EXE_PATH"
