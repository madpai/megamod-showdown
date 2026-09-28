#!/bin/bash
# MegaMod: Night Shift end to end (docs/night_shift/): the first production
# vertical slice. Open Asset Lab builds the project (projects/night_shift:
# nightshift.assets, nightshift.facility, the world HARROW ANNEX in
# nightshift.world01) with `assetlab project build`; this engine hosts it and
# desktop joiners play the whole scenario as a crew.
#
#   scripts/test_night_shift.sh    (HTA_TRIAL_DIR or HTA_MAP; OAL_DIR, default
#                                   ../open-asset-lab; BUILD, default build-host)
#
# 1. Build: OAL builds the project twice, byte for byte the same; the engine
#    loads the set; keys engine == OAL; megamod-resources resolves every
#    binding; OAL's checker accepts both packages' dependents.
# 2. Play (one host with --trace-events, joiners as a crew):
#    A  alone: the dead lift and D1 buzz (auxiliary power off, D1 still
#       blocks); the maintenance passage's distant bang; the aux breaker ->
#       generator, D1 powered; D1 opens; the knock from behind D3.
#    B, C  together: B at the security console; C through the flooded pump
#       room (the live floor hurts C, only C, once) to the coolant valve; both
#       subsystems -> D3 powered (an AND of two conditions); C opens D3.
#    D, E  together into the cold spot: Lua sees company -- it only stirs.
#    F  alone into the cold spot: Lua takes F to the holding cell; F finds
#       the way out, pulls the core -> lockdown (D1 unpowered and shut, D3
#       slams, shutters open, alarms); F crosses the tunnel (something follows),
#       powers the lift,
#       rides it: the surface, shift complete.
#    G  late, after all of it: finds the world as it is (no event replayed),
#       and rides out too.
#    No joiner runs Lua or a binding; each event's bindings ran once.
# 3. Compatibility: a changed world binding, a changed prefab binding (world
#    bytes unchanged) and a changed sound sample are refused before spawn;
#    provenance-only edits of both libraries are admitted.
# 4. An X6 engine (scratch/engines/x6, from test_cross_version.sh) refuses
#    the world; skipped when it is not built.
set -e
cd "$(dirname "$0")/.."
trial=${HTA_TRIAL_DIR:-$(dirname "${HTA_MAP:-/nonexistent/x}")}
[ -f "$trial/bloodgulch.map" ] || { echo "SKIP: no Trial maps (HTA_TRIAL_DIR or HTA_MAP)"; exit 0; }
oal=${OAL_DIR:-../open-asset-lab}; [ -d "$oal" ] && oal=$(cd "$oal" && pwd)
py=$oal/.venv/bin/python; [ -x "$py" ] || py=python3
[ -f "$oal/projects/night_shift/project.py" ] || { echo "SKIP: no Night Shift project at $oal (OAL_DIR)"; exit 0; }
build=${BUILD:-build-host}
cmake --build "$build" --target megamod-join megamod-match megamod-content megamod-resources >/dev/null
out=scratch/night_shift; rm -rf "$out"; mkdir -p "$out"
here=$PWD
W=night_shift
fail() { echo "FAIL: $1" >&2; [ -f "$out/host.log" ] && { echo "--- host"; grep -E "world|bind\]  |script|net" "$out/host.log" | tail -40; }; exit 1; }

# ---- 1. build ---------------------------------------------------------------------------
(cd "$oal" && "$py" -m assetlab project build projects/night_shift --output "$here/$out/bundle" --json) > "$out/build.json" ||
    fail "OAL could not build the Night Shift project"
(cd "$oal" && "$py" -m assetlab project build projects/night_shift --output "$here/$out/again") > /dev/null
for f in maps/$W.oalmap packages/nightshift.assets.oalasset packages/nightshift.facility.oalasset; do
    cmp -s "$out/bundle/$f" "$out/again/$f" || fail "two builds of $f differ"
