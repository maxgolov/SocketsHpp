# SocketsHpp vcpkg Port

This directory contains the vcpkg port overlay for SocketsHpp, enabling easy installation and consumption of the library through the vcpkg package manager.

## Port Structure

```
ports/socketshpp/
├── vcpkg.json      # Port manifest with metadata and dependencies
├── portfile.cmake  # Build instructions for vcpkg
└── usage           # Post-installation usage instructions
```

## Files Overview

### vcpkg.json
Defines the port metadata:
- **Name**: `socketshpp`
- **Version**: `1.0.0`
- **Description**: Lean header-only C++17 networking library
- **Dependencies**:
  - `nlohmann-json` (JSON parsing)
  - `cpp-jwt` (JWT authentication)
  - `bshoshany-thread-pool` (optional multi-threading)
  - `vcpkg-cmake` (build system)
  - `vcpkg-cmake-config` (CMake package config)

### portfile.cmake
Implements the installation process:
1. Downloads source from GitHub (`maxgolov/SocketsHpp`)
2. Installs headers to `include/SocketsHpp/`
3. Removes unnecessary library directories
4. Configures CMake package detection
5. Installs usage documentation

### usage
Post-installation instructions shown to users:
- CMake `find_package()` usage
- Linking instructions
- Dependency information

## Using the Port Overlay

### Method 1: Local Development (Overlay Ports)

Install SocketsHpp from the local repository:

**Windows (PowerShell):**
```powershell
vcpkg install socketshpp --overlay-ports=./ports
```

**Linux/macOS (Bash):**
```bash
vcpkg install socketshpp --overlay-ports=./ports
```

### Method 2: Manifest Mode (Recommended)

Add to your project's `vcpkg.json`:
```json
{
  "name": "my-project",
  "version": "1.0.0",
  "dependencies": ["socketshpp"],
  "vcpkg-configuration": {
    "overlay-ports": ["path/to/SocketsHpp/ports"]
  }
}
```

Then build with CMake:
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### Method 3: Git URL Registry

Add a registry to your `vcpkg-configuration.json`:
```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/maxgolov/SocketsHpp",
      "baseline": "main",
      "packages": ["socketshpp"]
    }
  ]
}
```

## CMake Integration

After installing via vcpkg, use in your CMakeLists.txt:

```cmake
find_package(SocketsHpp CONFIG REQUIRED)
target_link_libraries(your-target PRIVATE SocketsHpp::SocketsHpp)
```

## What Are Overlay Ports?

Overlay ports are **local port definitions** that override vcpkg's official registry without requiring forks or pull requests. Key points:

- **No forking required**: Overlay ports live in your project, not in microsoft/vcpkg
- **Take priority**: When a package name matches both an overlay and official registry, the overlay wins
- **Temporary or permanent**: Use for local modifications, private libraries, or testing before submitting upstream
- **Not versioned**: Overlay ports don't participate in vcpkg's versioning system

### When to Use Overlay Ports

✅ **Use overlay ports for:**
- Private/proprietary libraries not suitable for public registry
- Temporary modifications while waiting for upstream fixes
- Testing custom patches before submitting to vcpkg
- Internal company libraries
- Rapid prototyping of package changes

❌ **Don't use overlay ports for:**
- Public libraries that should be in the official registry (submit a PR instead)
- Sharing packages across teams (use a custom Git registry instead)
- Long-term version tracking (use a custom Git registry)

## Platform Support

The port currently supports:
- ✅ Windows (x64, ARM64)
- ✅ Linux (x64, ARM64)
- ✅ macOS (x64, ARM64)
- ❌ UWP (Universal Windows Platform) - explicitly excluded
- ❌ ARM 32-bit - explicitly excluded

Platform restrictions are defined in `vcpkg.json`:
```json
"supports": "!(uwp | arm)"
```

## Triplet Considerations

### Windows Triplets
- `x64-windows` - Dynamic linking (DLL)
- `x64-windows-static` - Static linking
- `x64-windows-static-md` - Static linking with dynamic MSVC runtime

**Recommended**: `x64-windows-static-md` for most Windows projects

### Linux Triplets
- `x64-linux` - Default dynamic linking
- `x64-linux-static` - Static linking
- `arm64-linux` - ARM64 support

