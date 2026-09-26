#!/bin/sh
# X1 end to end on this machine (docs/WORLD_ENTITIES.md): Open Asset Lab
# builds the original X1 world, megamod-match hosts it headless with the
# shared session, and megamod-join plays it offscreen.
#
#   scripts/test_x1.sh            (HTA_TRIAL_DIR or HTA_MAP for the Trial;
#                                  OAL_DIR for Open Asset Lab, default
#                                  ../open-asset-lab)
#
# 1. Joiner A walks into the closed door (blocked), presses the button
#    (button -> relay -> door on the host), walks through the doorway and
#    onto the trigger, and is teleported by the host.
# 2. Joiner B joins late, after the door opened: it must see it open at
#    once (state, not replayed events) and walk through the doorway.
# 3. Joiner C brings a different X1 package (a faster door): refused by the
#    v10 map check before it gets a player.
set -e
cd "$(dirname "$0")/.."
trial=${HTA_TRIAL_DIR:-$(dirname "${HTA_MAP:-/nonexistent/x}")}
[ -f "$trial/bloodgulch.map" ] || { echo "SKIP: no Trial maps (HTA_TRIAL_DIR or HTA_MAP)"; exit 0; }
oal=${OAL_DIR:-../open-asset-lab}
py=$oal/.venv/bin/python; [ -x "$py" ] || py=python3
[ -d "$oal/assetlab" ] || { echo "SKIP: no Open Asset Lab at $oal (OAL_DIR)"; exit 0; }
out=scratch/x1; rm -rf "$out"; mkdir -p "$out/bundle" "$out/other"
(cd "$oal" && "$py" -m assetlab fixture x1_event_lab --output "$OLDPWD/$out/bundle/x1_event_lab.oalmap") > "$out/fixture.json"
(cd "$oal" && "$py" -c "
import sys
from assetlab.fixtures import x1_event_lab, eid
from assetlab.world import compile_world
w = x1_event_lab()
next(e for e in w.entities if e.id == eid('door_main')).speed = 2.0
compile_world(w, sys.argv[1])" "$OLDPWD/$out/other/x1_event_lab.oalmap")
build=${BUILD:-build-host}
cmake --build "$build" --target megamod-join megamod-match >/dev/null
port=$((33000 + $$ % 500))
export HTA_TRIAL_DIR="$trial"
./"$build"/megamod-match --bundle "$out/bundle" --world x1_event_lab --bots 0 --seconds 50 \
    --host "$port" > "$out/host.log" 2>&1 &
host=$!
trap 'kill $host 2>/dev/null || true' EXIT
sleep 1.5
join() {
    timeout 50 ./"$build"/megamod-join 127.0.0.1 "$port" --world x1_event_lab \
        --map "$trial/bloodgulch.map" --preset low "$@" 2>&1
}
fail() { echo "FAIL: $1"; echo "--- host"; grep -E "world|net" "$out/host.log" | tail -20; exit 1; }

a=$(join --bundle "$out/bundle" --shot "$out/a.ppm" --auto 40 \
    --route "-1.0,0;B0.9,0;-0.6,-1.3;L-0.14,-1.3;w0.3;E;w2;-1.0,0;0.9,0;2.9,0;w1.5") || { echo "$a"; fail "joiner A"; }
echo "$a" > "$out/a.log"
echo "$a" | grep -q "blocked at .* as expected" || fail "the closed door did not block A"
echo "$a" | grep -q "mover x1:entity/door_main open (t 1.00" || fail "A did not see the door open"
echo "$a" | grep -q "moved by the host 1 time" || fail "A was not teleported exactly once"
grep -q "unit .* used x1:entity/button_main" "$out/host.log" || fail "the host saw no button use"
grep -q "teleported to (-4.25 2.40 0.40)" "$out/host.log" || fail "the host did not teleport"

b=$(join --bundle "$out/bundle" --shot "$out/b.ppm" --auto 20 --route "w0.5;-1.0,0;0.9,0;1.6,0") || { echo "$b"; fail "late joiner B"; }
echo "$b" > "$out/b.log"
echo "$b" | grep -q "mover x1:entity/door_main open (t 1.00" || fail "B did not see the open door"
echo "$b" | grep -q "moved by the host 0 time" || fail "B was moved"
echo "$b" | grep -q "route done" || fail "B could not walk through the open doorway"

c=$(join --bundle "$out/other" --auto 4 || true)
echo "$c" > "$out/c.log"
echo "$c" | grep -q "REFUSED (not the host's map)" || fail "a different X1 package was not refused"

wait $host 2>/dev/null || true
trap - EXIT
grep -q "refused a joiner: different map" "$out/host.log" || fail "the host did not log the refusal"
echo "$a" | grep -E "use at|blocked|moved|mover" | sed 's/^/  A /'
echo "$b" | grep -E "moved|mover" | sed 's/^/  B /'
echo "  C $(echo "$c" | grep REFUSED)"
grep -E "match: world entities|teleports" "$out/host.log" | sed 's/^/  host /'
echo "X1 test OK ($out)"
