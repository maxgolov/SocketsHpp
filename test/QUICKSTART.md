# Quick Test Guide

Tests are opt-in: configure with `-DSOCKETSHPP_BUILD_TESTS=ON` (needs GoogleTest).
See [README.md](README.md) for the full list of test binaries.

## Linux / macOS

```bash
cmake -S . -B build -DSOCKETSHPP_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Windows (PowerShell, vcpkg)

```powershell
cmake -S . -B build -DSOCKETSHPP_BUILD_TESTS=ON `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

## Running a subset

```bash
ctest --test-dir build -R '^unit\.'                  # unit tests only
ctest --test-dir build -R '^functional\.'            # functional tests only
ctest --test-dir build -R 'unit\.SocketAddrTest\.'   # one GoogleTest suite
ctest --test-dir build -N                            # list tests without running them
```

## Running a test binary directly

```bash
./build/test/url_parser_test
./build/test/socket_addr_test --gtest_filter='SocketAddrTest.*IPv6*'
./build/test/sockets_test --gtest_list_tests
```

(`build\test\Debug\<name>.exe` with Visual Studio generators.)

## Adding a test

1. Create `test/unit/<name>_test.cc` or `test/functional/<name>_test.cc`.
2. Add `<name>_test` to `UNIT_TESTS` or `FUNCTIONAL_TESTS` in `test/CMakeLists.txt`.
3. Re-run CMake; the new cases appear as `unit.<Suite>.<Test>` /
   `functional.<Suite>.<Test>` in ctest.
