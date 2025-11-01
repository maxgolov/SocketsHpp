# Build Scripts

This directory contains scripts for building and testing SocketsHpp on different platforms.

## Windows

### Install-Tools.ps1

Installs all required development tools on Windows.

**Requirements:**
- Windows 10/11
- PowerShell 5.1 or later
- Administrator privileges

**What it installs:**
- Visual Studio 2022 Community Edition (with C++ Desktop Development)
- LLVM/Clang (latest version)
- CMake (latest version)
- Ninja Build System
- vcpkg Package Manager
- Google Test (via vcpkg)

**Usage:**

```powershell
# Run as Administrator
.\scripts\Install-Tools.ps1

# Skip specific components
.\scripts\Install-Tools.ps1 -SkipVS2022   # Skip Visual Studio installation
.\scripts\Install-Tools.ps1 -SkipLLVM     # Skip LLVM installation
.\scripts\Install-Tools.ps1 -SkipCMake    # Skip CMake installation
.\scripts\Install-Tools.ps1 -SkipNinja    # Skip Ninja installation
.\scripts\Install-Tools.ps1 -SkipVcpkg    # Skip vcpkg installation
```

### Build-Windows.ps1

Builds and tests the project on Windows.

**Usage:**

```powershell
# Build in Debug mode and run tests
.\scripts\Build-Windows.ps1

# Build in Release mode
.\scripts\Build-Windows.ps1 -Configuration Release

# Skip tests
.\scripts\Build-Windows.ps1 -SkipTests

# Clean build
.\scripts\Build-Windows.ps1 -Clean

# Verbose output
.\scripts\Build-Windows.ps1 -Verbose

# Combine options
.\scripts\Build-Windows.ps1 -Configuration Release -Clean -Verbose
```

## Linux

### build-linux.sh

Builds and tests the project on Linux (including WSL).

**Requirements:**
- Ubuntu/Debian-based Linux distribution
- sudo privileges (for installing dependencies)

**What it installs (if missing):**
- CMake
- Ninja Build System
- GCC/G++ or Clang
- Google Test

**Usage:**

```bash
# Make executable
chmod +x scripts/build-linux.sh

# Build in Debug mode and run tests
./scripts/build-linux.sh

# Build in Release mode
./scripts/build-linux.sh --release

# Skip tests
./scripts/build-linux.sh --skip-tests

# Clean build
./scripts/build-linux.sh --clean

# Verbose output
./scripts/build-linux.sh --verbose

# Combine options
./scripts/build-linux.sh --release --clean --verbose

# Show help
./scripts/build-linux.sh --help
```

### build-arm64.sh

Builds and tests the project for ARM64 architecture using cross-compilation and QEMU emulation.

**Requirements:**
- Ubuntu/Debian-based Linux distribution (or WSL2)
- sudo privileges (for installing dependencies)

**What it installs (if missing):**
- ARM64 cross-compilation toolchain (gcc-aarch64-linux-gnu, g++-aarch64-linux-gnu)
- QEMU ARM64 user-mode emulation (qemu-user-static)
- binfmt-support for transparent ARM64 binary execution
- Google Test built for ARM64 architecture

**Usage:**

```bash
# Make executable
chmod +x scripts/build-arm64.sh

# Build and test on ARM64
./scripts/build-arm64.sh
```

**From Windows (PowerShell):**

```powershell
# Build and test
.\scripts\Install-qemu-arm64.ps1

# Run tests only (assumes build exists)
.\scripts\Install-qemu-arm64.ps1 -SkipBuild
```

**Output:**
- Build directory: `build-arm64/`
- All binaries are ARM64 (aarch64) ELF executables
- Tests run via QEMU emulation (transparent to CTest)
- **Test Results:** 78/78 tests pass (1 disabled test)

## Quick Start

### Windows

```powershell
# 1. Install tools (run as Administrator, first time only)
.\scripts\Install-Tools.ps1

# 2. Restart your terminal

# 3. Build and test
.\scripts\Build-Windows.ps1
```

### Linux / WSL

```bash
# 1. Make script executable
chmod +x scripts/build-linux.sh

# 2. Build and test (will install dependencies automatically)
./scripts/build-linux.sh
```

## CI/CD Integration

These scripts are designed to work in CI/CD environments:

### GitHub Actions (Windows)

```yaml
- name: Install Tools
  shell: powershell
  run: |
    .\scripts\Install-Tools.ps1 -SkipVS2022

- name: Build and Test
  shell: powershell
  run: |
    .\scripts\Build-Windows.ps1 -Configuration Release
```

### GitHub Actions (Linux)

```yaml
- name: Build and Test
  run: |
    chmod +x scripts/build-linux.sh
    ./scripts/build-linux.sh --release
```

## Troubleshooting

### Windows

**"Script execution is disabled"**
```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

**"vcpkg not found"**
```powershell
# Set VCPKG_ROOT environment variable
$env:VCPKG_ROOT = "C:\vcpkg"
# Or run Install-Tools.ps1 to install vcpkg
```

**"Visual Studio not found"**
- Ensure Visual Studio 2022 is installed with C++ Desktop Development workload
- Or run `Install-Tools.ps1` to install it automatically

### Linux

**"Permission denied"**
```bash
chmod +x scripts/build-linux.sh
```

**"CMake not found" or "GTest not found"**
- The script will automatically install dependencies with sudo
- Or manually install: `sudo apt-get install cmake libgtest-dev build-essential`

**WSL-specific issues**
- Ensure WSL2 is installed and updated
- Run `wsl --update` to get the latest version

## Manual Build (Without Scripts)

### Windows

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Debug
cd build
ctest -C Debug --output-on-failure
```

### Linux

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
cd build
ctest --output-on-failure
```