done
read -r okey ent bind <<<"$("$py" - "$out/build.json" <<'EOF'
import json, sys
w = json.load(open(sys.argv[1]))['worlds'][0]
print(w['world_key'], w['budget']['total'], w['budget']['bindings']['total'])
EOF
)"
./"$build"/megamod-content --trial "$trial" --bundle "$out/bundle" --world $W > "$out/content.log" 2>&1 || { cat "$out/content.log"; fail "the engine could not load the set"; }
ekey=$(sed -n 's/.* key \([0-9a-f]*\) .*/\1/p' "$out/content.log")
[ -n "$ekey" ] && [ "$ekey" = "$okey" ] || fail "OAL's world key ($okey) is not the engine's ($ekey)"
./"$build"/megamod-resources --bundle "$out/bundle" --world $W > "$out/resolved.json" || fail "megamod-resources refused the world"
"$py" - "$out/resolved.json" "$bind" <<'EOF' || fail "megamod-resources' view of the world is not the authored one"
import json, sys
d = json.load(open(sys.argv[1]))
b = d['bindings']
assert len(b) == int(sys.argv[2]), (len(b), sys.argv[2])
by = {(x['id'], x['origin']['instance'] if isinstance(x['origin'], dict) else 'world'): x for x in b}
t = by[('toggle', 'd3')]
assert t['source'] == 'nightshift:entity/d3__button' and t['actions'][0]['target'] == 'nightshift:entity/d3__door', t
assert by[('toggle', 'lift')]['actions'][0]['target'] == 'nightshift:entity/lift__door'
g = by[('sec_research', 'world')]
assert g['conditions'][0]['entity'] == 'nightshift:entity/coolant_flow' and g['actions'][0]['target'] == 'nightshift:entity/d3__power', g
assert [a['action'] for a in by[('ride', 'world')]['actions']] == ['teleport', 'activate', 'play_sound']
assert by[('cold_spot', 'world')]['actions'][0]['action'] == 'use'
EOF
for f in maps/$W.oalmap packages/nightshift.facility.oalasset; do
    (cd "$oal" && "$py" -m assetlab resources check "$here/$out/bundle/$f" --packages-dir "$here/$out/bundle") > "$out/oal-check.log" ||
        { cat "$out/oal-check.log"; fail "OAL's checker refused $f"; }
done
echo "  built: key $okey (engine $ekey), $ent/64 entities, $bind bindings; two builds identical"

# ---- variants for 3 ---------------------------------------------------------------------------
(cd "$oal" && "$py" - "$here/$out" <<'EOF'
import json, struct, sys
from pathlib import Path
out = Path(sys.argv[1]); good = out / 'bundle'
world = (good / 'maps/night_shift.oalmap').read_bytes()
fac = (good / 'packages/nightshift.facility.oalasset').read_bytes()
art = (good / 'packages/nightshift.assets.oalasset').read_bytes()
wml = struct.unpack_from('<I', world, 8)[0]

def lib_patch(data, old=None, new=None, manifest=None):
    ml = struct.unpack_from('<I', data, 8)[0]
    m = data[32:32 + ml]
    if manifest is not None:
        m = manifest(m)
    else:
        assert old.encode() in m, old
        m = m.replace(old.encode(), new.encode(), 1)
    return data[:8] + struct.pack('<I', len(m)) + data[12:32] + m + data[32 + ml:]

def prov(data, pid, rid):
    def f(m):
        j = json.loads(m)
        j['provenance'][rid]['creator'] = 'rebuilt on another machine'
        return json.dumps(j, sort_keys=True, separators=(',', ':')).encode()
    return lib_patch(data, manifest=f)

def variant(name, world_bytes=world, facility=fac, assets=art):
    d = out / name
    (d / 'maps').mkdir(parents=True); (d / 'packages').mkdir()
    (d / 'maps/night_shift.oalmap').write_bytes(world_bytes)
    (d / 'packages/nightshift.facility.oalasset').write_bytes(facility)
    (d / 'packages/nightshift.assets.oalasset').write_bytes(assets)

m = world[64:64 + wml]
assert b'"amount":20.0' in m
mb = m.replace(b'"amount":20.0', b'"amount":21.0', 1)
variant('worldmod', world[:8] + struct.pack('<I', len(mb)) + world[12:64] + mb + world[64 + wml:])
variant('prefabmod', facility=lib_patch(fac, '{"action":"toggle","target":"door"}', '{"action":"open","target":"door"}'))
variant('assetmod', assets=art[:-2] + bytes([art[-2] ^ 0x40, art[-1]]))
variant('provenance', facility=prov(fac, 'nightshift.facility', 'nightshift:prefab/security_door'),
        assets=prov(art, 'nightshift.assets', 'nightshift:sound/alarm'))
print('variants ok')
EOF
) > "$out/variants.log" || { cat "$out/variants.log"; fail "OAL could not build the variants"; }
eng_key() { ./"$build"/megamod-content --trial "$trial" --bundle "$out/$1" --world $W 2>&1 | sed -n 's/.* key \([0-9a-f]*\) .*/\1/p'; }
kw=$(eng_key worldmod); kf=$(eng_key prefabmod); ka=$(eng_key assetmod); kp=$(eng_key provenance)
[ -n "$kw" ] && [ "$kw" != "$okey" ] || fail "a changed world binding kept the key ($kw)"
[ -n "$kf" ] && [ "$kf" != "$okey" ] || fail "a changed prefab binding kept the key ($kf)"
[ -n "$ka" ] && [ "$ka" != "$okey" ] || fail "a changed sound sample kept the key ($ka)"
[ "$kp" = "$okey" ] || fail "provenance-only edits changed the key ($kp)"
cmp -s "$out/bundle/maps/$W.oalmap" "$out/prefabmod/maps/$W.oalmap" || fail "prefabmod's world bytes should be unchanged"

