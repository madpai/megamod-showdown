#!/bin/bash
# X5 end to end on this machine (docs/RESOURCES.md "Asset resources"):
# package-backed asset resources. Open Asset Lab builds a LIBRARY of art
# (x5.shared_art: x5shared:texture/test_crate, material/test_crate,
# model/test_crate, sound/test_impact) and two worlds that import from it
# without copying it: x5_resource_world (two crates -- props of the
# library's model -- and doors whose definition names the library's sound)
# and x5_second_world (one crate). MegaMod loads the library beside each
# world by package ID, decodes it once, resolves every typed reference and
# draws, collides and plays the result.
#
#   scripts/test_x5.sh            (HTA_TRIAL_DIR or HTA_MAP for the Trial;
#                                  OAL_DIR for Open Asset Lab, default
#                                  ../open-asset-lab; BUILD, default build-host)
#
# 1. Contract: OAL's copies of the engine's contract and conformance corpus
#    (IDs, package IDs, member paths) are exactly what this build prints.
# 2. Identity: both worlds' keys are OAL's; the engine reports prop ->
#    model -> material -> texture and mover -> sound, each from x5.shared_art;
#    neither world carries the library's bytes; both worlds share one set of
#    library bytes (the same digest).
# 3. Refusals, by OAL's checker AND the engine, with the same words.
# 4. Play: host + joiner A (blocked by a crate, presses button A: door A
#    opens and its sound starts on the host and on A) + late joiner B (door A
#    open, both crates drawn in the library's texture); no joiner runs Lua;
#    the second world hosts and admits a joiner too.
# 5. Compatibility: one texel of the library (world bytes unchanged) is
#    refused before spawn; a provenance-only library change is admitted; a
#    joiner without the library cannot load the world and says so.
set -e
cd "$(dirname "$0")/.."
trial=${HTA_TRIAL_DIR:-$(dirname "${HTA_MAP:-/nonexistent/x}")}
[ -f "$trial/bloodgulch.map" ] || { echo "SKIP: no Trial maps (HTA_TRIAL_DIR or HTA_MAP)"; exit 0; }
oal=${OAL_DIR:-../open-asset-lab}; [ -d "$oal" ] && oal=$(cd "$oal" && pwd)
py=$oal/.venv/bin/python; [ -x "$py" ] || py=python3
[ -d "$oal/assetlab" ] || { echo "SKIP: no Open Asset Lab at $oal (OAL_DIR)"; exit 0; }
build=${BUILD:-build-host}
cmake --build "$build" --target megamod-join megamod-match megamod-content megamod-resources >/dev/null
out=scratch/x5; rm -rf "$out"; mkdir -p "$out"
here=$PWD
fail() { echo "FAIL: $1" >&2; [ -f "$out/host.log" ] && { echo "--- host"; grep -E "world|net|package|sound|assets" "$out/host.log" | tail -30; }; exit 1; }

# ---- 1. the contract Open Asset Lab validates against is this engine's ----
./"$build"/megamod-resources --json > "$out/contract.json"
./"$build"/megamod-resources --conformance > "$out/conformance.json"
cmp -s "$out/contract.json" "$oal/assetlab/data/megamod_resources.json" ||
    fail "OAL's assetlab/data/megamod_resources.json is not this engine's contract (megamod-resources --json > it)"
cmp -s "$out/conformance.json" "$oal/assetlab/data/megamod_id_conformance.json" ||
    fail "OAL's assetlab/data/megamod_id_conformance.json is stale (megamod-resources --conformance > it)"

# ---- the packages ------------------------------------------------------------
mkdir -p "$out/bundle/maps"
for w in x5_resource_world x5_second_world; do
    (cd "$oal" && "$py" -m assetlab fixture $w --output "$here/$out/bundle/maps/$w.oalmap" --packages "$here/$out/bundle/packages") \
        > "$out/fixture-$w.json" || fail "OAL could not build $w"
