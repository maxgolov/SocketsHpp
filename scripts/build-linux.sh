#!/bin/bash
# build-linux.sh
# Build and test SocketsHpp on Linux

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default values
CONFIGURATION="Debug"
SKIP_TESTS=false
CLEAN=false
VERBOSE=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --release)
            CONFIGURATION="Release"
            shift
            ;;
        --skip-tests)
            SKIP_TESTS=true
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --release      Build in Release mode (default: Debug)"
            echo "  --skip-tests   Skip running tests"
            echo "  --clean        Clean build directory before building"
            echo "  --verbose      Verbose build output"
            echo "  --help         Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}Building SocketsHpp on Linux${NC}"
echo -e "${CYAN}Configuration: $CONFIGURATION${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

cd "$PROJECT_ROOT"

# Install dependencies if needed
echo -e "${CYAN}Checking dependencies...${NC}"

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo -e "${YELLOW}CMake not found. Installing...${NC}"
    sudo apt-get update
    sudo apt-get install -y cmake
fi

# Check for Ninja (optional but recommended)
if ! command -v ninja &> /dev/null; then
    echo -e "${YELLOW}Ninja not found. Installing...${NC}"
    sudo apt-get install -y ninja-build
    USE_NINJA=true
else
    USE_NINJA=true
fi

# Check for g++ or clang++
if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    echo -e "${YELLOW}C++ compiler not found. Installing g++...${NC}"
    sudo apt-get install -y build-essential
fi

# Check for vcpkg or install Google Test via apt
if [ ! -d "$HOME/vcpkg" ]; then
    echo -e "${YELLOW}vcpkg not found. Installing Google Test via apt...${NC}"
    sudo apt-get update
    sudo apt-get install -y libgtest-dev
    
    # Build and install gtest if needed
    if [ ! -f /usr/lib/libgtest.a ] && [ ! -f /usr/lib/x86_64-linux-gnu/libgtest.a ]; then
        echo -e "${YELLOW}Building Google Test...${NC}"
        cd /usr/src/gtest
        sudo cmake CMakeLists.txt
        sudo make
        sudo cp lib/*.a /usr/lib/ 2>/dev/null || sudo cp *.a /usr/lib/
        cd "$PROJECT_ROOT"
    fi
fi

# Clean build directory if requested
if [ "$CLEAN" = true ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf build
fi

# Configure with CMake
echo -e "\n${CYAN}Configuring project with CMake...${NC}"

CMAKE_ARGS=(
    -B build
    -S .
    -DCMAKE_BUILD_TYPE="$CONFIGURATION"
)

# Use Ninja if available
if [ "$USE_NINJA" = true ]; then
    CMAKE_ARGS+=(-G Ninja)
fi

echo -e "${NC}Running: cmake ${CMAKE_ARGS[*]}${NC}"
cmake "${CMAKE_ARGS[@]}"

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed${NC}"
    exit 1
fi

# Build with CMake
echo -e "\n${CYAN}Building project...${NC}"

BUILD_ARGS=(
    --build build
    --config "$CONFIGURATION"
)

if [ "$VERBOSE" = true ]; then
    BUILD_ARGS+=(--verbose)
fi

# Add parallel build
BUILD_ARGS+=(-j "$(nproc)")

echo -e "${NC}Running: cmake ${BUILD_ARGS[*]}${NC}"
cmake "${BUILD_ARGS[@]}"

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed${NC}"
    exit 1
fi

echo -e "\n${GREEN}Build completed successfully!${NC}"

# Run tests
if [ "$SKIP_TESTS" = false ]; then
    echo -e "\n${CYAN}Running tests...${NC}"
    cd build
    
    TEST_ARGS=(
        --output-on-failure
    )
    
    if [ "$VERBOSE" = true ]; then
        TEST_ARGS+=(-V)
    fi
    
    echo -e "${NC}Running: ctest ${TEST_ARGS[*]}${NC}"
    ctest "${TEST_ARGS[@]}"
    
    TEST_EXIT_CODE=$?
    cd "$PROJECT_ROOT"
    
    if [ $TEST_EXIT_CODE -eq 0 ]; then
        echo -e "\n${GREEN}All tests passed!${NC}"
    else
        echo -e "\n${RED}Tests failed with exit code $TEST_EXIT_CODE${NC}"
        exit $TEST_EXIT_CODE
    fi
else
    echo -e "\n${YELLOW}Skipping tests (--skip-tests specified)${NC}"
fi

echo -e "\n${GREEN}========================================${NC}"
echo -e "${GREEN}Linux Build Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