# ---- 2. play ------------------------------------------------------------------------------------
port=$((36000 + $$ % 400))
export HTA_TRIAL_DIR="$trial"
J=(./"$build"/megamod-join 127.0.0.1 "$port" --world $W --map "$trial/bloodgulch.map" --preset low --bundle "$out/bundle")
./"$build"/megamod-match --bundle "$out/bundle" --world $W --bots 0 --seconds 330 --trace-events --host "$port" > "$out/host.log" 2>&1 &
host=$!
trap 'kill $host 2>/dev/null || true' EXIT
sleep 1.5
t0=$(date +%s.%N)
# A: the dead lift, D1 locked; the passage; the breaker; D1 open; the knock from behind D3.
timeout 90 "${J[@]}" --auto 75 --route "3.2,-9.6;L4,-9.78;E;w1;0.5,-4.8;L0.78,-4.16;E;w1;B0,-3;-2,-10.4;-4.5,-10.4;-11.4,-10.4;-11.4,-6;-15.2,-4.2;L-16,-4.2;E;w2;-11.4,-6.5;-11.4,-10.4;-4.5,-10.4;0,-6;0.5,-4.8;L0.78,-4.16;E;w2;0,-2;0,5.5;w1.5" \
    > "$out/a.log" 2>&1 || { cat "$out/a.log"; fail "joiner A"; }
# B and C together: security, and coolant through the water.
timeout 90 "${J[@]}" --auto 60 --route "0,-3;0,3;-4.8,3.5;-4.8,4.2;L-4.8,5.2;E;w2;-2,3;0,3;0,7;w2" > "$out/b.log" 2>&1 &
jb=$!
timeout 90 "${J[@]}" --auto 75 --route "0,-3;0,3;-2,3;-8,1.6;-10.5,1.6;-11.5,2.5;w1;-9.6,1.6;-9.6,6;-15.4,6;-15.4,3;L-16,3;E;w1;-15.4,6;-9.6,6;-9.6,1.6;-8,1.6;-2,3;0,3;0,8;w1;L0.8,8.22;E;w2" \
    > "$out/c.log" 2>&1 || { cat "$out/c.log"; fail "joiner C"; }
wait $jb || { cat "$out/b.log"; fail "joiner B"; }
# D and E together into the cold spot.
timeout 60 "${J[@]}" --auto 40 --route "0,-3;0,8.6;1.5,8.7;3.6,8.8;5.5,9;w2" > "$out/d.log" 2>&1 &
jd=$!
timeout 60 "${J[@]}" --auto 40 --route "0,-3;0,9.2;1.5,9.2;3.6,9.1;5,9.1;w2" > "$out/e.log" 2>&1 || { cat "$out/e.log"; fail "joiner E"; }
wait $jd || { cat "$out/d.log"; fail "joiner D"; }
# F alone: taken; out of the cell; the core; lockdown; the tunnel; the lift breaker; the lift.
timeout 120 "${J[@]}" --auto 100 --route "0,-3;0,8.8;1.5,9;3.6,9;w1;4.8,12.3;8.5,12.3;11,12;11,15;11,19.8;L11,21;E;w5;14.5,20;18,20;18,0;L17.4,0;E;w1;18,-5.2;4.5,-5.2;3,-5.2;3.2,-9.6;L4,-9.78;E;w2;5.5,-9;w2" \
    > "$out/f.log" 2>&1 || { cat "$out/f.log"; fail "joiner F"; }
