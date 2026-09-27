#!/bin/bash
# X4 end to end on this machine (docs/RESOURCES.md): resource identity and a
# package dependency. Open Asset Lab builds x4_resource_lab -- a DECLARED
# world package (x4.resource_lab) whose button runs its own script
# (x4:script/open_door) and whose ability script is IMPORTED from a separate
# LIBRARY package (x4.shared: x4shared:script/pulse_ability). MegaMod loads
# the library beside the world by package ID, resolves every typed
# reference once, and plays exactly as X3 did.
#
#   scripts/test_x4.sh            (HTA_TRIAL_DIR or HTA_MAP for the Trial;
#                                  OAL_DIR for Open Asset Lab, default
#                                  ../open-asset-lab; BUILD, default build-host)
#
# 1. Contract: OAL's copies of the engine's contract and ID conformance
#    corpus are exactly what this build prints (no drift).
# 2. Identity: the engine's world key (world + library) equals OAL's; the
#    engine reports what resolved to what.
# 3. Refusals, each by OAL's checker AND the engine, with a message naming
#    the package, the reference and why: dependency omitted, missing
#    script, wrong resource type, malformed ID, duplicate provider, an
#    import the world does not declare, a library that declares another ID.
# 4. Play: host + target T + joiner A (button -> own script opens door A;
#    ability -> library script kills T through native damage) + late joiner
#    B (door A open); no joiner runs Lua.
# 5. Compatibility: a one-character change in the LIBRARY (world bytes
#    unchanged) is refused before spawn; a provenance-only library change
#    is admitted; a joiner without the library cannot load the world.
set -e
cd "$(dirname "$0")/.."
trial=${HTA_TRIAL_DIR:-$(dirname "${HTA_MAP:-/nonexistent/x}")}
[ -f "$trial/bloodgulch.map" ] || { echo "SKIP: no Trial maps (HTA_TRIAL_DIR or HTA_MAP)"; exit 0; }
oal=${OAL_DIR:-../open-asset-lab}; [ -d "$oal" ] && oal=$(cd "$oal" && pwd)
py=$oal/.venv/bin/python; [ -x "$py" ] || py=python3
[ -d "$oal/assetlab" ] || { echo "SKIP: no Open Asset Lab at $oal (OAL_DIR)"; exit 0; }
build=${BUILD:-build-host}
cmake --build "$build" --target megamod-join megamod-match megamod-content megamod-resources >/dev/null
out=scratch/x4; rm -rf "$out"; mkdir -p "$out"
here=$PWD
fail() { echo "FAIL: $1"; [ -f "$out/host.log" ] && { echo "--- host"; grep -E "script|world|net|killed|package" "$out/host.log" | tail -30; }; exit 1; }

# ---- 1. the contract Open Asset Lab validates against is this engine's ----
./"$build"/megamod-resources --json > "$out/contract.json"
./"$build"/megamod-resources --conformance > "$out/conformance.json"
cmp -s "$out/contract.json" "$oal/assetlab/data/megamod_resources.json" ||
    fail "OAL's assetlab/data/megamod_resources.json is not this engine's contract (megamod-resources --json > it)"
cmp -s "$out/conformance.json" "$oal/assetlab/data/megamod_id_conformance.json" ||
    fail "OAL's assetlab/data/megamod_id_conformance.json is stale (megamod-resources --conformance > it)"

