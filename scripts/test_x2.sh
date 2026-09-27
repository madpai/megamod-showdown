#!/bin/sh
# X2 end to end on this machine (docs/WORLD_ENTITIES.md): one reusable
# mover definition placed three times, hosted headless by megamod-match and
# played by two megamod-join clients at once; then the world key through
# the real host/join check.
#
#   scripts/test_x2.sh            (HTA_TRIAL_DIR or HTA_MAP for the Trial;
#                                  OAL_DIR for Open Asset Lab, default
#                                  ../open-asset-lab)
#
# 1. Joiner A: both doors shut on joining; walks into door B (blocked),
#    presses button A (A opens, B stays shut), walks through doorway A and
#    onto the trigger (teleported once), then stays connected.
# 2. Joiner B joins after door A opened: on joining it has A open, B shut,
#    C shut. It presses button B and walks through doorway B.
# 3. Both end with A open, B open, C shut; so does the host.
# 4. The world key: OAL's and the engine's agree; a package with a changed
#    mover DEFINITION and one with changed GEOMETRY but a byte-identical
#    manifest are refused before spawn; a package that differs only in
#    provenance is admitted.
set -e
cd "$(dirname "$0")/.."
trial=${HTA_TRIAL_DIR:-$(dirname "${HTA_MAP:-/nonexistent/x}")}
[ -f "$trial/bloodgulch.map" ] || { echo "SKIP: no Trial maps (HTA_TRIAL_DIR or HTA_MAP)"; exit 0; }
oal=${OAL_DIR:-../open-asset-lab}; [ -d "$oal" ] && oal=$(cd "$oal" && pwd)
py=$oal/.venv/bin/python; [ -x "$py" ] || py=python3
[ -d "$oal/assetlab" ] || { echo "SKIP: no Open Asset Lab at $oal (OAL_DIR)"; exit 0; }
out=scratch/x2; rm -rf "$out"; mkdir -p "$out/bundle" "$out/def" "$out/geom" "$out/prov"
here=$PWD
(cd "$oal" && "$py" -m assetlab fixture x2_definition_lab --output "$here/$out/bundle/x2_definition_lab.oalmap") > "$out/fixture.json"
(cd "$oal" && "$py" - "$here/$out" <<'EOF'
import json, struct, sys
from pathlib import Path
from assetlab.fixtures import x2_definition_lab
from assetlab.package import manifest_json, read_manifest
from assetlab.world import compile_world
out = Path(sys.argv[1])
# A different DEFINITION: the shared door is faster.
w = x2_definition_lab(); w.mover_definitions[0].speed = 2.0
compile_world(w, out / 'def/x2_definition_lab.oalmap')
# Different GEOMETRY only: button A's plate is thicker. Same manifest bytes.
w = x2_definition_lab()
b = next(b for b in w.boxes if b.material == 'button')
b.min = (-0.2, b.min[1], b.min[2])
compile_world(w, out / 'geom/x2_definition_lab.oalmap')
base = (out / 'bundle/x2_definition_lab.oalmap').read_bytes()
geom = (out / 'geom/x2_definition_lab.oalmap').read_bytes()
ml = struct.unpack_from('<I', base, 8)[0]
assert geom[64:64 + ml] == base[64:64 + ml], 'the geometry variant must keep the manifest byte-identical'
assert geom != base
# PROVENANCE only: the same world, other source notes and importer.
m = read_manifest(out / 'bundle/x2_definition_lab.oalmap')
m.update(source_provenance='rebuilt elsewhere, /home/someone/worlds', importer_version='original_world-9.9.9',
         conversion_warnings=['imported on another machine'])
mb = manifest_json(m)
(out / 'prov/x2_definition_lab.oalmap').write_bytes(base[:8] + struct.pack('<I', len(mb)) + base[12:64] + mb + base[64 + ml:])
EOF
)
oal_key=$(cd "$oal" && "$py" -m assetlab world-key "$here/$out/bundle/x2_definition_lab.oalmap" | sed -n 's/.*"world_key": "\(.*\)".*/\1/p')
build=${BUILD:-build-host}
cmake --build "$build" --target megamod-join megamod-match megamod-content >/dev/null
fail() { echo "FAIL: $1"; echo "--- host"; grep -E "world|net" "$out/host.log" | tail -25; exit 1; }
for v in bundle def geom prov; do
    ./"$build"/megamod-content --trial "$trial" --bundle "$out/$v" --world x2_definition_lab > "$out/content-$v.log" 2>&1 ||
        { cat "$out/content-$v.log"; fail "the engine could not load the $v package"; }
done
key() { sed -n 's/.* key \([0-9a-f]*\) .*/\1/p' "$out/content-$1.log"; }
[ -n "$oal_key" ] && [ "$(key bundle)" = "$oal_key" ] || fail "OAL's world key ($oal_key) is not the engine's ($(key bundle))"
[ "$(key prov)" = "$(key bundle)" ] || fail "a provenance-only change changed the world key"
[ "$(key def)" != "$(key bundle)" ] || fail "a definition change kept the world key"
[ "$(key geom)" != "$(key bundle)" ] || fail "a geometry change kept the world key"

