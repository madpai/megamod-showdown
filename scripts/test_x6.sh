#!/bin/bash
# X6 end to end on this machine (docs/PREFABS.md): prefabs. Open Asset Lab
# builds an art LIBRARY (x6.shared_assets: door panel, frame and button
# models, their materials and textures, a hiss), a PREFAB library
# (x6.facility: x6:prefab/security_door and its button script, composed of
# that art) and two worlds that import ONLY the prefab:
#   x6_prefab_world   north_door (untransformed) and south_door (turned -90)
#                     in two walls, and a lockdown button whose world script
#                     toggles south_door's door by its ordinary placed ID;
#   x6_second_world   one freestanding instance, turned 45 and 1.25x.
# MegaMod expands every instance at load into ordinary entities and plays
# them; nothing during play knows a prefab.
#
#   scripts/test_x6.sh            (HTA_TRIAL_DIR or HTA_MAP; OAL_DIR, default
#                                  ../open-asset-lab; BUILD, default build-host)
#
# 1. Contract: OAL's copies of the contract and conformance corpus (local
#    IDs included) are this build's.
# 2. Identity: both worlds' keys are OAL's; megamod-resources shows each
#    instance's children (paths, IDs, indices), the prefab from x6.facility,
#    every child reference resolved from the PROVIDER's view (x6.shared_
#    assets), the world importing only the prefab; both worlds share one
#    x6.facility digest.
# 3. Refusals, by OAL's checker AND the engine, with the same words.
# 4. Play: host + joiner A (blocked by north's door; presses north's button:
#    north opens, south stays shut; walks through) + late joiner B (finds
#    north open / south shut; blocked by the turned south door; opens it with
#    south's button; walks through; the lockdown script closes ONLY south)
#    + joiner C's picture of the turned door; the second world's turned,
#    scaled door blocks as its oriented box, opens, admits. No joiner runs Lua.
# 5. Compatibility: a changed prefab child (world bytes unchanged) is
#    refused before spawn; a changed texel of the art library the world never
#    imported is refused; a provenance-only prefab change is admitted; a
#    joiner without the prefab library says what it lacks.
set -e
cd "$(dirname "$0")/.."
trial=${HTA_TRIAL_DIR:-$(dirname "${HTA_MAP:-/nonexistent/x}")}
[ -f "$trial/bloodgulch.map" ] || { echo "SKIP: no Trial maps (HTA_TRIAL_DIR or HTA_MAP)"; exit 0; }
oal=${OAL_DIR:-../open-asset-lab}; [ -d "$oal" ] && oal=$(cd "$oal" && pwd)
py=$oal/.venv/bin/python; [ -x "$py" ] || py=python3
[ -d "$oal/assetlab" ] || { echo "SKIP: no Open Asset Lab at $oal (OAL_DIR)"; exit 0; }
build=${BUILD:-build-host}
cmake --build "$build" --target megamod-join megamod-match megamod-content megamod-resources >/dev/null
out=scratch/x6; rm -rf "$out"; mkdir -p "$out"
here=$PWD
fail() { echo "FAIL: $1" >&2; [ -f "$out/host.log" ] && { echo "--- host"; grep -E "world|net|package|sound|script" "$out/host.log" | tail -30; }; exit 1; }

# ---- 1. the contract ------------------------------------------------------------
./"$build"/megamod-resources --json > "$out/contract.json"
./"$build"/megamod-resources --conformance > "$out/conformance.json"
cmp -s "$out/contract.json" "$oal/assetlab/data/megamod_resources.json" ||
    fail "OAL's assetlab/data/megamod_resources.json is not this engine's contract (megamod-resources --json > it)"
cmp -s "$out/conformance.json" "$oal/assetlab/data/megamod_id_conformance.json" ||
    fail "OAL's assetlab/data/megamod_id_conformance.json is stale (megamod-resources --conformance > it)"

# ---- the packages -------------------------------------------------------------------
mkdir -p "$out/bundle/maps"
for w in x6_prefab_world x6_second_world; do
    (cd "$oal" && "$py" -m assetlab fixture $w --output "$here/$out/bundle/maps/$w.oalmap" --packages "$here/$out/bundle/packages") \
        > "$out/fixture-$w.json" || fail "OAL could not build $w"
