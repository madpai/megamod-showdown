#!/bin/bash
# Older engines against newer packages (docs/RESOURCES.md "Older engines
# and newer packages"): builds the last pre-X4 engine (X3, a4e0318) and the
# X4 engine (e0ae793) from this repository's history into scratch worktrees,
# then checks, with real binaries, that none of them silently plays a newer
# world with different semantics:
#
#   X3 + an X4 world that imports a script     refused at load (missing script)
#   X3 + a declared, self-contained X4 world   loads with a different world key,
#                                              so X3 and X4+ peers refuse each
#                                              other before spawn, both ways
#   X3 + an X5 world                           refused at load (unknown kind 'prop')
#   X4 + an X5 world                           refused at load ('model' is reserved)
#   X4 and this build on X4 worlds             the same keys (X5 changed none)
#   X5 + an X6 prefab world                    refused at load ('prefab' is reserved)
#   X5 + an X6 prefab library                  refused ('prefab' is reserved in its provides)
#   X5 + a schema 5 world with no prefab       refused at load (unsupported schema)
#   X5 and this build on X5 worlds             the same keys; an X5 joiner is admitted
#                                              by this build's host (X6 changed none)
#   X6 + the X7 world                          refused at load (the X7 prefab's 'bindings': unknown field)
#   X6 + a schema 5 world placing the X7 prefab refused at load (the same, in the library)
#   X6 + a schema 6 world, bindings, X6 prefabs refused at load ('bindings': unknown field)
#   X6 and this build on X6 worlds             the same keys; X6 and this build admit each
#                                              other's joiners on an X6 world (X7 changed none)
#
#   scripts/test_cross_version.sh     (HTA_TRIAL_DIR or HTA_MAP; OAL_DIR)
# Slow the first time (four engine builds); not part of verify.sh.
set -e
cd "$(dirname "$0")/.."
trial=${HTA_TRIAL_DIR:-$(dirname "${HTA_MAP:-/nonexistent/x}")}
[ -f "$trial/bloodgulch.map" ] || { echo "SKIP: no Trial maps (HTA_TRIAL_DIR or HTA_MAP)"; exit 0; }
oal=${OAL_DIR:-../open-asset-lab}; [ -d "$oal" ] && oal=$(cd "$oal" && pwd)
py=$oal/.venv/bin/python; [ -x "$py" ] || py=python3
[ -d "$oal/assetlab" ] || { echo "SKIP: no Open Asset Lab at $oal (OAL_DIR)"; exit 0; }
build=${BUILD:-build-host}
here=$PWD
out=scratch/cross_version; mkdir -p "$out"
fail() { echo "FAIL: $1" >&2; exit 1; }
cmake --build "$build" --target megamod-join megamod-match megamod-content >/dev/null
engine() {   # name commit -> builds scratch/engines/<name>
    local d=scratch/engines/$1
    if [ ! -x "$d/build-host/megamod-content" ]; then
        [ -d "$d" ] || git worktree add --detach "$d" "$2" >/dev/null 2>&1
        cmake -S "$d" -B "$d/build-host" -G Ninja >/dev/null
        cmake --build "$d/build-host" --target megamod-content megamod-match megamod-join >/dev/null
    fi
    echo "$here/$d/build-host"
}
x3=$(engine x3 a4e0318); x4=$(engine x4 e0ae793); x5=$(engine x5 1b5a43e); x6=$(engine x6 2eae9df); now=$here/$build
rm -rf "$out/p"; mkdir -p "$out/p"
(cd "$oal" && "$py" - "$here/$out/p" <<'EOF'
import sys
from pathlib import Path
from assetlab.dependencies import compile_library, mapping_source
from assetlab.fixtures import x4_resource_lab, x4_shared, x5_resource_world, x5_shared_art, x6_libraries, x6_prefab_world
from assetlab.scripts import Script, load_source
from assetlab.world import compile_world
out = Path(sys.argv[1])
libs = {'x4.shared': x4_shared(), 'x5.shared_art': x5_shared_art()}
for pid, lib in libs.items():
    for b in ('imports', 'selfcontained', 'x5'):
        compile_library(lib, out / b / 'packages' / f'{pid}.oalasset')
compile_world(x4_resource_lab(), out / 'imports/maps/x4_resource_lab.oalmap', mapping_source(libs))
w = x4_resource_lab()          # declared, but needs no other package
w.scripts.append(Script('x4:script/pulse_ability', load_source('x4shared/pulse_ability.lua'), ['on_ability']))
w.ability_script, w.requires = 'x4:script/pulse_ability', []
compile_world(w, out / 'selfcontained/maps/x4_resource_lab.oalmap')
compile_world(x5_resource_world(), out / 'x5/maps/x5_resource_world.oalmap', mapping_source(libs))
x6 = x6_libraries()
for pid, lib in x6.items():
    compile_library(lib, out / 'x6' / 'packages' / f'{pid}.oalasset', mapping_source(x6))
compile_world(x6_prefab_world(), out / 'x6/maps/x6_prefab_world.oalmap', mapping_source(x6))
# an X6 library alone, required by an X5-style world that imports nothing from it
from assetlab.world import compile_world as cw
w = x5_resource_world(); w.file_name = 'x6_lib_only'
from assetlab.resources import Requirement
w.requires = list(w.requires) + [Requirement('x6.facility', [])]
both = dict(libs); both.update(x6)
for pid, lib in x6.items():
    compile_library(lib, out / 'x6lib' / 'packages' / f'{pid}.oalasset', mapping_source(x6))
compile_library(x5_shared_art(), out / 'x6lib' / 'packages' / 'x5.shared_art.oalasset')
cw(w, out / 'x6lib/maps/x6_lib_only.oalmap', mapping_source(both))
# schema 5 without a prefab: a turned prop
w = x5_resource_world(); w.entities[-1].yaw_degrees = 30.0
compile_library(x5_shared_art(), out / 'schema5' / 'packages' / 'x5.shared_art.oalasset')
compile_world(w, out / 'schema5/maps/x5_resource_world.oalmap', mapping_source(libs))
# X7: the facility world and the second world (a schema 5 world placing a schema 2 prefab)
from assetlab.fixtures import x7_facility_world, x7_libraries, x7_second_world
x7 = x7_libraries()
for pid, lib in x7.items():
    compile_library(lib, out / 'x7' / 'packages' / f'{pid}.oalasset', mapping_source(x7))
compile_world(x7_facility_world(), out / 'x7/maps/x7_facility_world.oalmap', mapping_source(x7))
compile_world(x7_second_world(), out / 'x7/maps/x7_second_world.oalmap', mapping_source(x7))
# a schema 6 world whose only new thing is a binding, on X6's prefab
from assetlab.bindings import Action, EventBinding
for pid, lib in x6.items():
    compile_library(lib, out / 'x6bind' / 'packages' / f'{pid}.oalasset', mapping_source(x6))
w = x6_prefab_world()
w.bindings = [EventBinding('lockdown_too', 'x6:entity/lockdown', 'used', [], [Action('toggle', target='x6:entity/north_door__door')])]
compile_world(w, out / 'x6bind/maps/x6_prefab_world.oalmap', mapping_source(x6))
EOF
) || fail "OAL could not build the packages"
load() { "$1/megamod-content" --trial "$trial" --bundle "$out/p/$2" --world "$3" 2>&1 | grep -E "^world " || true; }
key() { load "$@" | sed -n 's/.* key \([0-9a-f]*\) .*/\1/p'; }
load "$x3" imports x4_resource_lab | grep -q "FAILED: world entities: ability_script references missing script x4shared:script/pulse_ability" ||
    fail "X3 did not refuse an X4 world that imports a script"
