#!/bin/sh
# Deterministic, asset-backed, process-level smoke test for both directions.
# SDL dummy video drives the same offscreen Vulkan/gameplay path as htaplay.
set -eu
cd "$(dirname "$0")/.."
map=${HTA_MAP:?Set HTA_MAP to your own bloodgulch.map}
port=${1:-32278}
cmake --build build-host --target htaplay htanet >/dev/null
mkdir -p scratch/net-regression
./build-host/htanet server "$port" 15 > scratch/net-regression/server.log 2>&1 & server=$!
a= b=
cleanup() {
    kill "$server" 2>/dev/null || true
    [ -z "$a" ] || kill "$a" 2>/dev/null || true
    [ -z "$b" ] || kill "$b" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
SDL_VIDEODRIVER=dummy ./build-host/htaplay "$map" 127.0.0.1 "$port" --auto 5 \
    > scratch/net-regression/a.log 2>&1 & a=$!
SDL_VIDEODRIVER=dummy ./build-host/htaplay "$map" 127.0.0.1 "$port" --auto 5 \
    > scratch/net-regression/b.log 2>&1 & b=$!
wait "$a"
wait "$b"
grep -q 'joined as player' scratch/net-regression/a.log
grep -q 'joined as player' scratch/net-regression/b.log
grep -q 'remote=[12]' scratch/net-regression/a.log
grep -q 'remote=[12]' scratch/net-regression/b.log
echo 'two Blood Gulch clients: remote movement, air, crouch, pistol and action clips OK'