done
for p in x6.shared_assets x6.facility; do [ -f "$out/bundle/packages/$p.oalasset" ] || fail "OAL did not write $p"; done
(cd "$oal" && "$py" - "$here/$out" <<'EOF'
import json, struct, sys
from pathlib import Path
out = Path(sys.argv[1])
good = out / 'bundle'
world = (good / 'maps/x6_prefab_world.oalmap').read_bytes()
fac = (good / 'packages/x6.facility.oalasset').read_bytes()
art = (good / 'packages/x6.shared_assets.oalasset').read_bytes()
wml, fml, aml = (struct.unpack_from('<I', b, 8)[0] for b in (world, fac, art))

def patched_world(old, new):
    m = world[64:64 + wml]
    assert old.encode() in m, old
    mb = m.replace(old.encode(), new.encode(), 1)
    return world[:8] + struct.pack('<I', len(mb)) + world[12:64] + mb + world[64 + wml:]

def patched_lib(lib, ml, old=None, new=None, payload=None):
    m = lib[32:32 + ml]
    if old is not None:
        assert old.encode() in m, old
        m = m.replace(old.encode(), new.encode(), 1)
    return lib[:8] + struct.pack('<I', len(m)) + lib[12:32] + m + (lib[32 + ml:] if payload is None else payload)

def variant(name, world_bytes=world, facility=fac, assets=art):
    d = out / name
    (d / 'maps').mkdir(parents=True)
    (d / 'maps/x6_prefab_world.oalmap').write_bytes(world_bytes)
    (d / 'packages').mkdir()
    if facility is not None:
        (d / 'packages/x6.facility.oalasset').write_bytes(facility)
    if assets is not None:
        (d / 'packages/x6.shared_assets.oalasset').write_bytes(assets)

F = lambda old, new: patched_lib(fac, fml, old, new)
north = '{"id":"north_door","position":[0.0,3.0,0.0],"prefab":"x6:prefab/security_door"}'
# the world's side
variant('omitted', patched_world('"resources":["x6:prefab/security_door"]', '"resources":[]'))
variant('absent', facility=None)
variant('noassets', assets=None)
variant('missing', patched_world(north, north.replace('security_door', 'security_dor')))
variant('wrongtype', patched_world(north, north.replace('x6:prefab/security_door', 'x6:script/security_door_log')))
variant('malformed', patched_world(north, north.replace('x6:prefab', 'X6:prefab')))
variant('badinstance', patched_world(north, north.replace('north_door', 'North_door')))
variant('scale0', patched_world('"yaw_degrees":-90.0}', '"scale":0.0,"yaw_degrees":-90.0}'))
variant('scaleneg', patched_world('"yaw_degrees":-90.0}', '"scale":-1.0,"yaw_degrees":-90.0}'))
variant('dupinstance', patched_world('"id":"south_door"', '"id":"north_door"'))
# eleven more instances before north_door: 1 + 11 x 6 entities passes 64 at the eleventh
variant('toomany', patched_world(north, ','.join(north.replace('north_door', f'n{i:02d}') for i in range(11)) + ',' + north))
# the prefab's side
variant('childmissing', facility=F('"model":"x6shared:model/door_panel"', '"model":"x6shared:model/door_panels"'))
variant('childwrongtype', facility=F('"model":"x6shared:model/frame_top"', '"model":"x6shared:sound/door_hiss"'))
variant('childunimported', facility=F('"x6shared:model/frame_top",', ''))
variant('dupchild', facility=F('{"id":"frame_right"', '{"id":"frame_left"'))
variant('sibling', facility=F('"target":"door"', '"target":"dor"'))
variant('nested', facility=F('{"id":"frame_top","kind":"prop"', '{"id":"frame_top","kind":"prefab"'))
variant('badkind', facility=F('{"id":"frame_top","kind":"prop"', '{"id":"frame_top","kind":"widget"'))
variant('provides', facility=F('"provides":["x6:prefab/security_door",', '"provides":['))
variant('cycle', facility=F('{"id":"door","kind":"mover"',
        '{"id":"d1","kind":"relay","links":[{"event":"fired","input":"activate","target":"d2"}]},'
        '{"id":"d2","kind":"relay","links":[{"event":"fired","input":"activate","target":"d1"}]},'
        '{"id":"door","kind":"mover"'))
# compatibility
variant('libmod', facility=F('"position":[0.0,0.7,0.7]', '"position":[0.0,0.71,0.7]'))
fm = json.loads(fac[32:32 + fml])
fm['provenance']['x6:prefab/security_door']['creator'] = 'rebuilt on another machine'
fm['source_provenance'] = 'elsewhere'
mb = json.dumps(fm, sort_keys=True, separators=(',', ':')).encode()
variant('libprov', facility=fac[:8] + struct.pack('<I', len(mb)) + fac[12:32] + mb)
p = bytearray(art[32 + aml:]); p[-100] ^= 0x10
variant('artmod', assets=patched_lib(art, aml, payload=bytes(p)))
# neither world carries a prefab or an asset byte
for w in ('x6_prefab_world', 'x6_second_world'):
    wb = (good / f'maps/{w}.oalmap').read_bytes()
    m = json.loads(wb[64:64 + struct.unpack_from('<I', wb, 8)[0]])
    assert 'prefabs' not in m and 'assets' not in m and b'MSH1' not in wb, w
    assert m['package']['requires'] == [{'package': 'x6.facility', 'resources': ['x6:prefab/security_door']}], w
print('variants ok')
EOF
) > "$out/variants.log" || { cat "$out/variants.log"; fail "OAL could not build the variants"; }

