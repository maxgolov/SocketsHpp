# SocketsHpp vcpkg Port

This directory is a vcpkg port for SocketsHpp. It is not in the official vcpkg
registry; use it as an **overlay port**.

## Files

| File | Purpose |
|------|---------|
| `vcpkg.json` | Port manifest: name `socketshpp`, version, dependencies, `jwt` feature, `supports` |
| `portfile.cmake` | Downloads the sources from GitHub (`maxgolov/SocketsHpp` at the `REF` in the file), configures with tests and examples off, installs headers and the CMake package |
| `usage` | Text vcpkg prints after installation |

## Dependencies and features

| Dependency | Kind |
|------------|------|
| `nlohmann-json` | required |
| `bshoshany-thread-pool` (>= 5.0.0) | required: `http_server.h` always includes `BS_thread_pool.hpp` (v5 API) |
| `vcpkg-cmake`, `vcpkg-cmake-config` | host tools used by the portfile |
| `jwt-cpp` | only with the `jwt` feature |

The `jwt` feature makes the exported `SocketsHpp::SocketsHpp` target link jwt-cpp and
define `SOCKETSHPP_HAS_JWT_CPP`, enabling JWT (HS256) validation in the MCP server.

Because the thread pool comes from the `bshoshany-thread-pool` port, the portfile
configures SocketsHpp with `-DSOCKETSHPP_INSTALL_BUNDLED_THREAD_POOL=OFF`.

## Platform support

The library is header-only and has no architecture-specific code. The port supports
every triplet except UWP (`"supports": "!uwp"`), including x64 and ARM64 on Windows,
Linux and macOS.

## Using the overlay port

### Manifest mode

Your project's `vcpkg.json`:

```json
{
  "name": "my-project",
  "version": "1.0.0",
  "dependencies": [ "socketshpp" ]
}
```

For JWT support use `{ "name": "socketshpp", "features": [ "jwt" ] }` as the
dependency instead.

Point vcpkg at the overlay, either on the CMake command line:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_OVERLAY_PORTS=/path/to/SocketsHpp/ports
cmake --build build
```

or in a `vcpkg-configuration.json` next to your `vcpkg.json`:

```json
{
  "overlay-ports": [ "/path/to/SocketsHpp/ports" ]
}
```

### Classic mode

```bash
vcpkg install socketshpp --overlay-ports=/path/to/SocketsHpp/ports
vcpkg install "socketshpp[jwt]" --overlay-ports=/path/to/SocketsHpp/ports
```

`--overlay-ports` is a vcpkg command-line option; with CMake use
`VCPKG_OVERLAY_PORTS` as shown above.

### CMake

```cmake
find_package(SocketsHpp CONFIG REQUIRED)
target_link_libraries(your-target PRIVATE SocketsHpp::SocketsHpp)
```

The target provides the include directories, C++17, threads and `ws2_32` on Windows,
and attaches `nlohmann_json::nlohmann_json` when it is found. Nothing else needs to be
linked.

A complete consumer project is in
[examples/11-vcpkg-consumption](../../examples/11-vcpkg-consumption/).

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `error: package 'socketshpp' not found` / no port named `socketshpp` | The overlay is not configured: pass `-DVCPKG_OVERLAY_PORTS=...` (manifest) or `--overlay-ports=...` (classic), using an absolute path to the `ports` directory. |
| `Could not find a package configuration file provided by "SocketsHpp"` | Configure with the vcpkg toolchain file. |
| Download or hash mismatch while building the port | The port downloads the `REF` from `portfile.cmake`; its `SHA512` must match that archive (vcpkg prints the actual hash). |

## Maintainer notes

To release a new version:

1. Update `version` in `vcpkg.json` (and in the top-level `CMakeLists.txt` project
   version).
2. Point `REF` in `portfile.cmake` at the release tag or commit and update `SHA512`
   (vcpkg reports the expected value on the first install attempt).
3. Install through the overlay on Windows, Linux and macOS and build
   `examples/11-vcpkg-consumption`.

To submit the port to the official registry, copy it to `ports/socketshpp/` in a
fork of [microsoft/vcpkg](https://github.com/microsoft/vcpkg), run
`vcpkg x-add-version socketshpp`, and open a pull request.

## References

- [Overlay ports](https://learn.microsoft.com/vcpkg/concepts/overlay-ports)
- [Manifest mode](https://learn.microsoft.com/vcpkg/concepts/manifest-mode)
- [Packaging a GitHub repository](https://learn.microsoft.com/vcpkg/get_started/get-started-packaging)