done
[ -f "$out/bundle/packages/x5.shared_art.oalasset" ] || fail "OAL did not write the library package"
(cd "$oal" && "$py" - "$here/$out" <<'EOF'
import json, shutil, struct, sys
from pathlib import Path
out = Path(sys.argv[1])
good = out / 'bundle'
world = (good / 'maps/x5_resource_world.oalmap').read_bytes()
lib = (good / 'packages/x5.shared_art.oalasset').read_bytes()
wml = struct.unpack_from('<I', world, 8)[0]
lml = struct.unpack_from('<I', lib, 8)[0]

def patched_world(old, new):
    """The good world with its manifest edited by hand, as a broken or
    hostile package would be."""
    m = world[64:64 + wml]
    assert old.encode() in m, old
    mb = m.replace(old.encode(), new.encode(), 1)
    return world[:8] + struct.pack('<I', len(mb)) + world[12:64] + mb + world[64 + wml:]

def patched_lib(old=None, new=None, payload=None):
    m = lib[32:32 + lml]
    if old is not None:
        assert old.encode() in m, old
        m = m.replace(old.encode(), new.encode(), 1)
    return lib[:8] + struct.pack('<I', len(m)) + lib[12:32] + m + (lib[32 + lml:] if payload is None else payload)

def variant(name, world_bytes=world, library=lib, extra=None):
    d = out / name
    (d / 'maps').mkdir(parents=True)
    (d / 'maps/x5_resource_world.oalmap').write_bytes(world_bytes)
    if library is not None:
        (d / 'packages').mkdir()
        (d / 'packages/x5.shared_art.oalasset').write_bytes(library)
    for pid, data in (extra or {}).items():
        (d / f'packages/{pid}.oalasset').write_bytes(data)

req = '"requires":[{"package":"x5.shared_art","resources":["x5shared:model/test_crate","x5shared:sound/test_impact"]}]'
crate = '"model":"x5shared:model/test_crate","position":[-2.0,1.2'
variant('omitted', patched_world(req, '"requires":[]'))                                   # dependency omitted
variant('absent', library=None)                                                            # provider package missing
variant('missing', patched_world(crate, '"model":"x5shared:model/test_crates","position":[-2.0,1.2'))
variant('wrongtype', patched_world(crate, '"model":"x5shared:material/test_crate","position":[-2.0,1.2'))
variant('malformed', patched_world(crate, '"model":"X5shared:model/test_crate","position":[-2.0,1.2'))
variant('undeclared', patched_world('"resources":["x5shared:model/test_crate","x5shared:sound/test_impact"]',
                                    '"resources":["x5shared:sound/test_impact"]'))
variant('liar', library=patched_lib('"id":"x5.shared_art"', '"id":"x5.other_art"'))       # file names another package
variant('nomember', library=patched_lib('"member":"models/test_crate.mesh"', '"member":"models/test_crate2.mesh"'))
variant('badpath', library=patched_lib('"member":"models/test_crate.mesh"', '"member":"models/Test_crate.mesh"'))
variant('traversal', library=patched_lib('{"path":"models/test_crate.mesh"', '{"path":"../models/test_crate.mesh"'))
tex = ('{"format":"rgba8","height":16,"id":"x5shared:texture/test_crate","member":"textures/test_crate.rgba","width":16}')
variant('dupdesc', library=patched_lib(tex, tex + ',' + tex))                               # duplicate descriptor
variant('provides', library=patched_lib('"provides":["x5shared:material/test_crate",', '"provides":['))
# A second library that also provides the crate texture: a duplicate provider.
m = json.loads(lib[32:32 + lml])
tex_bytes = lib[32 + lml:]
off = 0
for mem in m['assets']['members']:
    if mem['path'] == 'textures/test_crate.rgba':
        tb = tex_bytes[off:off + mem['size']]
    off += mem['size']
dup = {'assets': {'materials': [], 'members': [{'path': 't.rgba', 'size': len(tb)}], 'models': [], 'schema': 1, 'sounds': [],
                  'textures': [{'format': 'rgba8', 'height': 16, 'id': 'x5shared:texture/test_crate', 'member': 't.rgba', 'width': 16}]},
       'kind': 'library', 'package': {'id': 'x5.dup_art', 'provides': ['x5shared:texture/test_crate'], 'requires': [], 'schema': 1},
       'scripts': []}
db = json.dumps(dup, sort_keys=True, separators=(',', ':')).encode()
variant('duplicate', patched_world(req, '"requires":[{"package":"x5.dup_art","resources":[]},' + req[len('"requires":['):]),
        extra={'x5.dup_art': struct.pack('<4sIIII12x', b'OALA', 1, len(db), 0, 0) + db + tb})
# One texel of the library; its provenance only.
p = bytearray(lib[32 + lml:]); p[len(p) - 1024 * 0 - 4 * 17] ^= 0x10
variant('libmod', library=patched_lib(payload=bytes(p)))
lm = json.loads(lib[32:32 + lml])
lm['provenance']['x5shared:model/test_crate']['creator'] = 'rebuilt on another machine'
lm['source_provenance'] = 'elsewhere'
mb = json.dumps(lm, sort_keys=True, separators=(',', ':')).encode()
variant('libprov', library=lib[:8] + struct.pack('<I', len(mb)) + lib[12:32] + mb + lib[32 + lml:])
# Neither world carries the library's payload.
for w in ('x5_resource_world', 'x5_second_world'):
    wb = (good / f'maps/{w}.oalmap').read_bytes()
    assert tex_bytes[:4096] not in wb and b'MSH1' not in wb, w
    assert 'assets' not in json.loads(wb[64:64 + struct.unpack_from('<I', wb, 8)[0]]), w
print('variants ok')
EOF
) > "$out/variants.log" || { cat "$out/variants.log"; fail "OAL could not build the variants"; }

