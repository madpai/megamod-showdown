#!/bin/bash
# X3 end to end on this machine (docs/SCRIPTING.md): Open Asset Lab packs
# the original x3_script_lab with two Lua scripts, megamod-match hosts it
# headless (the only place a script runs), megamod-join clients play it.
#
#   scripts/test_x3.sh            (HTA_TRIAL_DIR or HTA_MAP for the Trial;
#                                  OAL_DIR for Open Asset Lab, default
#                                  ../open-asset-lab; BUILD, default build-host)
#
# 1. Target T joins and stands near the scripted button.
# 2. Joiner A: door A shut on joining and blocks; A presses the scripted
#    button (no links: the host's Lua on_used asks world.send(door_a,
#    'open') through the world-event queue); A presses ability: the host's
#    Lua on_ability picks T with game.near and asks game.damage -- the
#    native pipeline kills T and credits A. A walks through doorway A.
# 3. Late joiner B finds door A open, B and C shut (state, not script
#    history; B runs no script).
# 4. Packages whose script differs by one character, or only by a comment,
#    are refused before spawn; a provenance-only difference is admitted.
set -e
cd "$(dirname "$0")/.."
trial=${HTA_TRIAL_DIR:-$(dirname "${HTA_MAP:-/nonexistent/x}")}
[ -f "$trial/bloodgulch.map" ] || { echo "SKIP: no Trial maps (HTA_TRIAL_DIR or HTA_MAP)"; exit 0; }
oal=${OAL_DIR:-../open-asset-lab}; [ -d "$oal" ] && oal=$(cd "$oal" && pwd)
py=$oal/.venv/bin/python; [ -x "$py" ] || py=python3
[ -d "$oal/assetlab" ] || { echo "SKIP: no Open Asset Lab at $oal (OAL_DIR)"; exit 0; }
out=scratch/x3; rm -rf "$out"; mkdir -p "$out/bundle" "$out/mod" "$out/comment" "$out/prov"
here=$PWD
(cd "$oal" && "$py" -m assetlab fixture x3_script_lab --output "$here/$out/bundle/x3_script_lab.oalmap") > "$out/fixture.json"
(cd "$oal" && "$py" - "$here/$out" <<'EOF'
import struct, sys
from pathlib import Path
from assetlab.fixtures import x3_script_lab
from assetlab.package import manifest_json, read_manifest
from assetlab.world import compile_world
out = Path(sys.argv[1])
def variant(name, edit):
    w = x3_script_lab()
    s = next(s for s in w.scripts if s.id == 'x3:script/pulse_ability')
    s.source = edit(s.source)
    compile_world(w, out / name / 'x3_script_lab.oalmap')
variant('mod', lambda src: src.replace('local DAMAGE = 150', 'local DAMAGE = 151'))     # one character
variant('comment', lambda src: src + '-- a comment\n')                                   # a comment only
base = (out / 'bundle/x3_script_lab.oalmap').read_bytes()
ml = struct.unpack_from('<I', base, 8)[0]
m = read_manifest(out / 'bundle/x3_script_lab.oalmap')
m.update(source_provenance='rebuilt elsewhere', importer_version='original_world-9.9.9')
mb = manifest_json(m)
(out / 'prov/x3_script_lab.oalmap').write_bytes(base[:8] + struct.pack('<I', len(mb)) + base[12:64] + mb + base[64 + ml:])
EOF
)
oal_key=$(cd "$oal" && "$py" -m assetlab world-key "$here/$out/bundle/x3_script_lab.oalmap" | sed -n 's/.*"world_key": "\(.*\)".*/\1/p')
build=${BUILD:-build-host}
cmake --build "$build" --target megamod-join megamod-match megamod-content >/dev/null
fail() { echo "FAIL: $1"; echo "--- host"; grep -E "script|world|net|killed" "$out/host.log" | tail -30; exit 1; }
for v in bundle mod comment prov; do
    ./"$build"/megamod-content --trial "$trial" --bundle "$out/$v" --world x3_script_lab > "$out/content-$v.log" 2>&1 ||
        { cat "$out/content-$v.log"; fail "the engine could not load the $v package"; }
done
key() { sed -n 's/.* key \([0-9a-f]*\) .*/\1/p' "$out/content-$1.log"; }
[ -n "$oal_key" ] && [ "$(key bundle)" = "$oal_key" ] || fail "OAL's world key ($oal_key) is not the engine's ($(key bundle))"
[ "$(key prov)" = "$(key bundle)" ] || fail "a provenance-only change changed the world key"
[ "$(key mod)" != "$(key bundle)" ] || fail "a one-character script change kept the world key"
[ "$(key comment)" != "$(key bundle)" ] || fail "a comment-only script change kept the world key (it should not: script bytes count)"

port=$((34000 + $$ % 400))
export HTA_TRIAL_DIR="$trial"
J=(./"$build"/megamod-join 127.0.0.1 "$port" --world x3_script_lab --map "$trial/bloodgulch.map" --preset low)
./"$build"/megamod-match --bundle "$out/bundle" --world x3_script_lab --bots 0 --seconds 120 \
    --host "$port" > "$out/host.log" 2>&1 &