### macOS Triplets
- `x64-osx` - Intel Macs
- `arm64-osx` - Apple Silicon (M1/M2/M3)

## Build Features

The port does NOT expose build features (e.g., threading, compression) because:
- SocketsHpp is header-only - features are controlled at compile-time
- Dependencies (thread-pool, cpp-jwt, nlohmann-json) are always available
- Users can opt-in to features by including specific headers

## Troubleshooting

### Port not found
**Error**: `error: package 'socketshpp' not found`
**Solution**: Ensure you're using `--overlay-ports=./ports` or have configured the registry correctly

### Dependency conflicts
**Error**: Version conflicts with nlohmann-json or other packages
**Solution**: Use vcpkg's version constraints in your `vcpkg.json`:
```json
{
  "dependencies": [
    {
      "name": "socketshpp",
      "version>=": "1.0.0"
    }
  ]
}
```

### CMake can't find package
**Error**: `Could not find a package configuration file provided by "SocketsHpp"`
**Solution**: Ensure you're using the vcpkg toolchain file:
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake
```

### Header-only library linking errors
**Error**: Linker errors about missing symbols
**Solution**: SocketsHpp is header-only. Link platform libraries:
```cmake
# Windows
target_link_libraries(your-target PRIVATE ws2_32)

# Linux
target_link_libraries(your-target PRIVATE pthread)
```

## Testing

The port includes an example demonstrating consumption:
```bash
cd examples/11-vcpkg-consumption
./setup-windows.ps1  # Windows
./setup-linux.sh     # Linux/macOS
```

This example:
1. Installs SocketsHpp via overlay ports
2. Builds a simple HTTP server
3. Runs the server on http://localhost:9000
4. Demonstrates `find_package()` integration

## License

The vcpkg port follows SocketsHpp's licensing (Apache-2.0).

## Contributing

To improve the port:
1. Test on your platform/configuration
2. Report issues via GitHub Issues
3. Submit PRs with fixes/improvements
4. Update documentation as needed

## Resources

- [vcpkg Documentation](https://vcpkg.io/en/docs/README.html)
- [Creating Ports](https://vcpkg.io/en/docs/examples/packaging-github-repos.html)
- [Manifest Mode](https://vcpkg.io/en/docs/users/manifests.html)
- [Versioning](https://vcpkg.io/en/docs/users/versioning.html)
- [Overlay Ports](https://vcpkg.io/en/docs/specifications/ports-overlay.html)

## Maintainer Notes

### Updating Dependencies

When SocketsHpp updates its dependencies:
1. Update `vcpkg.json` dependencies array
2. Test on all supported platforms
3. Update this README if new features are added

### Release Checklist

Before creating a new release:
- [ ] Update version in `vcpkg.json`
- [ ] Update `REF` in `portfile.cmake` to new tag/commit
- [ ] Test installation on Windows, Linux, macOS
- [ ] Verify examples still build
- [ ] Update `SHA512` hash (vcpkg will calculate on first install)
- [ ] Document breaking changes in release notes
- [ ] Create GitHub release

### Submitting to Official vcpkg Registry (Optional)

If SocketsHpp becomes widely used, we may submit it to the official vcpkg registry:

1. Fork [microsoft/vcpkg](https://github.com/microsoft/vcpkg)
2. Copy port files to `ports/socketshpp/`
3. Run `./vcpkg x-add-version socketshpp` to update version database
4. Submit PR with title `[socketshpp] new port`
5. Pass vcpkg CI checks and code review

**Note**: This is NOT required for users to consume SocketsHpp - overlay ports work perfectly for local/private use!

### CI/CD Integration

Consider adding to your CI:
```yaml
# GitHub Actions example
- name: Install via vcpkg
  run: |
    vcpkg install socketshpp --overlay-ports=./ports
    
- name: Build example
  run: |
    cd examples/11-vcpkg-consumption
    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
    cmake --build build
```

## Future Enhancements

Potential improvements:
- [ ] Add to official vcpkg registry
- [ ] Support more platforms (FreeBSD, Android NDK)
- [ ] Add build features for optional components
- [ ] Create additional examples for specific use cases
- [ ] Automated testing via vcpkg CI
