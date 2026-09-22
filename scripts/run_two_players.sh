#!/bin/sh
# Two interactive Blood Gulch windows and one headless LAN server.
# Requires the owner's own map and bitmaps.map alongside it. SDL2 + Vulkan.
set -eu
cd "$(dirname "$0")/.."
map=${HTA_MAP:?Set HTA_MAP to your own bloodgulch.map}
port=${1:-32270}
cmake --build build-host --target htanet htaplay >/dev/null
mkdir -p scratch/net-live
./build-host/htanet server "$port" 86400 >scratch/net-live/server.log 2>&1 & server=$!
a= b=
cleanup() {
    kill "$server" 2>/dev/null || true
    [ -z "$a" ] || kill "$a" 2>/dev/null || true
    [ -z "$b" ] || kill "$b" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
echo "Blood Gulch LAN: two windows on UDP $port; logs in scratch/net-live/"
echo "WASD move, mouse look, Space jump, Ctrl crouch, 1/2 weapon, click fire/melee, G grenade, F1 release mouse, Esc quit."
./build-host/htaplay "$map" 127.0.0.1 "$port" --position 40 90 \
    >scratch/net-live/a.log 2>&1 & a=$!
./build-host/htaplay "$map" 127.0.0.1 "$port" --position 900 90 \
    >scratch/net-live/b.log 2>&1 & b=$!
wait "$a"
wait "$b"