host=$!
trap 'kill $host $jt 2>/dev/null || true' EXIT
sleep 1.5
# T: stands within the pulse's reach of where A will press the button.
timeout 110 "${J[@]}" --bundle "$out/bundle" --auto 100 --route "-1.2,-3.3;w50" > "$out/t.log" 2>&1 &
jt=$!
i=0
until grep -q "on joining" "$out/t.log" 2>/dev/null; do
    i=$((i + 1)); [ $i -lt 1200 ] || fail "target T never joined"; sleep 0.1
done
sleep 4                                       # T walks to its spot (and past spawn protection)
a=$(timeout 90 "${J[@]}" --bundle "$out/bundle" --shot "$out/a.ppm" --auto 80 \
    --route "B0.9,-3;-1.8,-4.6;-0.6,-4.2;L-0.14,-4.2;w0.3;E;w1;Q;w2;-1.0,-3;0.9,-3;w1") || { echo "$a"; fail "joiner A"; }
echo "$a" > "$out/a.log"
b=$(timeout 60 "${J[@]}" --bundle "$out/bundle" --auto 8 --route "w2") || { echo "$b"; fail "late joiner B"; }
echo "$b" > "$out/b.log"
m=$(timeout 30 "${J[@]}" --bundle "$out/mod" --auto 4 || true); echo "$m" > "$out/c-mod.log"
c=$(timeout 30 "${J[@]}" --bundle "$out/comment" --auto 4 || true); echo "$c" > "$out/c-comment.log"
p=$(timeout 30 "${J[@]}" --bundle "$out/prov" --auto 4 || true); echo "$p" > "$out/c-prov.log"
wait $jt || true
jt=
t=$(cat "$out/t.log")
wait $host 2>/dev/null || true
trap - EXIT

echo "$a" | grep -q "on joining mover x3:entity/door_a closed (t 0.00" || fail "A did not find door A shut"
echo "$a" | grep -q "blocked at .* as expected" || fail "door A did not block A before the script opened it"
echo "$a" | grep -q "at the end: mover x3:entity/door_a open (t 1.00" || fail "A does not end with door A open"
echo "$a" | grep -q "at the end: mover x3:entity/door_b closed (t 0.00" || fail "door B moved"
echo "$a" | grep -q "route done" || fail "A could not walk through doorway A"
grep -q "\[script\] 2 scripts loaded (megamod.v1, host only)" "$out/host.log" || fail "the host did not load the scripts"
grep -q "\[script\] x3:script/button_logic on_used(x3:entity/button_script, unit [0-9]*) phase host.world.script tick [0-9]*: ok, 1 requests" "$out/host.log" ||
    fail "the host's on_used did not run and request"
grep -q "\[script\] x3:script/pulse_ability: pulse by player [0-9]* hit 1" "$out/host.log" || fail "the pulse hit nobody"
grep -q "\[script\] x3:script/pulse_ability on_ability(unit [0-9]*) phase host.world.script tick [0-9]*: ok, 1 requests" "$out/host.log" ||
    fail "the host's on_ability did not run and request"
grep -qE "was killed by" "$out/host.log" || fail "no native kill from the scripted damage"
echo "$t" | grep -q "the host says we were hurt" || fail "T never saw its own health drop"
echo "$t" | grep -q "the host says we died" || fail "T never saw itself die"
for who in a b t; do grep -q "\[script\]" "$out/$who.log" && fail "joiner $who ran or logged a script"; done
echo "$b" | grep -q "on joining mover x3:entity/door_a open (t 1.00" || fail "late joiner B did not find door A open"
echo "$b" | grep -q "on joining mover x3:entity/door_b closed (t 0.00" || fail "late joiner B did not find door B shut"
echo "$m" | grep -q "REFUSED (not the host's map)" || fail "a one-character script change was not refused"
echo "$c" | grep -q "REFUSED (not the host's map)" || fail "a comment-only script change was not refused"
echo "$p" | grep -q "join: connected" || fail "a provenance-only variant was not admitted"
grep -q "match: scripts: .* 0 errors" "$out/host.log" || fail "the scripts had errors"
grep -q "match: Lua state closed, 0 bytes left" "$out/host.log" || fail "the Lua state did not give everything back"
echo "  world key $(key bundle) (OAL $oal_key); one-char $(key mod), comment $(key comment), provenance $(key prov)"
grep -E "\[script\]|killed by" "$out/host.log" | sed 's/^/  host /'
echo "$a" | grep -E "on joining mover x3:entity/door_a|blocked|use at|ability at|at the end: mover x3:entity/door_a" | sed 's/^/  A /'
echo "$t" | grep -E "hurt|died" | sed 's/^/  T /'
echo "$b" | grep -E "on joining" | sed 's/^/  B /'
echo "  mod:     $(echo "$m" | grep -E 'REFUSED|join: connected')"
echo "  comment: $(echo "$c" | grep -E 'REFUSED|join: connected')"
echo "  prov:    $(echo "$p" | grep -E 'REFUSED|join: connected')"
grep -E "match: scripts|Lua state" "$out/host.log" | sed 's/^/  host /'
echo "X3 test OK ($out)"