# ---- the packages ------------------------------------------------------------
mkdir -p "$out/bundle"
(cd "$oal" && "$py" -m assetlab fixture x4_resource_lab --output "$here/$out/bundle/x4_resource_lab.oalmap") > "$out/fixture.json"
[ -f "$out/bundle/packages/x4.shared.oalasset" ] || fail "OAL did not write the library package"
(cd "$oal" && "$py" - "$here/$out" <<'EOF'
import json, shutil, struct, sys
from pathlib import Path
from assetlab.dependencies import compile_library
from assetlab.fixtures import x4_shared
from assetlab.package import manifest_json, read_manifest
from assetlab.scripts import Script
out = Path(sys.argv[1])
good = out / 'bundle'
world = (good / 'x4_resource_lab.oalmap').read_bytes()
ml = struct.unpack_from('<I', world, 8)[0]

def variant(name, world_bytes=world, library=None, library_bytes=None, packages=True):
    d = out / name
    (d / 'packages').mkdir(parents=True) if packages else d.mkdir(parents=True)
    (d / 'x4_resource_lab.oalmap').write_bytes(world_bytes)
    if packages and library is not None:
        compile_library(library, d / 'packages' / 'x4.shared.oalasset')
    elif packages:
        (d / 'packages' / 'x4.shared.oalasset').write_bytes(library_bytes or (good / 'packages/x4.shared.oalasset').read_bytes())

def patched(old, new):
    """The good world with its manifest edited (a package OAL would never
    write: made by hand, as a hostile or broken one would be)."""
    m = world[64:64 + ml]
    assert old.encode() in m, old
    mb = m.replace(old.encode(), new.encode(), 1)
    return world[:8] + struct.pack('<I', len(mb)) + world[12:64] + mb + world[64 + ml:]

variant('omitted', packages=False)                                                       # dependency omitted
variant('missing', patched('"script":"x4:script/open_door"', '"script":"x4:script/open_dor"'))
variant('wrongtype', patched('"script":"x4:script/open_door"', '"script":"x4:mover/basic_slide_door"'))
variant('malformed', patched('"ability_script":"x4shared:script/pulse_ability"',
                             '"ability_script":"X4shared:script/pulse_ability"'))
variant('undeclared', patched('"resources":["x4shared:script/pulse_ability"]', '"resources":[]'))
lib = x4_shared()                                                                         # a duplicate provider
lib.scripts.append(Script('x4:script/open_door', 'function on_used(e, p) end\n', ['on_used']))
variant('duplicate', library=lib)
lib = x4_shared(); lib.id = 'x4.other'                                                   # a file claiming another ID
variant('liar', library=lib)
lib = x4_shared()                                                                         # one character of library Lua
lib.scripts[0].source = lib.scripts[0].source.replace('local DAMAGE = 150', 'local DAMAGE = 151')
variant('libmod', library=lib)
lb = (good / 'packages/x4.shared.oalasset').read_bytes()                                 # library provenance only
lm = json.loads(lb[32:])
lm.update(source_provenance='rebuilt on another machine', importer_version='library-9.9.9')
mb = json.dumps(lm, sort_keys=True, separators=(',', ':')).encode()
variant('libprov', library_bytes=lb[:8] + struct.pack('<I', len(mb)) + lb[12:32] + mb)
EOF
) || fail "OAL could not build the variants"

# ---- 2. identity: engine == OAL; what resolved to what --------------------------
oal_key() { (cd "$oal" && "$py" -m assetlab world-key "$here/$out/$1/x4_resource_lab.oalmap") | sed -n 's/.*"world_key": "\(.*\)".*/\1/p'; }
eng_key() { ./"$build"/megamod-content --trial "$trial" --bundle "$out/$1" --world x4_resource_lab > "$out/content-$1.log" 2>&1 ||
                { cat "$out/content-$1.log"; fail "the engine could not load the $1 package set"; }
            sed -n 's/.* key \([0-9a-f]*\) .*/\1/p' "$out/content-$1.log"; }
kg=$(eng_key bundle); km=$(eng_key libmod); kp=$(eng_key libprov)
[ -n "$kg" ] && [ "$kg" = "$(oal_key bundle)" ] || fail "OAL's world key ($(oal_key bundle)) is not the engine's ($kg)"
[ "$km" = "$(oal_key libmod)" ] && [ "$kp" = "$(oal_key libprov)" ] || fail "OAL and the engine disagree on a variant's key"
[ "$km" != "$kg" ] || fail "a one-character change in the required library kept the world key"
[ "$kp" = "$kg" ] || fail "a provenance-only library change changed the world key"
cmp -s "$out/bundle/x4_resource_lab.oalmap" "$out/libmod/x4_resource_lab.oalmap" || fail "libmod's world bytes should be unchanged"
./"$build"/megamod-resources --bundle "$out/bundle" --world x4_resource_lab > "$out/resolved.json" || fail "megamod-resources refused the good world"
grep -q '"from": "ability_script", "field": "world_entities.ability_script", "to": "x4shared:script/pulse_ability"' "$out/resolved.json" ||
    fail "the ability script did not resolve to the library's"
