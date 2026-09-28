#!/bin/bash
# X8 production proof: the separate Night Shift world restores D2 and D5,
# exceeds 64 runtime objects, and a desktop joiner receives its compact state.
set -euo pipefail
cd "$(dirname "$0")/.."
trial=${HTA_TRIAL_DIR:-$(dirname "${HTA_MAP:-/nonexistent/x}")}
[ -f "$trial/bloodgulch.map" ] || { echo 'SKIP: no Trial maps'; exit 0; }
oal=${OAL_DIR:-../open-asset-lab}
variant=${NIGHT_SHIFT_VARIANT:-x8}
[[ "$variant" == x8 || "$variant" == x9 ]] || { echo "unknown Night Shift variant: $variant" >&2; exit 2; }
world=night_shift_$variant
[ -f "$oal/projects/night_shift/project_$variant.py" ] || { echo "SKIP: no OAL $variant project"; exit 0; }
oal=$(cd "$oal" && pwd)
py="$oal/.venv/bin/python"; [ -x "$py" ] || py=python3
build=${BUILD:-build-host}
cmake --build "$build" --target megamod-content megamod-resources megamod-match megamod-join >/dev/null
out=$(mktemp -d "$PWD/scratch/$variant-prod.XXXXXX")
(cd "$oal" && "$py" -m assetlab project build "projects/night_shift/project_$variant.py" --output "$out/bundle" --json) > "$out/build.json"
./"$build"/megamod-content --trial "$trial" --bundle "$out/bundle" --world "$world" > "$out/content.log"
./"$build"/megamod-resources --bundle "$out/bundle" --world "$world" > "$out/resolved.json"
"$py" - "$out/build.json" "$out/resolved.json" "$out/content.log" "$variant" <<'PY'
import json, re, sys
oal = json.load(open(sys.argv[1]))['worlds'][0]
engine = json.load(open(sys.argv[2]))['world_state']
assert oal['world_key'] == re.search(r'key ([0-9a-f]+)', open(sys.argv[3]).read()).group(1)
budget = oal['budget']['world_state']
for key in ('runtime_objects', 'spatial', 'logical', 'host_only', 'snapshot_bytes'):
    assert budget[key] == engine[key], (key, budget[key], engine[key])
assert (engine['runtime_objects'], engine['spatial'], engine['logical'], engine['host_only']) == (74, 23, 11, 40)
assert engine['gpu_instances'] <= engine['limits']['gpu_instances']
objects = {x['id']: x for x in engine['objects']}
assert objects['nightshift:entity/valve__wheel']['runtime'] == 69
assert objects['nightshift:entity/vent_south__zone']['runtime'] == 73
assert objects['nightshift:entity/vent_south__plume']['channel'] == 'spatial'
assert 'nightshift:entity/d2__door' in objects and 'nightshift:entity/d5__door' in objects
if sys.argv[4] == 'x9':
    visual = json.load(open(sys.argv[2]))['visual']
    assert visual['lights_authored'] == 26 and visual['lights_active_max'] == 8
print('  OAL/engine key and counts agree:', oal['world_key'], budget)
PY
port=$((38000 + $$ % 1000))
HTA_TRIAL_DIR="$trial" ./"$build"/megamod-match --bundle "$out/bundle" --world "$world" --bots 0 --seconds 13 --host "$port" --world-state > "$out/host.log" 2>&1 &
host=$!
trap 'kill "$host" 2>/dev/null || true' EXIT
sleep 2
if [ -z "${VK_ICD_FILENAMES:-}" ] && [ -f "$PWD/scratch/lvp/usr/share/vulkan/icd.d/lvp_icd.json" ]; then
    export VK_ICD_FILENAMES="$PWD/scratch/lvp/usr/share/vulkan/icd.d/lvp_icd.json"
fi
HTA_TRIAL_DIR="$trial" timeout 25 ./"$build"/megamod-join 127.0.0.1 "$port" --world "$world" \
    --map "$trial/bloodgulch.map" --bundle "$out/bundle" --preset low --auto 4 --shot "$out/join.ppm" --world-state > "$out/join.log" 2>&1
wait "$host"
trap - EXIT
grep -q 'on joining mover nightshift:entity/pump__piston' "$out/join.log"
grep -q 'on joining mover nightshift:entity/vent_south__plume' "$out/join.log"
grep -q 'world state: .*largest 39).*synced, 0 refused' "$out/join.log"
grep -q 'WORLD_STATE sent .*largest 39' "$out/host.log"
grep -q 'object nightshift:entity/valve__wheel interactable runtime 69 host-only' "$out/host.log"

