# SocketsHpp vcpkg Port

SocketsHpp is not in the official vcpkg registry. This repository is itself a
[vcpkg git registry](https://learn.microsoft.com/vcpkg/maintainers/registries): the
port lives in `ports/socketshpp/` and its version database in `versions/`. Consume it
either as a **git registry** (recommended; nothing to clone) or as an **overlay port**
from a local checkout.

Either way, the port builds SocketsHpp from the public GitHub repository at the
commit pinned in `portfile.cmake` (`REF` + `SHA512`), not from your working tree.

## Files

| File | Purpose |
|------|---------|
| `ports/socketshpp/vcpkg.json` | Port manifest: name `socketshpp`, version, dependencies, `jwt` feature, `supports` |
| `ports/socketshpp/portfile.cmake` | Downloads `maxgolov/SocketsHpp` at the pinned `REF`, configures with tests and examples off, installs the headers and the CMake package |
| `ports/socketshpp/usage` | Text vcpkg prints after installation |
| `versions/baseline.json`, `versions/s-/socketshpp.json` | Registry version database: which port tree (`git-tree`) is version 1.0.0 |

## Dependencies and features

| Dependency | Kind |
|------------|------|
| `nlohmann-json` | required |
| `bshoshany-thread-pool` | required: `http_server.h` always includes `BS_thread_pool.hpp` and uses the v5 API (the vcpkg port is 5.x) |
| `vcpkg-cmake`, `vcpkg-cmake-config` | host tools used by the portfile |
| `jwt-cpp` | only with the `jwt` feature |

The `jwt` feature makes the exported `SocketsHpp::SocketsHpp` target link jwt-cpp and
define `SOCKETSHPP_HAS_JWT_CPP`, enabling JWT (HS256) validation in the MCP server.
Without the feature the port builds with `CMAKE_DISABLE_FIND_PACKAGE_jwt-cpp=ON`, so an
unrelated jwt-cpp install is never picked up.

Because the thread pool comes from the `bshoshany-thread-pool` port, the portfile
configures SocketsHpp with `-DSOCKETSHPP_INSTALL_BUNDLED_THREAD_POOL=OFF`.

## Platform support

The library is header-only and has no architecture-specific code. The port supports
every triplet except UWP (`"supports": "!uwp"`), including x64 and ARM64 on Windows,
Linux and macOS.

## Option 1: git registry (recommended)

Add a `vcpkg-configuration.json` next to your project's `vcpkg.json`:

```json
{
  "default-registry": {
    "kind": "git",
    "repository": "https://github.com/microsoft/vcpkg",
    "baseline": "<a microsoft/vcpkg commit SHA>"
  },
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/maxgolov/SocketsHpp",
      "baseline": "<a maxgolov/SocketsHpp commit SHA>",
      "packages": [ "socketshpp" ]
    }
  ]
}
```

- `baseline` for SocketsHpp is any commit of this repository that contains
  `versions/` (for example the latest commit on `main`); it selects the port version
  listed in that commit's `versions/baseline.json`.
- `default-registry` supplies `nlohmann-json`, `bshoshany-thread-pool` and `jwt-cpp`.
  If you already use `"builtin-baseline"` in `vcpkg.json`, you can omit
  `default-registry`.

Your `vcpkg.json`:

```json
{
  "name": "my-project",
  "version": "1.0.0",
  "dependencies": [ "socketshpp" ]
}
```

(`{ "name": "socketshpp", "features": [ "jwt" ] }` for JWT support.) Then configure
with the vcpkg toolchain file as usual:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

## Option 2: overlay port from a checkout

Point vcpkg at the `ports` directory of a SocketsHpp checkout, on the CMake command
line (manifest mode):

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_OVERLAY_PORTS=/path/to/SocketsHpp/ports
```

or with `"overlay-ports": [ "/path/to/SocketsHpp/ports" ]` in
`vcpkg-configuration.json`, or in classic mode:

```bash
vcpkg install socketshpp --overlay-ports=/path/to/SocketsHpp/ports
vcpkg install "socketshpp[jwt]" --overlay-ports=/path/to/SocketsHpp/ports
```

The overlay still builds the pinned GitHub commit. To build your local working tree
instead, use SocketsHpp directly with `add_subdirectory()` or `FetchContent` (see
[docs/INTEGRATION.md](../../docs/INTEGRATION.md)).

## CMake

```cmake
find_package(SocketsHpp CONFIG REQUIRED)
target_link_libraries(your-target PRIVATE SocketsHpp::SocketsHpp)
```

The target provides the include directories, C++17, threads and `ws2_32` on Windows,
and attaches `nlohmann_json::nlohmann_json` when it is found. Nothing else needs to be
linked.

A complete consumer project is in
[examples/11-vcpkg-consumption](../../examples/11-vcpkg-consumption/). CI builds it
through this port on every push (`scripts/test-vcpkg-port.sh`), in both overlay and
registry mode.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `error: package 'socketshpp' not found` / no port named `socketshpp` | Registry: check that `"packages"` lists `socketshpp` and that `baseline` is a commit that contains `versions/`. Overlay: pass an absolute path to the `ports` directory. |
| `Could not find a package configuration file provided by "SocketsHpp"` | Configure with the vcpkg toolchain file; delete the build directory if it was first configured without it. |
| Download or hash mismatch while building the port | The `SHA512` in `portfile.cmake` does not match the archive of `REF` (vcpkg prints the actual hash). |
| Registry: `the git tree ... was not found` | `versions/s-/socketshpp.json` does not match the committed port; see the release steps below. |

## Maintainer notes: releasing a new version

1. Update `version` in `ports/socketshpp/vcpkg.json` (and the `project()` version in
   the top-level `CMakeLists.txt`). For a port-only fix keep the version and add or
   increase `"port-version"` instead.
2. Set `REF` in `portfile.cmake` to the release tag or commit and update `SHA512`
   (compute it with `curl -sL https://github.com/maxgolov/SocketsHpp/archive/<REF>.tar.gz | sha512sum`,
   or let vcpkg report it on the first install).
3. Commit the port, then update the registry database and commit again:

   ```bash
   vcpkg x-add-version socketshpp \
     --x-builtin-ports-root=ports --x-builtin-registry-versions-dir=versions
   ```

   This records the new `git-tree` of `ports/socketshpp` in
   `versions/s-/socketshpp.json` and updates `versions/baseline.json`. Any change to
   files under `ports/socketshpp/` needs this step, or registry consumers get the old
   port. `scripts/test-vcpkg-port.sh` fails when the recorded `git-tree` is stale.
4. Run `scripts/test-vcpkg-port.sh` (CI does this too).

To submit the port to the official registry, copy `ports/socketshpp/` into a fork of
[microsoft/vcpkg](https://github.com/microsoft/vcpkg), run
`vcpkg x-add-version socketshpp` there, and open a pull request.

## References

- [Registries](https://learn.microsoft.com/vcpkg/maintainers/registries)
- [Overlay ports](https://learn.microsoft.com/vcpkg/concepts/overlay-ports)
- [Manifest mode](https://learn.microsoft.com/vcpkg/concepts/manifest-mode)
- [Packaging a GitHub repository](https://learn.microsoft.com/vcpkg/get_started/get-started-packaging)
