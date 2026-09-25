# Example 11: Consuming SocketsHpp with vcpkg

A stand-alone project that gets SocketsHpp from vcpkg, using the overlay port in
[`ports/socketshpp`](../../ports/socketshpp/README.md), and uses it through
`find_package(SocketsHpp CONFIG REQUIRED)` and the `SocketsHpp::SocketsHpp` target.

`main.cpp` is a small HTTP server on port 9000 with these routes:

| Route | Response |
|-------|----------|
| `/` (and any unknown path) | HTML page listing the endpoints |
| `/info` | Server information as JSON |
| `/echo?msg=...` | Echoes the `msg` query parameter |
| `/json` | A sample JSON document |

This example is not part of the top-level `BUILD_EXAMPLES` build; build it on its own.

## Prerequisites

- CMake 3.15+ and a C++17 compiler
- vcpkg, with `VCPKG_ROOT` pointing at it

## Build and run

The project uses vcpkg **manifest mode** (`vcpkg.json` lists `socketshpp`). SocketsHpp
is not in the official registry, so tell vcpkg where the port is with
`VCPKG_OVERLAY_PORTS`:

```bash
cd examples/11-vcpkg-consumption
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_OVERLAY_PORTS="$PWD/../../ports"
cmake --build build --config Release
./build/vcpkg-consumer            # Windows: .\build\Release\vcpkg-consumer.exe
```

```powershell
cd examples\11-vcpkg-consumption
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_OVERLAY_PORTS="$PWD/../../ports"
cmake --build build --config Release
.\build\Release\vcpkg-consumer.exe
```

During configuration vcpkg builds the `socketshpp` port and its dependencies
(`nlohmann-json`, `bshoshany-thread-pool`). The port downloads the SocketsHpp sources
from GitHub at the revision pinned in `ports/socketshpp/portfile.cmake`, not from your
local checkout.

Then try:

```bash
curl http://localhost:9000/
curl http://localhost:9000/info
curl "http://localhost:9000/echo?msg=HelloVcpkg"
curl http://localhost:9000/json
```

## Using it in your own project

1. Make the port available: reference `SocketsHpp/ports` from `VCPKG_OVERLAY_PORTS`
   (as above) or from `"overlay-ports"` in a `vcpkg-configuration.json`, or copy the
   `ports/socketshpp` directory into your own overlay directory.
2. Add `"socketshpp"` (or `{ "name": "socketshpp", "features": ["jwt"] }` for JWT
   support in the MCP server) to the `dependencies` in your `vcpkg.json`.
3. In `CMakeLists.txt`:

   ```cmake
   find_package(SocketsHpp CONFIG REQUIRED)
   target_link_libraries(your-target PRIVATE SocketsHpp::SocketsHpp)
   ```

   The target already carries threads, `ws2_32` on Windows and nlohmann/json, so the
   extra `ws2_32` / `pthread` lines in this example's `CMakeLists.txt` are optional.

## Files

```
11-vcpkg-consumption/
├── CMakeLists.txt     # find_package(SocketsHpp) + one executable
├── vcpkg.json         # manifest: depends on socketshpp
├── main.cpp           # HTTP server on port 9000
├── setup-linux.sh     # helper scripts (see note below)
├── setup-windows.ps1
└── README.md
```

Note: the setup scripts run `vcpkg install socketshpp --overlay-ports=...` and then
configure without an overlay. Inside this directory vcpkg runs in manifest mode, where
`vcpkg install` does not accept package names, so prefer the CMake commands above.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `socketshpp` not found / "no port named socketshpp" | Pass `-DVCPKG_OVERLAY_PORTS=<path to SocketsHpp>/ports` (an absolute path is safest). |
| `Could not find a package configuration file provided by "SocketsHpp"` | Configure with `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`; delete `build/` if it was first configured without it. |
| vcpkg not found | Clone and bootstrap vcpkg (`bootstrap-vcpkg.sh` / `bootstrap-vcpkg.bat`) and set `VCPKG_ROOT`. |

## See also

- [ports/socketshpp/README.md](../../ports/socketshpp/README.md)
- [docs/INTEGRATION.md](../../docs/INTEGRATION.md#vcpkg-overlay-port)
- [vcpkg manifest mode](https://learn.microsoft.com/vcpkg/concepts/manifest-mode)