# ---- 2. identity: engine == OAL; what resolved to what --------------------------
oal_key() { (cd "$oal" && "$py" -m assetlab world-key "$here/$out/$1/maps/$2.oalmap" --packages-dir "$here/$out/$1") | sed -n 's/.*"world_key": "\(.*\)".*/\1/p'; }
eng_key() { ./"$build"/megamod-content --trial "$trial" --bundle "$out/$1" --world $2 > "$out/content-$1-$2.log" 2>&1 ||
                { cat "$out/content-$1-$2.log"; fail "the engine could not load the $1 package set"; }
            sed -n 's/.* key \([0-9a-f]*\) .*/\1/p' "$out/content-$1-$2.log"; }
kg=$(eng_key bundle x5_resource_world); k2=$(eng_key bundle x5_second_world)
km=$(eng_key libmod x5_resource_world); kp=$(eng_key libprov x5_resource_world)
[ -n "$kg" ] && [ "$kg" = "$(oal_key bundle x5_resource_world)" ] || fail "OAL's world key ($(oal_key bundle x5_resource_world)) is not the engine's ($kg)"
[ -n "$k2" ] && [ "$k2" = "$(oal_key bundle x5_second_world)" ] || fail "OAL's second-world key is not the engine's ($k2)"
[ "$km" = "$(oal_key libmod x5_resource_world)" ] && [ "$kp" = "$(oal_key libprov x5_resource_world)" ] || fail "OAL and the engine disagree on a variant's key"
[ "$km" != "$kg" ] || fail "one texel of the required library kept the world key"
[ "$kp" = "$kg" ] || fail "a provenance-only library change changed the world key"
cmp -s "$out/bundle/maps/x5_resource_world.oalmap" "$out/libmod/maps/x5_resource_world.oalmap" || fail "libmod's world bytes should be unchanged"
for w in x5_resource_world x5_second_world; do
    ./"$build"/megamod-resources --bundle "$out/bundle" --world $w > "$out/resolved-$w.json" || fail "megamod-resources refused $w"