# ---- 2. identity ------------------------------------------------------------------------
oal_key() { (cd "$oal" && "$py" -m assetlab world-key "$here/$out/$1/maps/$2.oalmap" --packages-dir "$here/$out/$1") | sed -n 's/.*"world_key": "\(.*\)".*/\1/p'; }
eng_key() { ./"$build"/megamod-content --trial "$trial" --bundle "$out/$1" --world $2 > "$out/content-$1-$2.log" 2>&1 ||
                { cat "$out/content-$1-$2.log"; fail "the engine could not load the $1 package set"; }
            sed -n 's/.* key \([0-9a-f]*\) .*/\1/p' "$out/content-$1-$2.log"; }
kg=$(eng_key bundle x6_prefab_world); k2=$(eng_key bundle x6_second_world)
km=$(eng_key libmod x6_prefab_world); kp=$(eng_key libprov x6_prefab_world); ka=$(eng_key artmod x6_prefab_world)
[ -n "$kg" ] && [ "$kg" = "$(oal_key bundle x6_prefab_world)" ] || fail "OAL's world key ($(oal_key bundle x6_prefab_world)) is not the engine's ($kg)"
[ -n "$k2" ] && [ "$k2" = "$(oal_key bundle x6_second_world)" ] || fail "OAL's second-world key is not the engine's ($k2)"
[ "$km" = "$(oal_key libmod x6_prefab_world)" ] && [ "$kp" = "$(oal_key libprov x6_prefab_world)" ] &&
    [ "$ka" = "$(oal_key artmod x6_prefab_world)" ] || fail "OAL and the engine disagree on a variant's key"
[ "$km" != "$kg" ] || fail "a changed prefab child kept the world key"
[ "$ka" != "$kg" ] || fail "a changed texel of the art library kept the world key"
[ "$kp" = "$kg" ] || fail "a provenance-only prefab change changed the world key"
cmp -s "$out/bundle/maps/x6_prefab_world.oalmap" "$out/libmod/maps/x6_prefab_world.oalmap" || fail "libmod's world bytes should be unchanged"
for w in x6_prefab_world x6_second_world; do
    ./"$build"/megamod-resources --bundle "$out/bundle" --world $w > "$out/resolved-$w.json" || fail "megamod-resources refused $w"
done
r=$out/resolved-x6_prefab_world.json
grep -q '"from": "north_door", "field": "world_entities.prefab_instances\[\].prefab", "to": "x6:prefab/security_door", "provider": "x6.facility", "entities": \[1, 7\]' "$r" ||
    fail "north_door's prefab did not resolve to x6.facility's"
