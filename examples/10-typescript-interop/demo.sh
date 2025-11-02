#!/bin/bash
# Build and run the TypeScript/C++ MCP interop example

set -e

echo "======================================"
echo "TypeScript/C++ MCP Interop Example"
echo "======================================"
echo

# Check if Node.js is installed
if ! command -v node &> /dev/null; then
    echo "Error: Node.js is not installed. Please install Node.js 18+ first."
    exit 1
fi

echo "Step 1: Building C++ components..."
echo "-----------------------------------"

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --config Release

cd ..

echo
echo "Step 2: Installing TypeScript dependencies..."
echo "----------------------------------------------"

# Install TS server dependencies
cd ts_server
npm install
cd ..

# Install TS client dependencies
cd ts_client
npm install
cd ..

echo
echo "✅ Build completed successfully!"
echo
echo "======================================"
echo "Running Demo"
echo "======================================"
echo

# Function to cleanup background processes
cleanup() {
    echo
    echo "Cleaning up..."
    if [ ! -z "$CPP_SERVER_PID" ]; then
        kill $CPP_SERVER_PID 2>/dev/null || true
    fi
    if [ ! -z "$TS_SERVER_PID" ]; then
        kill $TS_SERVER_PID 2>/dev/null || true
    fi
    exit
}

trap cleanup EXIT INT TERM

echo "Demo 1: TypeScript Client → C++ MCP Server"
echo "-------------------------------------------"

# Start C++ server in background
./build/cpp_server &
CPP_SERVER_PID=$!

# Wait for server to start
sleep 2

# Run TS client
cd ts_client
npx tsx client.ts
cd ..

# Stop C++ server
kill $CPP_SERVER_PID 2>/dev/null || true
sleep 1

echo
echo "Demo 2: C++ Client → TypeScript MCP Server"
echo "-------------------------------------------"

# Start TS server in background
cd ts_server
npx tsx server.ts &
TS_SERVER_PID=$!
cd ..

# Wait for server to start
sleep 2

# Run C++ client
./build/cpp_client

# Stop TS server
kill $TS_SERVER_PID 2>/dev/null || true

echo
echo "======================================"
echo "✅ All demos completed successfully!"
echo "======================================"