grep -q '"from": "x4:entity/button_script", "field": "world_entities.entities\[\].script", "to": "x4:script/open_door", "index": 0' "$out/resolved.json" ||
    fail "the button's script did not resolve to the world's own"
grep -q '"set": \[{"package": "x4.shared", "direct": true' "$out/resolved.json" || fail "the package set is not x4.shared"
(cd "$oal" && "$py" -m assetlab resources check "$here/$out/bundle/x4_resource_lab.oalmap" "$here/$out/bundle/packages/x4.shared.oalasset") \
    > "$out/oal-check-bundle.log" || { cat "$out/oal-check-bundle.log"; fail "OAL's checker refused the good package set"; }

# ---- 3. refusals: OAL first, the engine regardless ------------------------------
refuse() {   # variant, message both must give
    local v=$1 want=$2
    (cd "$oal" && "$py" -m assetlab resources check "$here/$out/$v/x4_resource_lab.oalmap") > "$out/oal-check-$v.log" 2>&1 &&
        fail "OAL's checker accepted the $v variant"
    grep -qF -- "$want" "$out/oal-check-$v.log" || { cat "$out/oal-check-$v.log"; fail "OAL's checker did not say '$want' for $v"; }
    ./"$build"/megamod-resources --bundle "$out/$v" --world x4_resource_lab > "$out/eng-$v.json" &&
        fail "the engine accepted the $v variant"
    grep -qF -- "$want" "$out/eng-$v.json" || { cat "$out/eng-$v.json"; fail "the engine did not say '$want' for $v"; }
    ./"$build"/megamod-content --trial "$trial" --bundle "$out/$v" --world x4_resource_lab > "$out/content-$v.log" 2>&1 &&
        fail "megamod-content loaded the $v variant"
    echo "  refused $v: $want"
}
refuse omitted "package x4.resource_lab requires package x4.shared, but it is not present (looked for packages/x4.shared.oalasset)"
refuse missing "x4:entity/button_script references missing script x4:script/open_dor"
refuse wrongtype "x4:entity/button_script: script x4:mover/basic_slide_door is a mover definition, expected a script"
refuse malformed "ability_script: ability script 'X4shared:script/pulse_ability' is not a resource ID: namespace has capital 'X'"
refuse undeclared "ability script x4shared:script/pulse_ability is provided by package x4.shared, which package x4.resource_lab requires but does not import it from"
refuse duplicate "x4:script/open_door: provided by both package x4.resource_lab and package x4.shared"
refuse liar "package x4.resource_lab requires package x4.shared, but packages/x4.shared.oalasset declares package x4.other"

# ---- 4. play ------------------------------------------------------------------------
port=$((34400 + $$ % 400))
export HTA_TRIAL_DIR="$trial"
J=(./"$build"/megamod-join 127.0.0.1 "$port" --world x4_resource_lab --map "$trial/bloodgulch.map" --preset low)
./"$build"/megamod-match --bundle "$out/bundle" --world x4_resource_lab --bots 0 --seconds 120 \
    --host "$port" > "$out/host.log" 2>&1 &
host=$!
trap 'kill $host $jt 2>/dev/null || true' EXIT
sleep 1.5
timeout 110 "${J[@]}" --bundle "$out/bundle" --auto 100 --route "-1.2,-3.3;w50" > "$out/t.log" 2>&1 &
jt=$!
i=0
until grep -q "on joining" "$out/t.log" 2>/dev/null; do
    i=$((i + 1)); [ $i -lt 1200 ] || fail "target T never joined"; sleep 0.1
done
sleep 4
a=$(timeout 90 "${J[@]}" --bundle "$out/bundle" --auto 80 \
    --route "B0.9,-3;-1.8,-4.6;-0.6,-4.2;L-0.14,-4.2;w0.3;E;w1;Q;w2;-1.0,-3;0.9,-3;w1") || { echo "$a"; fail "joiner A"; }