grep -q '{"path": "south_door/door", "entity": "x6:entity/south_door__door", "kind": "mover", "index": 9' "$r" || fail "south_door's door is not entity 9"
grep -q '"from": "x6:entity/south_door__button", "field": "world_entities.entities\[\].links\[\].target", "to": "x6:entity/south_door__door", "index": 9' "$r" ||
    fail "south's button does not link to south's door"
grep -q '"from": "x6:entity/north_door__button", "field": "world_entities.entities\[\].links\[\].target", "to": "x6:entity/north_door__door", "index": 3' "$r" ||
    fail "north's button does not link to north's door"
grep -q '"from": "x6:entity/north_door__door", "field": "prefabs.prefabs\[\].children\[\].model", "to": "x6shared:model/door_panel", "provider": "x6.shared_assets"' "$r" ||
    fail "the door's model did not resolve (from the prefab's view) to x6.shared_assets"
grep -q '"from": "x6:entity/south_door__door", "field": "prefabs.prefabs\[\].children\[\].sound", "to": "x6shared:sound/door_hiss", "provider": "x6.shared_assets"' "$r" ||
    fail "the door's sound did not resolve to x6.shared_assets"
grep -q '"id": "x6:script/security_door_log", "index": 1, "from": "x6.facility"' "$r" || fail "the prefab's script is not in the world's table from x6.facility"
grep -q '"x6.shared_assets", "direct": false' "$r" || fail "the art library should be in the set but not required by the world"
grep -q '"from": "gate", "field": "world_entities.prefab_instances\[\].prefab", "to": "x6:prefab/security_door", "provider": "x6.facility"' \
    "$out/resolved-x6_second_world.json" || fail "the second world's gate is not the same prefab"
d1=$(sed -n 's/.*"x6.facility", "direct": true, "digest": "\([0-9a-f]*\)".*/\1/p' "$r")
d2=$(sed -n 's/.*"x6.facility", "direct": true, "digest": "\([0-9a-f]*\)".*/\1/p' "$out/resolved-x6_second_world.json")
[ -n "$d1" ] && [ "$d1" = "$d2" ] || fail "the two consumers do not share one prefab library ($d1, $d2)"
for w in x6_prefab_world x6_second_world; do
    (cd "$oal" && "$py" -m assetlab resources check "$here/$out/bundle/maps/$w.oalmap" "$here/$out/bundle/packages/x6.facility.oalasset" \
        --packages-dir "$here/$out/bundle") > "$out/oal-check-$w.log" || { cat "$out/oal-check-$w.log"; fail "OAL's checker refused $w"; }
done