k3=$(key "$x3" selfcontained x4_resource_lab); k4=$(key "$x4" selfcontained x4_resource_lab); kn=$(key "$now" selfcontained x4_resource_lab)
[ -n "$k3" ] && [ -n "$k4" ] && [ "$k4" = "$kn" ] || fail "keys: X3 $k3, X4 $k4, this build $kn"
[ "$k3" != "$k4" ] || fail "X3 computes the same key as X4 for a declared world ($k3): it could join with other semantics"
load "$x3" x5 x5_resource_world | grep -q "FAILED: world entities: x5:entity/crate_a: unknown kind 'prop'" || fail "X3 did not refuse an X5 world"
load "$x4" x5 x5_resource_world | grep -q "resource type 'model' is reserved, not loadable by this engine" || fail "X4 did not refuse an X5 world"
[ "$(key "$x4" imports x4_resource_lab)" = "$(key "$now" imports x4_resource_lab)" ] || fail "this build changed an X4 world's key"
load "$x5" x6 x6_prefab_world | grep -q "resource type 'prefab' is reserved, not loadable by this engine" || fail "X5 did not refuse an X6 prefab world"
load "$x5" x6lib x6_lib_only | grep -q "provides 'x6:prefab/security_door': resource type 'prefab' is reserved" || fail "X5 did not refuse an X6 prefab library"
load "$x5" schema5 x5_resource_world | grep -q "unsupported schema" || fail "X5 did not refuse a schema 5 world"
load "$now" x6 x6_prefab_world | grep -q "FAILED" && fail "this build refused the X6 world"
k5=$(key "$x5" x5 x5_resource_world); k6=$(key "$now" x5 x5_resource_world)
[ -n "$k5" ] && [ "$k5" = "$k6" ] || fail "this build changed an X5 world's key ($k5 vs $k6)"
load "$x6" x7 x7_facility_world | grep -q "package x7.facility: prefab x7:prefab/security_door: unknown field 'bindings'" ||
    fail "X6 did not refuse the X7 world"