t1=$(date +%s.%N)
# G, late: the world as it is now; then out.
timeout 60 "${J[@]}" --auto 40 --route "0,-8;3.2,-9;5.5,-9;w2" > "$out/g.log" 2>&1 || { cat "$out/g.log"; fail "late joiner G"; }
for v in worldmod prefabmod assetmod provenance; do
    timeout 30 ./"$build"/megamod-join 127.0.0.1 "$port" --world $W --map "$trial/bloodgulch.map" --preset low --bundle "$out/$v" --auto 4 > "$out/v-$v.log" 2>&1 || true
done
wait $host 2>/dev/null || true
trap - EXIT

H=$out/host.log
unit() { sed -n 's/^join: connected, player \([0-9]*\).*/\1/p' "$1" | tail -1; }
grep -q "\[world\] 60 world entities" "$H" || grep -q "world entities" "$H" || fail "the host did not load the world's entities"
grep -q "\[script\] 1 scripts loaded (megamod.v1, host only)" "$H" || fail "the host did not load the anomaly script"
# A
grep -q "binding aux_on: relay_state nightshift:entity/aux_power is inactive -> true" "$H" || fail "auxiliary power did not start off"
grep -qF "join: sound nightshift:sound/locked at nightshift:entity/lift__plate (the host's binding)" "$out/a.log" || fail "A did not hear the dead lift"
grep -qF "join: sound nightshift:sound/locked at nightshift:entity/d1__plate (the host's binding)" "$out/a.log" || fail "A did not hear D1 locked"
grep -q "blocked at .* short of (0.00 -3.00), as expected" "$out/a.log" || fail "unpowered D1 did not block A"
grep -qF "join: sound nightshift:sound/distant_bang" "$out/a.log" || fail "A did not hear the passage's bang"
grep -qF "join: sound nightshift:sound/generator at nightshift:entity/aux_breaker__box" "$out/a.log" || fail "A did not hear the generator"
grep -qF "join: sound nightshift:sound/power_on at nightshift:entity/d1__plate" "$out/a.log" || fail "D1 did not report power"
grep -qF "join: sound nightshift:sound/knock at nightshift:entity/d3__door" "$out/a.log" || fail "A did not hear the knock behind D3"
grep -q "route stuck" "$out/a.log" && fail "A could not get through D1"
[ "$(grep -c "event nightshift:entity/aux_power activated" "$H")" = 1 ] || fail "aux power activated other than once"
# B, C
ub=$(unit "$out/b.log"); uc=$(unit "$out/c.log")
grep -qF "join: sound nightshift:sound/confirm at nightshift:entity/console__desk" "$out/b.log" || fail "B's console did not confirm"
grep -qE "\[world\] unit [0-9]+ hurt 20 by nightshift:entity/puddle_b \(binding shock_b\)" "$H" || fail "the live floor did not hurt"
[ "$(grep -c "hurt 20 by nightshift:entity/puddle_" "$H")" = 1 ] || fail "the live floor hurt other than once"
grep -q "join: the host says we were hurt" "$out/c.log" || fail "C (who stepped in) was not hurt"
grep -q "join: the host says we were hurt" "$out/b.log" && fail "B (who stayed dry) was hurt"
grep -q "binding cool_research: relay_state nightshift:entity/security_link is active -> true" "$H" || fail "the AND gate did not see security"
[ "$(grep -c "event nightshift:entity/d3__power activated" "$H")" = 1 ] || fail "D3 was powered other than once"
grep -qF "join: sound nightshift:sound/door_servo: nightshift:entity/d3__door started moving" "$out/c.log" || fail "C did not open D3"
# D, E
grep -q "\[script\] nightshift:script/anomaly: cold spot: [0-9] nearby, it keeps its distance" "$H" || fail "Lua did not see D and E together"
grep -qF "join: sound nightshift:sound/anomaly" "$out/d.log" || fail "D did not hear it stir"
for who in d e; do grep -q "join: the host moved us" "$out/$who.log" && fail "joiner $who was taken although not alone"; done
# F
grep -q "\[script\] nightshift:script/anomaly: cold spot: alone -- taken to the holding cell (1)" "$H" || fail "Lua did not take F alone"
grep -qE "join: the host moved us to \(2\.6[0-9] 10\.7[0-9]" "$out/f.log" || fail "F was not moved to the holding cell"
for want in "event nightshift:entity/core__socket used by unit" "binding unlatch (core): action open nightshift:entity/core__core queued" \
            "event nightshift:entity/core__core opened" "binding extract: action activate nightshift:entity/lockdown queued" \
            "event nightshift:entity/lockdown activated" "binding lockdown_seal: action deactivate nightshift:entity/d1__power queued" \
            "event nightshift:entity/d1__power deactivated" "binding unpowered (d1): action close nightshift:entity/d1__door queued" \
            "binding lockdown_routes: action open nightshift:entity/shutter_core__shutter queued" \
            "binding lift_power: action activate nightshift:entity/lift__power queued" "binding ride: action teleport nightshift:entity/surface queued"; do
    grep -qF "$want" "$H" || fail "the escalation's trace lacks '$want'"