# ---- 3. refusals: OAL first, the engine regardless -------------------------------------
refuse() {   # variant, message both must give
    local v=$1 want=$2
    (cd "$oal" && "$py" -m assetlab resources check "$here/$out/$v/maps/x6_prefab_world.oalmap" --packages-dir "$here/$out/$v") \
        > "$out/oal-check-$v.log" 2>&1 && fail "OAL's checker accepted the $v variant"
    grep -qF -- "$want" "$out/oal-check-$v.log" || { cat "$out/oal-check-$v.log"; fail "OAL's checker did not say '$want' for $v"; }
    ./"$build"/megamod-resources --bundle "$out/$v" --world x6_prefab_world > "$out/eng-$v.json" &&
        fail "the engine accepted the $v variant"
    grep -qF -- "$want" "$out/eng-$v.json" || { cat "$out/eng-$v.json"; fail "the engine did not say '$want' for $v"; }
    ./"$build"/megamod-content --trial "$trial" --bundle "$out/$v" --world x6_prefab_world > "$out/content-$v.log" 2>&1 &&
        fail "megamod-content loaded the $v variant"
    echo "  refused $v: $want"
}
refuse omitted "prefab instance north_door: prefab x6:prefab/security_door is provided by package x6.facility, which package x6.prefab_world requires but does not import it from"
refuse absent "package x6.prefab_world requires package x6.facility, but it is not present (looked for packages/x6.facility.oalasset)"
refuse noassets "package x6.facility requires package x6.shared_assets, but it is not present (looked for packages/x6.shared_assets.oalasset)"
refuse missing "prefab instance north_door references missing prefab x6:prefab/security_dor"
refuse wrongtype "prefab instance north_door: prefab x6:script/security_door_log is a script, expected a prefab"
refuse malformed "prefab instance north_door: prefab 'X6:prefab/security_door' is not a resource ID: namespace has capital 'X'"
refuse badinstance "prefab instance 'North_door': instance id has capital 'N' (IDs are lowercase; nothing is folded)"
refuse scale0 "prefab instance south_door: scale 0 out of range (uniform, 0.25 to 4)"
refuse scaleneg "prefab instance south_door: scale -1 out of range (uniform, 0.25 to 4)"
refuse dupinstance "prefab instance north_door appears twice"
refuse toomany "prefab instance n10 expands the world to 67 entities, exceeding limit 64"
refuse childmissing "prefab x6:prefab/security_door child 'door' references missing model x6shared:model/door_panels"
refuse childwrongtype "prefab x6:prefab/security_door child 'frame_top': model x6shared:sound/door_hiss is a sound, expected a model"
refuse childunimported "prefab x6:prefab/security_door child 'frame_top': model x6shared:model/frame_top is provided by package x6.shared_assets, which package x6.facility requires but does not import it from"
refuse dupchild "prefab x6:prefab/security_door contains duplicate local child id 'frame_left'"
refuse sibling "prefab x6:prefab/security_door child 'button' references missing child 'dor'"
refuse nested "prefab x6:prefab/security_door child 'frame_top' contains a nested prefab reference, which is not supported in prefab schema 1"
refuse badkind "prefab x6:prefab/security_door child 'frame_top': unknown kind 'widget'"
refuse provides "package x6.facility has prefab x6:prefab/security_door but does not list it in provides"
refuse cycle "prefab x6:prefab/security_door: link cycle: d1 -> d2 -> d1"

# ---- 4. play ------------------------------------------------------------------------------
port=$((35100 + $$ % 400))
export HTA_TRIAL_DIR="$trial"
J=(./"$build"/megamod-join 127.0.0.1 "$port" --world x6_prefab_world --map "$trial/bloodgulch.map" --preset low)
./"$build"/megamod-match --bundle "$out/bundle" --world x6_prefab_world --bots 0 --seconds 100 \
    --host "$port" > "$out/host.log" 2>&1 &
host=$!
trap 'kill $host 2>/dev/null || true' EXIT
sleep 1.5
a=$(timeout 60 "${J[@]}" --bundle "$out/bundle" --auto 25 \
    --route "-1.0,3.0;L0,3.0;B1.0,3.0;-0.9,2.3;L-0.16,2.3;E;w2.5;-0.8,3.0;1.5,3.0;w1") || { echo "$a"; fail "joiner A"; }
echo "$a" > "$out/a.log"
b=$(timeout 70 "${J[@]}" --bundle "$out/bundle" --auto 35 \
    --route "-3.0,-1.0;L-3.0,-2.0;B-3.0,-3.0;-3.7,-1.2;L-3.7,-1.84;E;w2.5;-3.0,-1.2;-3.0,-3.2;-3.0,-1.2;-5.3,0.5;L-5.84,0.5;E;w2.5") ||
    { echo "$b"; fail "late joiner B"; }
echo "$b" > "$out/b.log"
c=$(timeout 40 "${J[@]}" --bundle "$out/bundle" --auto 6 --shot "$out/c.ppm" --route "-3.0,0.2;L-3.0,-2.0;w1" 2>>"$out/stderr.log") ||
    { echo "$c"; fail "joiner C"; }