echo "$a" > "$out/a.log"
b=$(timeout 60 "${J[@]}" --bundle "$out/bundle" --auto 8 --route "w2") || { echo "$b"; fail "late joiner B"; }
echo "$b" > "$out/b.log"
m=$(timeout 30 "${J[@]}" --bundle "$out/libmod" --auto 4 || true); echo "$m" > "$out/c-libmod.log"
p=$(timeout 30 "${J[@]}" --bundle "$out/libprov" --auto 4 || true); echo "$p" > "$out/c-libprov.log"
o=$(timeout 30 "${J[@]}" --bundle "$out/omitted" --auto 4 2>&1 || true); echo "$o" > "$out/c-omitted.log"
wait $jt || true
jt=
t=$(cat "$out/t.log")
wait $host 2>/dev/null || true
trap - EXIT

grep -q "\[world\] x4_resource_lab" "$out/host.log" || fail "the host did not load the world"
grep -q "\[script\] 2 scripts loaded (megamod.v1, host only)" "$out/host.log" || fail "the host did not load its own and the imported script"
echo "$a" | grep -q "on joining mover x4:entity/door_a closed (t 0.00" || fail "A did not find door A shut"
echo "$a" | grep -q "blocked at .* as expected" || fail "door A did not block A before the script opened it"
echo "$a" | grep -q "at the end: mover x4:entity/door_a open (t 1.00" || fail "A does not end with door A open"
echo "$a" | grep -q "at the end: mover x4:entity/door_b closed (t 0.00" || fail "door B moved"
echo "$a" | grep -q "route done" || fail "A could not walk through doorway A"
grep -q "\[script\] x4:script/open_door on_used(x4:entity/button_script, unit [0-9]*) phase host.world.script tick [0-9]*: ok, 1 requests" "$out/host.log" ||
    fail "the world's own on_used did not run and request"
grep -q "\[script\] x4shared:script/pulse_ability: pulse by player [0-9]* hit 1" "$out/host.log" || fail "the library's pulse hit nobody"
grep -q "\[script\] x4shared:script/pulse_ability on_ability(unit [0-9]*) phase host.world.script tick [0-9]*: ok, 1 requests" "$out/host.log" ||
    fail "the imported on_ability did not run and request"
grep -qE "was killed by" "$out/host.log" || fail "no native kill from the imported script's damage"
echo "$t" | grep -q "the host says we died" || fail "T never saw itself die"
for who in a b t; do grep -q "\[script\]" "$out/$who.log" && fail "joiner $who ran or logged a script"; done
echo "$b" | grep -q "on joining mover x4:entity/door_a open (t 1.00" || fail "late joiner B did not find door A open"
echo "$b" | grep -q "on joining mover x4:entity/door_b closed (t 0.00" || fail "late joiner B did not find door B shut"
# ---- 5. compatibility ---------------------------------------------------------------
echo "$m" | grep -q "REFUSED (not the host's map)" || fail "a one-character library change was not refused"
echo "$p" | grep -q "join: connected" || fail "a provenance-only library change was not admitted"
echo "$o" | grep -q "requires package x4.shared, but it is not present" || fail "a joiner without the library did not say what it lacks"
grep -q "match: scripts: .* 0 errors" "$out/host.log" || fail "the scripts had errors"
grep -q "match: Lua state closed, 0 bytes left" "$out/host.log" || fail "the Lua state did not give everything back"

echo "  contract and conformance: OAL's copies are this engine's"
echo "  world key $kg (OAL $(oal_key bundle)); library changed $km, library provenance $kp"
grep -E "\[script\]|killed by" "$out/host.log" | sed 's/^/  host /'
echo "$a" | grep -E "on joining mover x4:entity/door_a|blocked|use at|ability at|at the end: mover x4:entity/door_a" | sed 's/^/  A /'
echo "$b" | grep -E "on joining" | sed 's/^/  B /'
echo "  libmod:  $(echo "$m" | grep -E 'REFUSED|join: connected')"
echo "  libprov: $(echo "$p" | grep -E 'REFUSED|join: connected')"
echo "  omitted: $(echo "$o" | grep -E 'not present' | head -1)"
echo "X4 test OK ($out)"