port=$((33500 + $$ % 400))
export HTA_TRIAL_DIR="$trial"
./"$build"/megamod-match --bundle "$out/bundle" --world x2_definition_lab --bots 0 --seconds 75 \
    --host "$port" > "$out/host.log" 2>&1 &
host=$!
trap 'kill $host $ja 2>/dev/null || true' EXIT
sleep 1.5
join() {
    timeout 75 ./"$build"/megamod-join 127.0.0.1 "$port" --world x2_definition_lab \
        --map "$trial/bloodgulch.map" --preset low "$@" 2>&1
}
# A: stays connected (the last wait) while B joins and opens door B.
join --bundle "$out/bundle" --shot "$out/a.ppm" --auto 60 \
    --route "-1.0,0;B0.9,0;-0.6,-4.2;L-0.14,-4.2;w0.3;E;w2;-1.0,-3;0.9,-3;3.0,3.7;w1;w26" > "$out/a.log" &
ja=$!
i=0
until grep -q "used x2:entity/button_a" "$out/host.log"; do
    i=$((i + 1)); [ $i -lt 400 ] || fail "the host never saw button A used"; sleep 0.1
done
sleep 2.5                                     # door A fully open (1.25 wu at 1 wu/s)
b=$(join --bundle "$out/bundle" --shot "$out/b.ppm" --auto 30 \
    --route "w0.5;-0.6,-1.3;L-0.14,-1.3;w0.3;E;w2;-1.0,0;0.9,0;w1") || { echo "$b"; fail "late joiner B"; }
echo "$b" > "$out/b.log"
wait $ja || { cat "$out/a.log"; fail "joiner A"; }
ja=
a=$(cat "$out/a.log")
echo "$a" | grep -q "on joining mover x2:entity/door_a closed (t 0.00" || fail "A did not find door A shut"
echo "$a" | grep -q "on joining mover x2:entity/door_b closed (t 0.00" || fail "A did not find door B shut"
echo "$a" | grep -q "blocked at .* as expected" || fail "door B did not block A"
echo "$a" | grep -q "moved by the host 1 time" || fail "A was not teleported exactly once"
echo "$b" | grep -q "on joining mover x2:entity/door_a open (t 1.00" || fail "late joiner B did not find door A open"
echo "$b" | grep -q "on joining mover x2:entity/door_b closed (t 0.00" || fail "late joiner B did not find door B shut"
echo "$b" | grep -q "on joining mover x2:entity/door_c closed (t 0.00" || fail "late joiner B did not find door C shut"
echo "$b" | grep -q "route done" || fail "B could not walk through doorway B"
for who in a b; do
    log=$(cat "$out/$who.log")
    echo "$log" | grep -q "at the end: mover x2:entity/door_a open (t 1.00" || fail "$who does not end with door A open"
    echo "$log" | grep -q "at the end: mover x2:entity/door_b open (t 1.00" || fail "$who does not end with door B open"
    echo "$log" | grep -q "at the end: mover x2:entity/door_c closed (t 0.00" || fail "$who does not end with door C shut"
done
grep -q "unit .* used x2:entity/button_b" "$out/host.log" || fail "the host saw no use of button B"

c=$(join --bundle "$out/def" --auto 4 || true); echo "$c" > "$out/c-def.log"
echo "$c" | grep -q "REFUSED (not the host's map)" || fail "a package with a different mover definition was not refused"
g=$(join --bundle "$out/geom" --auto 4 || true); echo "$g" > "$out/c-geom.log"
echo "$g" | grep -q "REFUSED (not the host's map)" || fail "a package with different geometry was not refused"
p=$(join --bundle "$out/prov" --auto 4 || true); echo "$p" > "$out/c-prov.log"
echo "$p" | grep -q "join: connected" || fail "a provenance-only variant was not admitted"

wait $host 2>/dev/null || true
trap - EXIT
[ "$(grep -c "refused a joiner: different map" "$out/host.log")" -ge 2 ] || fail "the host did not log both refusals"
grep -q "match: mover x2:entity/door_a phase 2 t 1.00" "$out/host.log" || fail "the host does not end with door A open"
grep -q "match: mover x2:entity/door_b phase 2 t 1.00" "$out/host.log" || fail "the host does not end with door B open"
grep -q "match: mover x2:entity/door_c phase 0 t 0.00" "$out/host.log" || fail "the host does not end with door C shut"
echo "  world key $(key bundle) (OAL $oal_key); definition variant $(key def), geometry variant $(key geom), provenance variant $(key prov)"
echo "$a" | grep -E "on joining|blocked|moved|at the end" | sed 's/^/  A /'
echo "$b" | grep -E "on joining|at the end" | sed 's/^/  B /'
echo "  def:  $(echo "$c" | grep -E "REFUSED|connected")"
echo "  geom: $(echo "$g" | grep -E "REFUSED|connected")"
echo "  prov: $(echo "$p" | grep -E "REFUSED|connected")"
grep -E "match: world entities|match: mover" "$out/host.log" | sed 's/^/  host /'
echo "X2 test OK ($out)"
