#!/bin/sh
# The PC joining a LAN match, end to end on this machine: megamod-fakehost
# (bots, gibbed kills, blasts, props) and megamod-join rendering offscreen.
#   scripts/test_join.sh <map.oalmap> [out.ppm]
# Exits non-zero unless the joiner connected, was given its unit and saw a
# gibbed kill. With a Trial map, add HTA_MAP=... to draw Spartans instead of
# stand-ins and send the real map check.
set -e
cd "$(dirname "$0")/.."
oal=${1:?usage: scripts/test_join.sh <map.oalmap> [out.ppm]}
shot=${2:-scratch/join.ppm}
mkdir -p "$(dirname "$shot")"
cmake --build build-host --target megamod-join megamod-fakehost >/dev/null
port=$((32400 + $$ % 500))
./build-host/megamod-fakehost --port "$port" --bots 4 --props 40 --seconds 14 > scratch/fakehost.log 2>&1 &
host=$!
sleep 0.5
set -- --oalmap "$oal" --preset high --shot "$shot" --auto 9
[ -n "$HTA_MAP" ] && set -- "$@" --map "$HTA_MAP"
out=$(./build-host/megamod-join 127.0.0.1 "$port" "$@" 2>&1) || { echo "$out"; kill $host 2>/dev/null; exit 1; }
wait $host || true
echo "$out" | tail -4
echo "$out" | grep -q "join: connected" || { echo "FAIL: not connected"; exit 1; }
echo "$out" | grep -qE "[1-9][0-9]* kills \([1-9]" || { echo "FAIL: no gibbed kill seen"; exit 1; }
echo "join test OK: $shot"