done
r=$out/resolved-x5_resource_world.json
grep -q '"from": "x5:entity/crate_a", "field": "world_entities.entities\[\].model", "to": "x5shared:model/test_crate", "provider": "x5.shared_art", "index": 0' "$r" ||
    fail "crate A's model did not resolve to the library's"
grep -q '"from": "x5:mover/basic_slide_door", "field": "world_entities.mover_definitions\[\].sound", "to": "x5shared:sound/test_impact", "provider": "x5.shared_art"' "$r" ||
    fail "the door definition's sound did not resolve to the library's"
grep -q '"from": "x5shared:model/test_crate", "field": "assets.models\[\].materials\[\]", "slot": 0, "to": "x5shared:material/test_crate"' "$r" ||
    fail "the model's slot did not resolve to the library's material"
grep -q '"from": "x5shared:material/test_crate", "field": "assets.materials\[\].texture", "to": "x5shared:texture/test_crate"' "$r" ||
    fail "the material did not resolve to the library's texture"
grep -q '"from": "x5b:entity/crate", "field": "world_entities.entities\[\].model", "to": "x5shared:model/test_crate", "provider": "x5.shared_art"' \
    "$out/resolved-x5_second_world.json" || fail "the second world's crate did not resolve to the library's model"
d1=$(sed -n 's/.*"x5.shared_art", "direct": true, "digest": "\([0-9a-f]*\)".*/\1/p' "$r")
d2=$(sed -n 's/.*"x5.shared_art", "direct": true, "digest": "\([0-9a-f]*\)".*/\1/p' "$out/resolved-x5_second_world.json")
[ -n "$d1" ] && [ "$d1" = "$d2" ] || fail "the two consumers do not share one library ($d1, $d2)"
for w in x5_resource_world x5_second_world; do
    (cd "$oal" && "$py" -m assetlab resources check "$here/$out/bundle/maps/$w.oalmap" "$here/$out/bundle/packages/x5.shared_art.oalasset" \
        --packages-dir "$here/$out/bundle") > "$out/oal-check-$w.log" || { cat "$out/oal-check-$w.log"; fail "OAL's checker refused $w"; }
done

# ---- 3. refusals: OAL first, the engine regardless ------------------------------
refuse() {   # variant, message both must give
    local v=$1 want=$2
    (cd "$oal" && "$py" -m assetlab resources check "$here/$out/$v/maps/x5_resource_world.oalmap" --packages-dir "$here/$out/$v") \
        > "$out/oal-check-$v.log" 2>&1 && fail "OAL's checker accepted the $v variant"
    grep -qF -- "$want" "$out/oal-check-$v.log" || { cat "$out/oal-check-$v.log"; fail "OAL's checker did not say '$want' for $v"; }
    ./"$build"/megamod-resources --bundle "$out/$v" --world x5_resource_world > "$out/eng-$v.json" &&
        fail "the engine accepted the $v variant"
    grep -qF -- "$want" "$out/eng-$v.json" || { cat "$out/eng-$v.json"; fail "the engine did not say '$want' for $v"; }
    ./"$build"/megamod-content --trial "$trial" --bundle "$out/$v" --world x5_resource_world > "$out/content-$v.log" 2>&1 &&
        fail "megamod-content loaded the $v variant"
    echo "  refused $v: $want"
}
refuse omitted "x5:mover/basic_slide_door references missing sound x5shared:sound/test_impact (no package in this set provides namespace 'x5shared': is a requirement missing?)"
refuse absent "package x5.resource_world requires package x5.shared_art, but it is not present (looked for packages/x5.shared_art.oalasset)"
refuse missing "x5:entity/crate_a references missing model x5shared:model/test_crates"
refuse wrongtype "x5:entity/crate_a: model x5shared:material/test_crate is a material, expected a model"
refuse malformed "x5:entity/crate_a: model 'X5shared:model/test_crate' is not a resource ID: namespace has capital 'X'"
refuse undeclared "x5:entity/crate_a: model x5shared:model/test_crate is provided by package x5.shared_art, which package x5.resource_world requires but does not import it from"
refuse duplicate "x5shared:texture/test_crate: provided by both package x5.dup_art and package x5.shared_art (duplicate providers are refused, never picked)"
refuse liar "package x5.resource_world requires package x5.shared_art, but packages/x5.shared_art.oalasset declares package x5.other_art"
refuse nomember "x5shared:model/test_crate declares package member models/test_crate2.mesh, but that member is missing"
refuse badpath "x5shared:model/test_crate: member path 'models/Test_crate.mesh': has capital 'T' (paths are lowercase; nothing is folded)"
refuse traversal "member path '../models/test_crate.mesh': has '..' (no parent references)"
refuse dupdesc "package x5.shared_art: x5shared:texture/test_crate is declared twice"
refuse provides "package x5.shared_art has material x5shared:material/test_crate but does not list it in provides"

