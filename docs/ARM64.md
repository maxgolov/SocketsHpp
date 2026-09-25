# Linux ARM64: Cross-Compilation and Testing with QEMU

SocketsHpp itself needs nothing special for ARM64: it is header-only and uses epoll on
Linux on every architecture. This page covers building the tests and examples for
`aarch64` on an x64 Linux host (or WSL) and running them under QEMU user-mode
emulation. On a native ARM64 machine, use the regular build from the
[README](../README.md#building-this-repository).

## Prerequisites (Ubuntu/Debian)

```bash
sudo apt-get install g++-aarch64-linux-gnu qemu-user-static cmake ninja-build git
git submodule update --init --recursive   # nlohmann-json (tests use it for MCP)
```

`aarch64-linux-gnu-g++ --version` and `qemu-aarch64-static --version` should both work.

## One-time setup: GoogleTest for ARM64

```bash
./scripts/setup-arm64-env.sh
```

The script:

- checks for the cross-compiler and QEMU;
- clones GoogleTest v1.14.0 and installs an ARM64 build of it into `.arm64-env/`;
- writes the toolchain file `.arm64-env/arm64-toolchain.cmake`;
- (re)generates the helper scripts `./build-arm64.sh` and `./test-arm64.sh` in the
  repository root.

`.arm64-env/` survives clean builds; delete it to start over.

## Build and test

Tests are opt-in, so pass `-DSOCKETSHPP_BUILD_TESTS=ON`:

```bash
cmake -S . -B build/linux-arm64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=.arm64-env/arm64-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSOCKETSHPP_BUILD_TESTS=ON \
    -DBUILD_EXAMPLES=ON \
    -DCMAKE_CROSSCOMPILING_EMULATOR="/usr/bin/qemu-aarch64-static;-L;/usr/aarch64-linux-gnu"
cmake --build build/linux-arm64 --parallel

QEMU_LD_PREFIX=/usr/aarch64-linux-gnu ctest --test-dir build/linux-arm64 --output-on-failure
```

`CMAKE_CROSSCOMPILING_EMULATOR` makes both GoogleTest's test discovery (at build time)
and ctest run the ARM64 binaries through QEMU.

The generated `./build-arm64.sh` configures the same build directory but without
`-DSOCKETSHPP_BUILD_TESTS=ON`, so it builds no tests unless you add that option to it;
`./test-arm64.sh` then runs ctest in `build/linux-arm64` with `QEMU_LD_PREFIX` set.

### Checking and running binaries

```bash
file build/linux-arm64/test/base64_test
# ELF 64-bit LSB pie executable, ARM aarch64, ...

qemu-aarch64-static -L /usr/aarch64-linux-gnu build/linux-arm64/test/base64_test
qemu-aarch64-static -L /usr/aarch64-linux-gnu build/linux-arm64/examples/03-http-server/http-server
```

Test binaries are in `build/linux-arm64/test/<name>` and examples in
`build/linux-arm64/examples/<example>/<name>`.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `qemu-aarch64: Could not open '/lib/ld-linux-aarch64.so.1'` | Export `QEMU_LD_PREFIX=/usr/aarch64-linux-gnu` (or pass `-L /usr/aarch64-linux-gnu`). |
| `Could not find a package configuration file provided by "GTest"` | Run `./scripts/setup-arm64-env.sh`, and configure with its toolchain file. |
| `aarch64-linux-gnu-g++: not found` | `sudo apt-get install g++-aarch64-linux-gnu` |
| Tests are slow | Expected under emulation (several times slower than native). Build a single target with `--target <name>` while iterating. |
| Tests using IPv6 are skipped | The host (or container) has no IPv6 loopback; this is reported as "Skipped", not a failure. |

`scripts/build-arm64.sh` is an older WSL-oriented variant of this flow; the steps
above are the reference.

## Windows on ARM64

Windows ARM64 builds use MSVC's ARM64 toolset with the normal CMake flow (for example
`cmake -S . -B build -A ARM64`); there is nothing ARM64-specific in the code, but
this configuration is not covered by CI.

## References

- [QEMU user-mode emulation](https://www.qemu.org/docs/master/user/main.html)
- [CMake toolchains](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html)