# Play the restored security path. The first player starts auxiliary power;
# the next opens D1 and enables security; the third opens restored D2 and
# uses the valve, a prefab child at runtime index 69. A fresh joiner sees
# the resulting relays and movers without receiving that event history.
dynport=$((39000 + $$ % 1000))
HTA_TRIAL_DIR="$trial" ./"$build"/megamod-match --bundle "$out/bundle" --world "$world" --bots 0 \
    --seconds 220 --trace-events --host "$dynport" --world-state > "$out/dynamic-host.log" 2>&1 &
dynhost=$!
trap 'kill "$dynhost" 2>/dev/null || true' EXIT
sleep 2
J=(./"$build"/megamod-join 127.0.0.1 "$dynport" --world "$world" --map "$trial/bloodgulch.map" \
   --bundle "$out/bundle" --preset low)
timeout 95 "${J[@]}" --auto 75 --route "3.2,-9.6;L4,-9.78;E;w1;0.5,-4.8;L0.78,-4.16;E;w1;B0,-3;-2,-10.4;-4.5,-10.4;-11.4,-10.4;-11.4,-6;-15.2,-4.2;L-16,-4.2;E;w2" > "$out/aux.log" 2>&1
timeout 95 "${J[@]}" --auto 85 --route "0.5,-4.8;L0.78,-4.16;E;w2;0,-3;0,3;-4.8,3.5;-4.8,4.2;L-4.8,5.2;E;w2" > "$out/security.log" 2>&1
if [[ "$variant" == x9 ]]; then
    timeout 110 "${J[@]}" --auto 100 --route "0,-3;0,3;-2,3;-8,1.6;L-8.84,0.82;E;w2;-10.5,1.6;-11.5,2.5;w1;-9.6,1.6;-9.6,6;-15.4,6;-15.4,3;L-16,3;E;w2;-15.4,6;-9.6,6;-9.6,1.6;-8,1.6;-2,3;0,3;0,8;w1;L0.8,8.22;E;w2" > "$out/coolant.log" 2>&1
    timeout 100 "${J[@]}" --auto 85 --route "0,-3;0,8.8;1.5,9;3.6,9;w1;4.8,12.3;8.5,12.3;11,15.5;L11.78,15.84;E;w2;11,19.8;L11,21;E;w5" \
        --shot "$out/lockdown.ppm" --shot-view 11,18,1.05,1.57,0 > "$out/lockdown.log" 2>&1
else
    timeout 95 "${J[@]}" --auto 85 --route "0,-3;0,3;-2,3;-8,1.6;L-8.84,0.82;E;w2;-10.5,1.6;-11.5,2.5;w1;-9.6,1.6;-9.6,6;-15.4,6;-15.4,3;L-16,3;E;w2" > "$out/coolant.log" 2>&1
fi
timeout 25 "${J[@]}" --auto 4 --shot "$out/late.ppm" --world-state > "$out/late.log" 2>&1
wait "$dynhost"
trap - EXIT
for path in aux security coolant; do grep -q 'join: route done' "$out/$path.log"; done
grep -q 'used nightshift:entity/d2__button_back' "$out/dynamic-host.log"
grep -q 'used nightshift:entity/valve__wheel' "$out/dynamic-host.log"
grep -q 'event nightshift:entity/coolant_flow activated' "$out/dynamic-host.log"
grep -q 'event nightshift:entity/d5__power activated' "$out/dynamic-host.log"
grep -q 'on joining relay nightshift:entity/security_link active' "$out/late.log"
grep -q 'on joining relay nightshift:entity/coolant_flow active' "$out/late.log"
if [[ "$variant" == x9 ]]; then
    grep -q 'join: route done' "$out/lockdown.log"
    grep -q 'event nightshift:entity/lockdown activated' "$out/dynamic-host.log"
    grep -q 'on joining relay nightshift:entity/lockdown active' "$out/late.log"
fi
if [[ "$variant" == x9 ]]; then
    grep -q 'on joining mover nightshift:entity/d2__door closed' "$out/late.log"
    grep -q 'on joining mover nightshift:entity/d5__door closed' "$out/late.log"
else
    grep -q 'on joining mover nightshift:entity/d2__door open' "$out/late.log"
fi
grep -q 'on joining mover nightshift:entity/pump__piston' "$out/late.log"
grep -q 'world state: .*synced, 0 refused' "$out/late.log"
echo "Night Shift $variant: 74 objects, 23 movers, 11 relays, 40 host-only; 39-byte idle snapshot, index-69 use and late join synced ($out)"