# ---- 4. play ------------------------------------------------------------------------
port=$((34800 + $$ % 400))
export HTA_TRIAL_DIR="$trial"
J=(./"$build"/megamod-join 127.0.0.1 "$port" --world x5_resource_world --map "$trial/bloodgulch.map" --preset low)
./"$build"/megamod-match --bundle "$out/bundle" --world x5_resource_world --bots 0 --seconds 90 \
    --host "$port" > "$out/host.log" 2>&1 &
host=$!
trap 'kill $host 2>/dev/null || true' EXIT
sleep 1.5
a=$(timeout 70 "${J[@]}" --bundle "$out/bundle" --auto 60 \
    --route "-3.2,1.2;L-2,1.2;B-1.2,1.2;-1.8,-4.6;-0.6,-4.2;L-0.14,-4.2;E;w2;-1.0,-3;0.9,-3;w1") || { echo "$a"; fail "joiner A"; }
echo "$a" > "$out/a.log"
b=$(timeout 60 "${J[@]}" --bundle "$out/bundle" --auto 30 --shot "$out/b.ppm" --route "-3.4,1.7;L-2,1.7;w1" 2>>"$out/stderr.log") || { echo "$b"; fail "late joiner B"; }
echo "$b" > "$out/b.log"
m=$(timeout 30 "${J[@]}" --bundle "$out/libmod" --auto 4 || true); echo "$m" > "$out/c-libmod.log"
p=$(timeout 30 "${J[@]}" --bundle "$out/libprov" --auto 4 || true); echo "$p" > "$out/c-libprov.log"
o=$(timeout 30 "${J[@]}" --bundle "$out/absent" --auto 4 2>&1 || true); echo "$o" > "$out/c-absent.log"
wait $host 2>/dev/null || true
# the second consumer hosts too
port2=$((port + 1))
./"$build"/megamod-match --bundle "$out/bundle" --world x5_second_world --bots 0 --seconds 12 --host "$port2" > "$out/host2.log" 2>&1 &
host=$!
sleep 1.5
s=$(timeout 30 ./"$build"/megamod-join 127.0.0.1 "$port2" --world x5_second_world --map "$trial/bloodgulch.map" --preset low \
    --bundle "$out/bundle" --auto 6 --shot "$out/s.ppm" --route "-3.6,1.2;L-2.5,1.2;w2" 2>>"$out/stderr.log") || { echo "$s"; fail "second world joiner"; }
echo "$s" > "$out/s.log"
wait $host 2>/dev/null || true
trap - EXIT

grep -q "\[world\] assets: 1 textures, 1 materials, 1 models, 1 sounds (12 KB), 2 props, 1 sounds bound" "$out/host.log" ||
    fail "the host did not load the library's assets"
echo "$a" | grep -q "join: prop x5:entity/crate_a: model x5shared:model/test_crate (index 0, from x5.shared_art), 12 triangles, material x5shared:material/test_crate, texture x5shared:texture/test_crate" ||
    fail "A did not resolve crate A through the library"