echo "$c" > "$out/c.log"
m=$(timeout 30 "${J[@]}" --bundle "$out/libmod" --auto 4 || true); echo "$m" > "$out/c-libmod.log"
t=$(timeout 30 "${J[@]}" --bundle "$out/artmod" --auto 4 || true); echo "$t" > "$out/c-artmod.log"
p=$(timeout 30 "${J[@]}" --bundle "$out/libprov" --auto 4 || true); echo "$p" > "$out/c-libprov.log"
o=$(timeout 30 "${J[@]}" --bundle "$out/absent" --auto 4 2>&1 || true); echo "$o" > "$out/c-absent.log"
wait $host 2>/dev/null || true
# the second consumer: one instance, turned 45 degrees and scaled 1.25
port2=$((port + 1))
./"$build"/megamod-match --bundle "$out/bundle" --world x6_second_world --bots 0 --seconds 25 --host "$port2" > "$out/host2.log" 2>&1 &
host=$!
sleep 1.5
s=$(timeout 35 ./"$build"/megamod-join 127.0.0.1 "$port2" --world x6_second_world --map "$trial/bloodgulch.map" --preset low \
    --bundle "$out/bundle" --auto 20 --shot "$out/s.ppm" \
    --route "0.44,-1.06;L1.5,0;B2.56,1.06;1.41,-1.33;L1.977,-0.76;E;w2.5;0.44,-1.06;2.56,1.06;w1;0.0,-1.8;L1.5,0;w1" 2>>"$out/stderr.log") ||
    { echo "$s"; fail "second world joiner"; }
echo "$s" > "$out/s.log"
wait $host 2>/dev/null || true
trap - EXIT

# the host expanded, loaded and ran
grep -q "\[world\] 13 world entities, 2 links" "$out/host.log" || fail "the host did not expand 13 entities"
grep -q "\[world\] prefab instance north_door: x6:prefab/security_door from x6.facility -> entities 1..6 (x6:entity/north_door__button .. x6:entity/north_door__frame_top)" \
    "$out/host.log" || fail "the host's north_door expansion"
grep -q "\[world\] prefab instance south_door: x6:prefab/security_door from x6.facility -> entities 7..12" "$out/host.log" || fail "the host's south_door expansion"
grep -q "\[script\] 2 scripts loaded (megamod.v1, host only)" "$out/host.log" || fail "the host did not load the world's and the prefab's script"
# A: north's button moves north's door only
echo "$a" | grep -q "join: prefab instance south_door: x6:prefab/security_door from x6.facility -> entities 7..12" || fail "A did not expand as the host did"
echo "$a" | grep -q "on joining mover x6:entity/north_door__door closed" || fail "A did not find north shut"
echo "$a" | grep -q "blocked at .* short of (1.00 3.00), as expected" || fail "north's door did not block A"
echo "$a" | grep -q "join: sound x6shared:sound/door_hiss: x6:entity/north_door__door started moving" || fail "A did not hear north's door"
echo "$a" | grep -q "at the end: mover x6:entity/north_door__door open (t 1.00" || fail "A does not end with north open"
echo "$a" | grep -q "at the end: mover x6:entity/south_door__door closed (t 0.00" || fail "north's button moved south's door"
echo "$a" | grep -q "route stuck" && fail "A could not walk through north's doorway"
echo "$a" | grep -q "route done" || fail "A did not finish"
grep -q "\[script\] x6:script/security_door_log: security door button entity x6:entity/north_door__button use 1" "$out/host.log" ||
    fail "the prefab's script did not receive north's own button"
grep -q "\[world\] sound x6shared:sound/door_hiss: x6:entity/north_door__door started opening" "$out/host.log" || fail "the host did not sound north's door"
# B: late join per instance; south's button, south's door; the world's script, south only
echo "$b" | grep -q "on joining mover x6:entity/north_door__door open (t 1.00" || fail "late joiner B did not find north open"
echo "$b" | grep -q "on joining mover x6:entity/south_door__door closed (t 0.00" || fail "late joiner B did not find south shut"
echo "$b" | grep -q "blocked at .* short of (-3.00 -3.00), as expected" || fail "south's turned door did not block B"
echo "$b" | grep -q "route stuck" && fail "B could not walk through south's doorway"
echo "$b" | grep -q "route done" || fail "B did not finish"
grep -q "\[script\] x6:script/security_door_log: security door button entity x6:entity/south_door__button use 2" "$out/host.log" ||
    fail "the prefab's script did not receive south's own button"
grep -q "\[script\] x6:script/lockdown: lockdown toggles entity x6:entity/south_door__door which was open" "$out/host.log" ||
    fail "the world's script did not address south's door by its placed ID"
