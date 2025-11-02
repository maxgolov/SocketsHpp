# ARM64 Cross-Compilation and Testing with QEMU

This directory contains scripts for setting up a reusable ARM64 build and test environment.

## Quick Start

### One-Time Setup

Install the ARM64 cross-compilation toolchain and build environment:

```bash
./scripts/setup-arm64-env.sh
```

This will:
- Check for required tools (aarch64-linux-gnu-g++, qemu-aarch64-static)
- Clone and build GoogleTest for ARM64
- Create a reusable ARM64 environment in `.arm64-env/`
- Generate helper scripts (`build-arm64.sh`, `test-arm64.sh`)

### Building for ARM64

```bash
./build-arm64.sh
```

Builds all targets (library, samples, tests) for ARM64 architecture.

### Running ARM64 Tests with QEMU

```bash
./test-arm64.sh
```

Runs all tests using QEMU user-mode emulation. No physical ARM64 hardware required!

## Prerequisites

### Ubuntu/Debian

```bash
# ARM64 cross-compiler
sudo apt-get install g++-aarch64-linux-gnu

# QEMU user-mode emulation
sudo apt-get install qemu-user-static

# Build tools
sudo apt-get install cmake git
```

### Verification

```bash
# Check cross-compiler
aarch64-linux-gnu-g++ --version

# Check QEMU
qemu-aarch64-static --version
```

## Directory Structure

```
.arm64-env/                    # Reusable ARM64 build environment
├── include/                   # GTest headers
├── lib/
│   ├── libgtest.a            # GTest static library (ARM64)
│   ├── libgtest_main.a       # GTest main static library (ARM64)
│   └── cmake/GTest/          # CMake config files
├── arm64-toolchain.cmake     # CMake toolchain file
└── googletest/               # GTest source (built for ARM64)

build/linux-arm64/            # ARM64 build artifacts
├── samples/
│   ├── tcp-client            # ARM64 executable
│   └── udp-client            # ARM64 executable
└── test/
    ├── base64_test           # ARM64 test executable
    ├── socket_addr_test      # ARM64 test executable
    └── ...                   # Other test executables
```

## How It Works

### Cross-Compilation

The build uses:
- **Cross-compiler:** `aarch64-linux-gnu-g++` (GCC for ARM64)
- **Target system:** Linux ARM64 (aarch64)
- **CMake toolchain:** `.arm64-env/arm64-toolchain.cmake`

### Test Execution

Tests run using:
- **QEMU:** User-mode emulation (`qemu-aarch64-static`)
- **Dynamic loader:** `/usr/aarch64-linux-gnu` (ARM64 system libraries)
- **CMake integration:** `CMAKE_CROSSCOMPILING_EMULATOR` automatically runs tests with QEMU

### Reusability

The `.arm64-env` directory is:
- ✅ **Self-contained** - All ARM64 dependencies in one place
- ✅ **Persistent** - Survives clean builds (not in build/ directory)
- ✅ **Cacheable** - Can be committed or shared across machines
- ✅ **Fast** - No need to rebuild GTest each time

## Test Results

**Latest run:**
```
100% tests passed, 0 tests failed out of 106
Total Test time (real) = 15.25 sec
```

**Platforms:**
- ✅ Windows x64: 106/106 tests passing
- ✅ Linux x64: 106/106 tests passing  
- ✅ ARM64 (QEMU): 106/106 tests passing

## Advanced Usage

### Manual Build Commands

```bash
# Configure
CC=aarch64-linux-gnu-gcc \
CXX=aarch64-linux-gnu-g++ \
cmake -B build/linux-arm64 -S . \
    -DCMAKE_TOOLCHAIN_FILE=.arm64-env/arm64-toolchain.cmake \
    -DCMAKE_CROSSCOMPILING_EMULATOR="/usr/bin/qemu-aarch64-static;-L;/usr/aarch64-linux-gnu"

# Build
cmake --build build/linux-arm64 -j$(nproc)

# Test
ctest --test-dir build/linux-arm64
```

### Verify Binary Architecture

```bash
file build/linux-arm64/samples/tcp-client
# Output: ELF 64-bit LSB pie executable, ARM aarch64, ...
```

### Run Individual Test

```bash
# With QEMU
qemu-aarch64-static -L /usr/aarch64-linux-gnu \
    build/linux-arm64/test/base64_test

# Output: [==========] Running 21 tests from 1 test suite.
```

### Clean ARM64 Environment

```bash
# Remove build artifacts (keeps .arm64-env)
rm -rf build/linux-arm64

# Complete cleanup (requires re-running setup)
rm -rf .arm64-env build/linux-arm64
```

## Troubleshooting

### "qemu-aarch64: Could not open '/lib/ld-linux-aarch64.so.1'"

**Fix:** Set `QEMU_LD_PREFIX`:
```bash
export QEMU_LD_PREFIX=/usr/aarch64-linux-gnu
```

Already configured in `test-arm64.sh`.

### "GTest not found"

**Fix:** Re-run setup to build ARM64 GTest:
```bash
rm -rf .arm64-env
./scripts/setup-arm64-env.sh
```

### "Cross-compiler not found"

**Fix:** Install ARM64 toolchain:
```bash
sudo apt-get install g++-aarch64-linux-gnu
```

### Build is slow

This is expected with QEMU emulation. ARM64 tests take ~4x longer than native x64:
- x64 native: 3.64 sec
- ARM64 QEMU: 15.25 sec

For faster iteration, build without tests:
```bash
cmake --build build/linux-arm64 --target tcp-client udp-client
```

## CI/CD Integration

### GitHub Actions Example

```yaml
- name: Install ARM64 toolchain
  run: |
    sudo apt-get update
    sudo apt-get install -y g++-aarch64-linux-gnu qemu-user-static

- name: Setup ARM64 environment
  run: ./scripts/setup-arm64-env.sh

- name: Build ARM64
  run: ./build-arm64.sh

- name: Test ARM64
  run: ./test-arm64.sh
```

### Docker Example

```dockerfile
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    cmake ninja-build git \
    g++-aarch64-linux-gnu \
    qemu-user-static

WORKDIR /workspace
COPY . .

RUN ./scripts/setup-arm64-env.sh
RUN ./build-arm64.sh
RUN ./test-arm64.sh
```

## Performance Notes

QEMU user-mode emulation is:
- ✅ **Accurate** - Full ARM64 instruction emulation
- ✅ **Convenient** - No hardware required
- ⚠️ **Slower** - 3-5x slower than native execution
- ✅ **Suitable** - Excellent for CI/CD and development

For production benchmarking, use native ARM64 hardware.

## References

- [QEMU User Mode Documentation](https://www.qemu.org/docs/master/user/main.html)
- [Cross-compilation with CMake](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html)
- [ARM64 Linux ABI](https://github.com/ARM-software/abi-aa)

## Maintenance

The ARM64 environment should be updated when:
- GoogleTest version changes (update in `setup-arm64-env.sh`)
- New system dependencies are needed
- Toolchain updates are available

To update GoogleTest:
```bash
rm -rf .arm64-env
# Edit setup-arm64-env.sh to change version
./scripts/setup-arm64-env.sh
```