done
grep -qF "join: sound nightshift:sound/alarm" "$out/f.log" || fail "F did not hear the alarm"
grep -qF "join: sound nightshift:sound/knock at nightshift:entity/shutter_core__shutter" "$out/f.log" || fail "F did not hear something follow into the tunnel"
grep -qF "join: sound nightshift:sound/shift_over at nightshift:entity/surface" "$out/f.log" || fail "F did not reach the surface's chord"
grep -qE "join: the host moved us to \(33\.5[0-9] -10\.0[0-9]" "$out/f.log" || fail "F did not ride the lift to the surface"
grep -q "route stuck" "$out/f.log" && fail "F got stuck"
# G: late join is state
for want in "mover nightshift:entity/d1__door closed" "mover nightshift:entity/d1__lamp closed" "mover nightshift:entity/d3__lamp open" \
            "mover nightshift:entity/core__core open" "mover nightshift:entity/shutter_dock__shutter open" "mover nightshift:entity/lift__lamp open" \
            "mover nightshift:entity/lift__door open" "mover nightshift:entity/sec_lamp__lamp open" "mover nightshift:entity/alarm_corridor__light open"; do
    grep -q "on joining $want" "$out/g.log" || fail "late joiner G did not find $want"
done
grep -qE "join: sound nightshift:sound/(generator|power_on|confirm|core_release|knock|distant_bang)" "$out/g.log" && fail "late joiner G heard history replayed"
grep -qE "join: the host moved us to \(33\.5[0-9] -10\.0[0-9]" "$out/g.log" || fail "G did not ride out"
# nobody but the host thinks
for who in a b c d e f g; do grep -qE "\[script\]|\[bind\]" "$out/$who.log" && fail "joiner $who ran a script or a binding"; done
grep -q "match: relay nightshift:entity/shift_complete active" "$H" || fail "the shift did not end complete"
grep -q "match: relay nightshift:entity/lockdown active" "$H" || fail "lockdown did not stay on"
# 3. compatibility
grep -q "REFUSED (not the host's map)" "$out/v-worldmod.log" || fail "a changed world binding was not refused"
grep -q "REFUSED (not the host's map)" "$out/v-prefabmod.log" || fail "a changed prefab binding was not refused"
grep -q "REFUSED (not the host's map)" "$out/v-assetmod.log" || fail "a changed sound sample was not refused"
grep -q "join: connected" "$out/v-provenance.log" || fail "provenance-only edits were not admitted"
# 4. an older engine
x6=scratch/engines/x6/$build/megamod-content
if [ -x "$x6" ]; then
    "$x6" --trial "$trial" --bundle "$out/bundle" --world $W > "$out/x6.log" 2>&1 && fail "the X6 engine accepted Night Shift"
    echo "  X6 engine refuses: $(grep -m1 -iE "refus|unknown|error" "$out/x6.log")"
else
    echo "  (no X6 engine in scratch/engines; scripts/test_cross_version.sh builds it)"
fi

echo "  crew: A $(unit "$out/a.log"), B $ub, C $uc, D $(unit "$out/d.log"), E $(unit "$out/e.log"), F $(unit "$out/f.log"), G $(unit "$out/g.log")"
echo "  scenario A..F: $(awk "BEGIN{printf \"%.0f\", $t1 - $t0}") s of play"
grep -E "\[world\] ([0-9]+ world entities|[0-9]+ event bindings)|\[script\] nightshift|hurt|teleported|match: (bindings|relay)" "$H" | sed 's/^/  host /'
echo "  G on joining: $(grep -c "on joining mover" "$out/g.log") movers, $(grep -c "join: sound" "$out/g.log") sounds (alarms only)"
echo "  keys: good $okey, world binding $kw, prefab binding $kf, sound sample $ka, provenance $kp"
echo "Night Shift test OK ($out)"