echo "$a" | grep -q "join: 2 props drawn with 1 models from the package set" || fail "A did not draw two props from one model"
echo "$a" | grep -q "blocked at .* short of (-1.20 1.20), as expected" || fail "crate A did not block A"
echo "$a" | grep -q "on joining mover x5:entity/door_a closed (t 0.00" || fail "A did not find door A shut"
echo "$a" | grep -q "join: sound x5shared:sound/test_impact: x5:entity/door_a started moving" || fail "A did not hear door A start"
echo "$a" | grep -q "at the end: mover x5:entity/door_a open (t 1.00" || fail "A does not end with door A open"
echo "$a" | grep -q "route done" || fail "A did not finish its route"
grep -q "\[world\] sound x5shared:sound/test_impact: x5:entity/door_a started opening" "$out/host.log" || fail "the host did not sound door A"
echo "$b" | grep -q "on joining mover x5:entity/door_a open (t 1.00" || fail "late joiner B did not find door A open"
echo "$b" | grep -q "join: sound" && fail "late joiner B heard a door it found already open"
for who in a b s; do grep -q "\[script\]" "$out/$who.log" && fail "joiner $who ran or logged a script"; done
echo "$s" | grep -q "join: prop x5b:entity/crate: model x5shared:model/test_crate (index 0, from x5.shared_art)" || fail "the second world's joiner has no crate"
echo "$s" | grep -q "join: connected" || fail "the second world did not admit its joiner"
# The picture: the library's cyan crate faces, on B's screen and the second world's.
crate_pixels() { python3 - "$1" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
head = d.split(b'\n', 3)
w, h = map(int, head[1].split())
px = head[3]
n = sum(1 for i in range(0, w * h * 3, 3) if px[i + 2] > 150 and px[i + 1] > 140 and px[i] < 120 and px[i + 2] - px[i] > 80)
print(n * 1000 // (w * h))
EOF
}
cb=$(crate_pixels "$out/b.ppm"); cs=$(crate_pixels "$out/s.ppm")
[ "$cb" -ge 20 ] || fail "B's picture has no crate in the library's colours ($cb per mille)"
[ "$cs" -ge 10 ] || fail "the second world's picture has no crate ($cs per mille)"
# ---- 5. compatibility ---------------------------------------------------------------
echo "$m" | grep -q "REFUSED (not the host's map)" || fail "one texel of the library was not refused"
echo "$p" | grep -q "join: connected" || fail "a provenance-only library change was not admitted"
echo "$o" | grep -q "requires package x5.shared_art, but it is not present" || fail "a joiner without the library did not say what it lacks"
grep -q "match: assets: 1 textures, 1 materials, 1 models, 1 sounds; 1 mover sounds started (1 distinct clips)" "$out/host.log" ||
    fail "the host's sound count is wrong"

echo "  contract and conformance: OAL's copies are this engine's"
echo "  world key $kg (OAL $(oal_key bundle x5_resource_world)); second world $k2; library texel $km, library provenance $kp"
echo "  one library, two consumers: digest $d1 in both; neither world carries its bytes"
grep -E "\[world\] (assets|sound)" "$out/host.log" | sed 's/^/  host /'
echo "$a" | grep -E "prop x5:entity/crate_a|props drawn|blocked|use at|join: sound|at the end: mover x5:entity/door_a" | sed 's/^/  A /'
echo "$b" | grep -E "on joining mover x5:entity/door_a" | sed 's/^/  B /'
echo "  B's picture: $cb per mille crate pixels; second world's: $cs"
echo "  libmod:  $(echo "$m" | grep -E 'REFUSED|join: connected')"
echo "  libprov: $(echo "$p" | grep -E 'REFUSED|join: connected')"
echo "  absent:  $(echo "$o" | grep -E 'not present' | head -1)"
echo "X5 test OK ($out)"