echo "$b" | grep -q "at the end: mover x6:entity/south_door__door closed (t 0.00" || fail "the lockdown did not close south's door"
echo "$b" | grep -q "at the end: mover x6:entity/north_door__door open (t 1.00" || fail "the lockdown touched north's door"
[ "$(grep -c "\[world\] sound x6shared:sound/door_hiss: x6:entity/north_door__door" "$out/host.log")" = 1 ] ||
    fail "north's door moved more than once"
for who in a b c s; do grep -q "\[script\]" "$out/$who.log" && fail "joiner $who ran or logged a script"; done
# the second world: its turned, scaled door blocks as an oriented box
echo "$s" | grep -q "join: prefab instance gate: x6:prefab/security_door from x6.facility -> entities 0..5" || fail "the second world did not expand its gate"
bl=$(echo "$s" | sed -n 's/.*blocked at (\([-0-9.]*\) \([-0-9.]*\)) short of (2.56 1.06), as expected.*/\1 \2/p')
[ -n "$bl" ] || fail "the second world's door did not block"
python3 -c "
import sys; x, y = map(float, sys.argv[1:])
d = ((x - 1.5) * -0.70711 + (y - 0.0) * -0.70711)      # distance in front of the door along its facing
assert 0.15 < d < 0.45, d                                 # half its thickness (0.0625) + a body radius: the oriented box
" $bl || fail "the second world's door blocked at ($bl), not at its oriented face"
echo "$s" | grep -q "at the end: mover x6b:entity/gate__door open (t 1.00, offset -1.15 1.15 0.00)" || fail "the gate door did not slide along its own axis"
echo "$s" | grep -q "route stuck" && fail "the second world's joiner could not walk through the gate"
echo "$s" | grep -q "join: connected" || fail "the second world did not admit its joiner"
# pictures: the prefab's hazard-striped door on C's screen (turned) and the second world's
door_pixels() { python3 - "$1" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
head = d.split(b'\n', 3)
w, h = map(int, head[1].split())
px = head[3]
n = sum(1 for i in range(0, w * h * 3, 3) if px[i] > 170 and px[i + 1] > 140 and px[i + 2] < 110)
print(n * 1000 // (w * h))
EOF
}
cc=$(door_pixels "$out/c.ppm"); cs=$(door_pixels "$out/s.ppm")
[ "$cc" -ge 20 ] || fail "C's picture has no security door ($cc per mille)"
[ "$cs" -ge 10 ] || fail "the second world's picture has no security door ($cs per mille)"
# ---- 5. compatibility -------------------------------------------------------------------------
echo "$m" | grep -q "REFUSED (not the host's map)" || fail "a changed prefab child was not refused"
echo "$t" | grep -q "REFUSED (not the host's map)" || fail "a changed texel of the art library was not refused"
echo "$p" | grep -q "join: connected" || fail "a provenance-only prefab change was not admitted"
echo "$o" | grep -q "requires package x6.facility, but it is not present" || fail "a joiner without the prefab library did not say what it lacks"

echo "  contract and conformance: OAL's copies are this engine's"
echo "  world key $kg (OAL $(oal_key bundle x6_prefab_world)); second world $k2; prefab child $km, art texel $ka, provenance $kp"
echo "  one prefab library, two consumers: digest $d1 in both; neither world carries a prefab or asset byte"
grep -E "\[world\] (prefab|13 world)|\[script\] x6:script/(security_door_log|lockdown):" "$out/host.log" | sed 's/^/  host /'
echo "$a" | grep -E "blocked|use at|join: sound|at the end" | sed 's/^/  A /'
echo "$b" | grep -E "on joining|blocked|use at|at the end" | sed 's/^/  B /'
echo "$s" | grep -E "prefab instance|blocked|use at|at the end" | sed 's/^/  second /'
echo "  pictures: C $cc per mille door pixels; second world $cs"
echo "  libmod:  $(echo "$m" | grep -E 'REFUSED|join: connected')"
echo "  artmod:  $(echo "$t" | grep -E 'REFUSED|join: connected')"
echo "  libprov: $(echo "$p" | grep -E 'REFUSED|join: connected')"
echo "  absent:  $(echo "$o" | grep -E 'not present' | head -1)"
echo "X6 test OK ($out)"