load "$x6" x7 x7_second_world | grep -q "package x7.facility: prefab x7:prefab/security_door: unknown field 'bindings'" ||
    fail "X6 did not refuse a world placing the X7 prefab"
load "$x6" x6bind x6_prefab_world | grep -q "world_entities: unknown field 'bindings'" || fail "X6 did not refuse a schema 6 world"
load "$now" x7 x7_facility_world | grep -q "FAILED" && fail "this build refused the X7 world"
k6a=$(key "$x6" x6 x6_prefab_world); k7a=$(key "$now" x6 x6_prefab_world)
[ -n "$k6a" ] && [ "$k6a" = "$k7a" ] || fail "this build changed an X6 world's key ($k6a vs $k7a)"
export HTA_TRIAL_DIR="$trial"
join6() {   # host-bin joiner-bin port -> joiner's verdict on the X6 world
    "$1/megamod-match" --bundle "$out/p/x6" --world x6_prefab_world --bots 0 --seconds 10 --host "$3" > "$out/h6-$3.log" 2>&1 &
    local h=$!
    sleep 1.5
    timeout 20 "$2/megamod-join" 127.0.0.1 "$3" --world x6_prefab_world --map "$trial/bloodgulch.map" --bundle "$out/p/x6" \
        --preset low --auto 4 --shot "$out/j6.ppm" 2>/dev/null | grep -oE "REFUSED \(not the host's map\)|join: connected" | head -1 || true
    wait $h 2>/dev/null || true
}
join() {   # host-bin joiner-bin port -> joiner's verdict
    "$1/megamod-match" --bundle "$out/p/selfcontained" --world x4_resource_lab --bots 0 --seconds 10 --host "$3" > "$out/h-$3.log" 2>&1 &
    local h=$!
    sleep 1.5
    timeout 20 "$2/megamod-join" 127.0.0.1 "$3" --world x4_resource_lab --map "$trial/bloodgulch.map" --bundle "$out/p/selfcontained" \
        --preset low --auto 4 --shot "$out/j.ppm" 2>/dev/null | grep -oE "REFUSED \(not the host's map\)|join: connected" | head -1 || true
    wait $h 2>/dev/null || true
}
port=$((35300 + $$ % 300))
a=$(join "$now" "$x3" $port); b=$(join "$x3" "$now" $((port + 1))); c=$(join "$now" "$x4" $((port + 2))); d=$(join "$now" "$x5" $((port + 3)))
[ "$a" = "REFUSED (not the host's map)" ] || fail "an X3 joiner was not refused by this build's host ($a)"
[ "$b" = "REFUSED (not the host's map)" ] || fail "this build's joiner was not refused by an X3 host ($b)"
[ "$c" = "join: connected" ] || fail "an X4 joiner was not admitted to the same X4 world ($c)"
[ "$d" = "join: connected" ] || fail "an X5 joiner was not admitted to the same X4 world by this build ($d)"
e=$(join6 "$now" "$x6" $((port + 4))); f=$(join6 "$x6" "$now" $((port + 5)))
[ "$e" = "join: connected" ] || fail "an X6 joiner was not admitted to the X6 world by this build ($e)"
[ "$f" = "join: connected" ] || fail "this build's joiner was not admitted to the X6 world by an X6 host ($f)"
echo "  X3 + X4 world importing a script: refused at load"
echo "  X3 + declared self-contained X4 world: key $k3, X4/this build $k4 -> joins refused both ways"
echo "  X3 + X5 world: refused (unknown kind 'prop'); X4 + X5 world: refused ('model' reserved)"
echo "  X4 joiner, this host, X4 world: admitted"
echo "  X5 + X6 prefab world: $(load "$x5" x6 x6_prefab_world | sed 's/.*FAILED: //')"
echo "  X5 + X6 prefab library: $(load "$x5" x6lib x6_lib_only | sed 's/.*FAILED: //')"
echo "  X5 + schema 5 world (a turned prop, no prefab): $(load "$x5" schema5 x5_resource_world | sed 's/.*FAILED: //')"
echo "  X5 world: key $k5 on X5 and on this build; an X5 joiner, this host, the X4 world: admitted"
echo "  X6 + X7 world: $(load "$x6" x7 x7_facility_world | sed 's/.*FAILED: //')"
echo "  X6 + schema 5 world placing the X7 prefab: $(load "$x6" x7 x7_second_world | sed 's/.*FAILED: //')"
echo "  X6 + schema 6 world (a binding on X6's prefab): $(load "$x6" x6bind x6_prefab_world | sed 's/.*FAILED: //')"
echo "  X6 world: key $k6a on X6 and on this build; X6 joiner <-> this host, this joiner <-> X6 host: admitted"
echo "cross-version test OK ($out)"
