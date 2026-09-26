#!/usr/bin/env bash
# Run both interop directions between SocketsHpp and the official MCP TypeScript SDK:
#   1. TypeScript SDK client  -> C++ MCPServer   (ts/client.ts -> cpp_server)
#   2. C++ MCPClient          -> TypeScript SDK server   (cpp_client -> ts/server.ts)
# Exits non-zero if either direction fails. Used by CI.
#
# Usage: ./run-interop.sh [dir containing cpp_server and cpp_client]
#   Without an argument the C++ programs are built from the repository root into
#   build-interop/ (requires CMake and a C++17 compiler); Node.js >= 18 is required.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CPP_PORT="${CPP_PORT:-3000}"
TS_PORT="${TS_PORT:-3001}"

if [ $# -ge 1 ]; then
    BIN_DIR="$(cd "$1" && pwd)"
else
    echo "== Building the C++ programs"
    cmake -S "$ROOT" -B "$ROOT/build-interop" -DBUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release > /dev/null
    cmake --build "$ROOT/build-interop" --target cpp_server cpp_client --parallel
    BIN_DIR="$ROOT/build-interop/examples/10-typescript-interop"
fi

echo "== Installing the TypeScript dependencies"
(cd "$HERE/ts" && npm ci --no-audit --no-fund)

PIDS=()
cleanup() {
    for pid in "${PIDS[@]:-}"; do
        [ -n "$pid" ] && kill "$pid" 2> /dev/null || true
    done
}
trap cleanup EXIT

wait_for_port() {  # wait_for_port <port>
    for _ in $(seq 1 100); do
        if (exec 3<> "/dev/tcp/127.0.0.1/$1") 2> /dev/null; then
            return 0
        fi
        sleep 0.1
    done
    echo "timed out waiting for port $1" >&2
    return 1
}

echo
echo "== 1. TypeScript SDK client -> C++ MCPServer"
"$BIN_DIR/cpp_server" "$CPP_PORT" &
PIDS+=($!)
wait_for_port "$CPP_PORT"
(cd "$HERE/ts" && ./node_modules/.bin/tsx client.ts "http://127.0.0.1:$CPP_PORT/mcp")

echo
echo "== 2. C++ MCPClient -> TypeScript SDK server"
# exec tsx directly (not via npx) so the kill in cleanup reaches it; tsx forwards
# SIGTERM to its node child.
(cd "$HERE/ts" && exec ./node_modules/.bin/tsx server.ts "$TS_PORT") &
PIDS+=($!)
wait_for_port "$TS_PORT"
"$BIN_DIR/cpp_client" "http://127.0.0.1:$TS_PORT/mcp"

echo
echo "== Interop OK in both directions"
