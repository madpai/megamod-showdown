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
#
#   scripts/test_cross_version.sh     (HTA_TRIAL_DIR or HTA_MAP; OAL_DIR)
# Slow the first time (two engine builds); not part of verify.sh.
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
x3=$(engine x3 a4e0318); x4=$(engine x4 e0ae793); now=$here/$build
rm -rf "$out/p"; mkdir -p "$out/p"
(cd "$oal" && "$py" - "$here/$out/p" <<'EOF'
import sys
from pathlib import Path
from assetlab.dependencies import compile_library, mapping_source
from assetlab.fixtures import x4_resource_lab, x4_shared, x5_resource_world, x5_shared_art
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
export HTA_TRIAL_DIR="$trial"
join() {   # host-bin joiner-bin port -> joiner's verdict
    "$1/megamod-match" --bundle "$out/p/selfcontained" --world x4_resource_lab --bots 0 --seconds 10 --host "$3" > "$out/h-$3.log" 2>&1 &
    local h=$!
    sleep 1.5
    timeout 20 "$2/megamod-join" 127.0.0.1 "$3" --world x4_resource_lab --map "$trial/bloodgulch.map" --bundle "$out/p/selfcontained" \
        --preset low --auto 4 --shot "$out/j.ppm" 2>/dev/null | grep -oE "REFUSED \(not the host's map\)|join: connected" | head -1 || true
    wait $h 2>/dev/null || true
}
port=$((35300 + $$ % 300))
a=$(join "$now" "$x3" $port); b=$(join "$x3" "$now" $((port + 1))); c=$(join "$now" "$x4" $((port + 2)))
[ "$a" = "REFUSED (not the host's map)" ] || fail "an X3 joiner was not refused by this build's host ($a)"
[ "$b" = "REFUSED (not the host's map)" ] || fail "this build's joiner was not refused by an X3 host ($b)"
[ "$c" = "join: connected" ] || fail "an X4 joiner was not admitted to the same X4 world ($c)"
echo "  X3 + X4 world importing a script: refused at load"
echo "  X3 + declared self-contained X4 world: key $k3, X4/this build $k4 -> joins refused both ways"
echo "  X3 + X5 world: refused (unknown kind 'prop'); X4 + X5 world: refused ('model' reserved)"
echo "  X4 joiner, this host, X4 world: admitted"
echo "cross-version test OK ($out)"
