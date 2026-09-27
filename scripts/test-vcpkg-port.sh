#!/usr/bin/env bash
# Copyright Max Golovanov.
# SPDX-License-Identifier: Apache-2.0
#
# Checks the vcpkg port in ports/socketshpp and the registry database in versions/:
#   1. versions/s-/socketshpp.json records the committed git tree of ports/socketshpp
#   2. overlay mode:  examples/11-vcpkg-consumption builds with VCPKG_OVERLAY_PORTS=ports
#   3. registry mode: a consumer that lists this repository as a git registry
#                     (baseline = HEAD) builds the same program
# Each consumer is started and must answer GET /info.
#
# Usage: scripts/test-vcpkg-port.sh [vcpkg root]
#   The vcpkg root defaults to $VCPKG_ROOT, then $VCPKG_INSTALLATION_ROOT (GitHub
#   runners). It must be a git clone of microsoft/vcpkg (used as the default registry
#   in registry mode). EXTRA_OVERLAY_PORTS may name additional overlay directories.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_ROOT="${1:-${VCPKG_ROOT:-${VCPKG_INSTALLATION_ROOT:-}}}"
if [ -z "$VCPKG_ROOT" ] || [ ! -x "$VCPKG_ROOT/vcpkg" ]; then
    echo "error: vcpkg not found; pass its root directory or set VCPKG_ROOT" >&2
    exit 1
fi
TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
WORK="$(mktemp -d)"
PORT=9000
PIDS=()
cleanup() {
    for pid in "${PIDS[@]:-}"; do [ -n "$pid" ] && kill "$pid" 2> /dev/null || true; done
    rm -rf "$WORK"
}
trap cleanup EXIT

echo "== 1. Registry database matches the committed port"
committed="$(git -C "$ROOT" rev-parse HEAD:ports/socketshpp)"
recorded="$(sed -n 's/.*"git-tree": *"\([0-9a-f]*\)".*/\1/p' "$ROOT/versions/s-/socketshpp.json" | head -1)"
if [ "$committed" != "$recorded" ]; then
    echo "error: versions/s-/socketshpp.json records git-tree $recorded," >&2
    echo "       but ports/socketshpp at HEAD is $committed. Run:" >&2
    echo "       vcpkg x-add-version socketshpp --x-builtin-ports-root=ports --x-builtin-registry-versions-dir=versions" >&2
    exit 1
fi
if ! git -C "$ROOT" diff --quiet HEAD -- ports versions; then
    echo "warning: ports/ or versions/ has uncommitted changes; registry mode uses HEAD" >&2
fi
echo "git-tree $committed OK"

run_consumer() {  # run_consumer <build dir>
    "$1/vcpkg-consumer" &
    PIDS+=($!)
    for _ in $(seq 1 50); do
        if curl -fsS -m 2 "http://127.0.0.1:$PORT/info" > "$WORK/info.json" 2> /dev/null; then
            cat "$WORK/info.json"; echo
            kill "${PIDS[-1]}"; wait "${PIDS[-1]}" 2> /dev/null || true
            return 0
        fi
        sleep 0.2
    done
    echo "error: consumer did not answer on port $PORT" >&2
    return 1
}

overlays="$ROOT/ports${EXTRA_OVERLAY_PORTS:+;$EXTRA_OVERLAY_PORTS}"

echo
echo "== 2. Overlay port (examples/11-vcpkg-consumption)"
cmake -S "$ROOT/examples/11-vcpkg-consumption" -B "$WORK/overlay" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DVCPKG_OVERLAY_PORTS="$overlays" \
    -DVCPKG_INSTALLED_DIR="$WORK/overlay/vcpkg_installed" -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/overlay"
run_consumer "$WORK/overlay"

echo
echo "== 3. Git registry (this repository at HEAD)"
consumer="$WORK/registry-src"
mkdir -p "$consumer"
cp "$ROOT/examples/11-vcpkg-consumption/main.cpp" "$ROOT/examples/11-vcpkg-consumption/CMakeLists.txt" "$consumer/"
cat > "$consumer/vcpkg.json" << JSON
{ "name": "registry-consumer", "version": "1.0.0", "dependencies": [ "socketshpp" ] }
JSON
extra=""
if [ -n "${EXTRA_OVERLAY_PORTS:-}" ]; then
    extra=", \"overlay-ports\": [ \"${EXTRA_OVERLAY_PORTS//;/\", \"}\" ]"
fi
cat > "$consumer/vcpkg-configuration.json" << JSON
{
  "default-registry": { "kind": "git", "repository": "$VCPKG_ROOT", "baseline": "$(git -C "$VCPKG_ROOT" rev-parse HEAD)" },
  "registries": [
    { "kind": "git", "repository": "$ROOT", "baseline": "$(git -C "$ROOT" rev-parse HEAD)", "packages": [ "socketshpp" ] }
  ]$extra
}
JSON
cmake -S "$consumer" -B "$WORK/registry" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/registry"
run_consumer "$WORK/registry"

echo
echo "== vcpkg port OK (overlay and registry)"
