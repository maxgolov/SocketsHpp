# Example 11: Consuming SocketsHpp with vcpkg

A stand-alone project that gets SocketsHpp from vcpkg through the port in
[`ports/socketshpp`](../../ports/socketshpp/README.md) and uses it with
`find_package(SocketsHpp CONFIG REQUIRED)` and the `SocketsHpp::SocketsHpp` target.

`main.cpp` is a small HTTP server on `127.0.0.1:9000` that builds its JSON with
nlohmann-json (installed by vcpkg as a SocketsHpp dependency):

| Route | Response |
|-------|----------|
| `/` (and any unknown path) | HTML page listing the endpoints |
| `/info` | Server information as JSON |
| `/echo?msg=...` | `{"echo": "<msg>"}`; 400 with a JSON error for a malformed query string |
| `/json` | A sample JSON document |

This example is not part of the top-level `BUILD_EXAMPLES` build; build it on its own.
CI builds and runs it through the port on every push (`scripts/test-vcpkg-port.sh`).

## Prerequisites

- CMake 3.15+ and a C++17 compiler
- vcpkg, with `VCPKG_ROOT` pointing at it

## Build and run

The project uses vcpkg **manifest mode**. Its `vcpkg.json` lists `socketshpp` and
declares `../../ports` as an overlay, so inside this repository nothing else is needed:

```bash
cd examples/11-vcpkg-consumption
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
./build/vcpkg-consumer            # Windows: .\build\Release\vcpkg-consumer.exe
```

```powershell
cd examples\11-vcpkg-consumption
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
.\build\Release\vcpkg-consumer.exe
```

During configuration vcpkg builds the `socketshpp` port and its dependencies
(`nlohmann-json`, `bshoshany-thread-pool`). The port downloads the SocketsHpp sources
from GitHub at the commit pinned in `ports/socketshpp/portfile.cmake`, not from your
local checkout. `setup-linux.sh` / `setup-windows.ps1` run the same steps and start the
server.

Then try:

```bash
curl http://127.0.0.1:9000/
curl http://127.0.0.1:9000/info
curl "http://127.0.0.1:9000/echo?msg=HelloVcpkg"
curl http://127.0.0.1:9000/json
```

## Using it in your own project

1. Make the port available, either
   - as a **git registry** (no checkout needed): add a `vcpkg-configuration.json` that
     lists `https://github.com/maxgolov/SocketsHpp` as a registry for `socketshpp`, as
     shown in [ports/socketshpp/README.md](../../ports/socketshpp/README.md#option-1-git-registry-recommended); or
   - as an **overlay port**: `-DVCPKG_OVERLAY_PORTS=<SocketsHpp checkout>/ports`, or
     `"overlay-ports"` in `vcpkg-configuration.json`.
2. Add `"socketshpp"` (or `{ "name": "socketshpp", "features": ["jwt"] }` for JWT
   support in the MCP server) to the `dependencies` in your `vcpkg.json`.
3. In `CMakeLists.txt`:

   ```cmake
   find_package(SocketsHpp CONFIG REQUIRED)
   target_link_libraries(your-target PRIVATE SocketsHpp::SocketsHpp)
   ```

   The target carries the include paths, C++17, threads, `ws2_32` on Windows and
   nlohmann-json; nothing else needs to be linked.

## Files

```
11-vcpkg-consumption/
├── CMakeLists.txt     # find_package(SocketsHpp) + one executable
├── vcpkg.json         # manifest: depends on socketshpp, overlay ../../ports
├── main.cpp           # HTTP server on 127.0.0.1:9000
├── setup-linux.sh     # configure, build and run (Linux/macOS)
├── setup-windows.ps1  # the same for Windows
└── README.md
```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `socketshpp` not found / "no port named socketshpp" | Build from inside this directory (the overlay path in `vcpkg.json` is relative), or pass `-DVCPKG_OVERLAY_PORTS=<absolute path to SocketsHpp>/ports`. |
| `Could not find a package configuration file provided by "SocketsHpp"` | Configure with `-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`; delete `build/` if it was first configured without it. |
| vcpkg not found | Clone and bootstrap vcpkg (`bootstrap-vcpkg.sh` / `bootstrap-vcpkg.bat`) and set `VCPKG_ROOT`. |

## See also

- [ports/socketshpp/README.md](../../ports/socketshpp/README.md)
- [docs/INTEGRATION.md](../../docs/INTEGRATION.md#vcpkg)
- [vcpkg manifest mode](https://learn.microsoft.com/vcpkg/concepts/manifest-mode)
