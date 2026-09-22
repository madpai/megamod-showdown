#!/bin/sh
# Two interactive Blood Gulch windows and one headless LAN server.
# Requires the owner's own map and bitmaps.map alongside it. SDL2 + Vulkan.
set -eu
cd "$(dirname "$0")/.."
map=${HTA_MAP:?Set HTA_MAP to your own bloodgulch.map}
port=${1:-32270}
cmake --build build-host --target htanet htaplay >/dev/null
./build-host/htanet server "$port" 86400 & server=$!
a= b=
cleanup() {
    kill "$server" 2>/dev/null || true
    [ -z "$a" ] || kill "$a" 2>/dev/null || true
    [ -z "$b" ] || kill "$b" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
./build-host/htaplay "$map" 127.0.0.1 "$port" & a=$!
./build-host/htaplay "$map" 127.0.0.1 "$port" & b=$!
wait "$a"
wait "$b"
