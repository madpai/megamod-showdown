#!/bin/bash
# X7 end to end on this machine (docs/EVENT_BINDINGS.md): declarative event
# bindings. Open Asset Lab builds a LIBRARY (x7.facility: the prefab
# x7:prefab/security_door -- a POWERED door whose behaviour is bindings, no
# Lua -- and two sounds, reusing X6's art library x6.shared_assets) and two
# worlds:
#   x7_facility_world  north_door and south_door (turned -90) of the powered
#                      door; a shock pad (entered -> damage, teleport, sound);
#                      a maintenance button whose world Lua script (custom:
#                      every second press) opens north's door, while its own
#                      binding clicks;
#   x7_second_world    one freestanding powered door (turned 90).
#
#   scripts/test_x7.sh            (HTA_TRIAL_DIR or HTA_MAP; OAL_DIR, default
#                                  ../open-asset-lab; BUILD, default build-host)
#
# 1. Contract: OAL's copies are this build's (bindings vocabulary included).
# 2. Identity: keys engine == OAL; megamod-resources shows every binding,
#    compiled per instance; both worlds share one x7.facility digest.
# 3. Refusals, by OAL's checker AND the engine, with the same words; OAL
#    also refuses a binding cycle no condition can break (the engine loads it
#    and bounds it: step 5).
# 4. Play (host with --trace-events): A is blocked by north's door; north's
#    button, unpowered, only sounds locked; north's power button: power ->
#    activated -> open -> opened -> chime; A walks through; south untouched.
#    Late B finds north open / south shut; north's button (powered) closes
#    north; the maintenance button twice: Lua opens north on the second, the
#    click binding's sound dispatches BEFORE Lua's request, and north's chime
#    answers Lua's open. C walks onto the shock pad: hurt 40 once, teleported
#    once, sound. No joiner runs Lua or a binding. The second world's gate
#    blocks, powers open, admits.
# 5. A cyclic world (loop_a <-> loop_b, patched past OAL): the host survives,
#    the chain limit stops it naming the binding, and north's power still works.
# 6. Compatibility: a changed prefab binding (world bytes unchanged) and a
#    changed damage amount are refused before spawn; provenance is admitted.
set -e
cd "$(dirname "$0")/.."
trial=${HTA_TRIAL_DIR:-$(dirname "${HTA_MAP:-/nonexistent/x}")}
[ -f "$trial/bloodgulch.map" ] || { echo "SKIP: no Trial maps (HTA_TRIAL_DIR or HTA_MAP)"; exit 0; }
oal=${OAL_DIR:-../open-asset-lab}; [ -d "$oal" ] && oal=$(cd "$oal" && pwd)
py=$oal/.venv/bin/python; [ -x "$py" ] || py=python3
[ -d "$oal/assetlab" ] || { echo "SKIP: no Open Asset Lab at $oal (OAL_DIR)"; exit 0; }
build=${BUILD:-build-host}
cmake --build "$build" --target megamod-join megamod-match megamod-content megamod-resources >/dev/null
out=scratch/x7; rm -rf "$out"; mkdir -p "$out"
here=$PWD
fail() { echo "FAIL: $1" >&2; [ -f "$out/host.log" ] && { echo "--- host"; grep -E "world|bind|script|net" "$out/host.log" | tail -40; }; exit 1; }

# ---- 1. the contract ------------------------------------------------------------
./"$build"/megamod-resources --json > "$out/contract.json"
./"$build"/megamod-resources --conformance > "$out/conformance.json"
cmp -s "$out/contract.json" "$oal/assetlab/data/megamod_resources.json" ||
    fail "OAL's assetlab/data/megamod_resources.json is not this engine's contract (megamod-resources --json > it)"
cmp -s "$out/conformance.json" "$oal/assetlab/data/megamod_id_conformance.json" ||
    fail "OAL's assetlab/data/megamod_id_conformance.json is stale (megamod-resources --conformance > it)"

# ---- the packages -------------------------------------------------------------------
mkdir -p "$out/bundle/maps"
for w in x7_facility_world x7_second_world; do
    (cd "$oal" && "$py" -m assetlab fixture $w --output "$here/$out/bundle/maps/$w.oalmap" --packages "$here/$out/bundle/packages") \
        > "$out/fixture-$w.json" || fail "OAL could not build $w"
done
for p in x6.shared_assets x7.facility; do [ -f "$out/bundle/packages/$p.oalasset" ] || fail "OAL did not write $p"; done
(cd "$oal" && "$py" - "$here/$out" <<'EOF'
import json, struct, sys
from pathlib import Path
out = Path(sys.argv[1])
good = out / 'bundle'
world = (good / 'maps/x7_facility_world.oalmap').read_bytes()
fac = (good / 'packages/x7.facility.oalasset').read_bytes()
art = (good / 'packages/x6.shared_assets.oalasset').read_bytes()
wml, fml = (struct.unpack_from('<I', b, 8)[0] for b in (world, fac))

def patched_world(old, new):
    m = world[64:64 + wml]
    assert old.encode() in m, old
    mb = m.replace(old.encode(), new.encode(), 1)
    return world[:8] + struct.pack('<I', len(mb)) + world[12:64] + mb + world[64 + wml:]

def patched_lib(old, new):
    m = fac[32:32 + fml]
    assert old.encode() in m, old
    m = m.replace(old.encode(), new.encode(), 1)
    return fac[:8] + struct.pack('<I', len(m)) + fac[12:32] + m + fac[32 + fml:]

def variant(name, world_bytes=world, facility=fac):
    d = out / name
    (d / 'maps').mkdir(parents=True)
    (d / 'maps/x7_facility_world.oalmap').write_bytes(world_bytes)
    (d / 'packages').mkdir()
    (d / 'packages/x7.facility.oalasset').write_bytes(facility)
    (d / 'packages/x6.shared_assets.oalasset').write_bytes(art)

W, F = patched_world, patched_lib
shock = '"event":"entered","id":"shock","source":"x7:entity/shock_pad"'
# the world's side
variant('schema5', W('"schema":6,"scripts"', '"schema":5,"scripts"'))
variant('badevent', W(shock, shock.replace('entered', 'opened')))
variant('unknownevent', W(shock, shock.replace('entered', 'left')))
variant('badtarget', W('"target":"x7:entity/pad_dest"', '"target":"x7:entity/shock_pad"'))
variant('missingtarget', W('"target":"x7:entity/pad_dest"', '"target":"x7:entity/pad_dst"'))
variant('amount0', W('"amount":40.0', '"amount":0.0'))
variant('unknownaction', W('{"action":"damage"', '{"action":"explode"'))
variant('soundtype', W('"sound":"x7:sound/locked"}],"conditions":[],"event":"used"',
                       '"sound":"x7:prefab/security_door"}],"conditions":[],"event":"used"'))
variant('soundunimported', W('"resources":["x7:prefab/security_door","x7:sound/locked"]', '"resources":["x7:prefab/security_door"]'))
variant('order', W('"id":"shock"', '"id":"a_shock"'))
# the prefab's side
variant('childmissing', facility=F('"source":"door"', '"source":"dor"'))
variant('prefabtarget', facility=F('{"action":"toggle","target":"door"}', '{"action":"toggle","target":"power"}'))
variant('prefabactor', facility=F('{"action":"play_sound","at":"door","sound":"x7:sound/chime"}', '{"action":"damage","amount":5.0}'))
variant('prefabcond', facility=F('"is":"active"', '"is":"on"'))
variant('prefabschema', facility=F('"schema":2}', '"schema":1}'))
# a cycle no condition can break: OAL refuses it, the engine loads and bounds it
loops = ('{"actions":[{"action":"activate","target":"x7:entity/relay_b"}],"conditions":[],"event":"activated","id":"loop_a","source":"x7:entity/relay_a"},'
         '{"actions":[{"action":"activate","target":"x7:entity/relay_a"}],"conditions":[],"event":"activated","id":"loop_b","source":"x7:entity/relay_b"},'
         '{"actions":[{"action":"activate","target":"x7:entity/relay_a"}],"conditions":[],"event":"used","id":"loop_start","source":"x7:entity/maintenance"},')
lw = patched_world('"bindings":[', '"bindings":[' + loops)
m = lw[64:64 + struct.unpack_from('<I', lw, 8)[0]]
m = m.replace(b'"entities":[', b'"entities":[{"id":"x7:entity/relay_a","kind":"relay","links":[]},{"id":"x7:entity/relay_b","kind":"relay","links":[]},', 1)
variant('loop', lw[:8] + struct.pack('<I', len(m)) + lw[12:64] + m + lw[64 + struct.unpack_from('<I', lw, 8)[0]:])
# compatibility
variant('libmod', facility=F('{"action":"toggle","target":"door"}', '{"action":"open","target":"door"}'))
variant('amountmod', W('"amount":40.0', '"amount":41.0'))
fm = json.loads(fac[32:32 + fml])
fm['provenance']['x7:prefab/security_door']['creator'] = 'rebuilt on another machine'
mb = json.dumps(fm, sort_keys=True, separators=(',', ':')).encode()
variant('libprov', facility=fac[:8] + struct.pack('<I', len(mb)) + fac[12:32] + mb + fac[32 + fml:])
# neither world carries a binding of the prefab's, nor an asset byte
for w in ('x7_facility_world', 'x7_second_world'):
    wb = (good / f'maps/{w}.oalmap').read_bytes()
    mm = json.loads(wb[64:64 + struct.unpack_from('<I', wb, 8)[0]])
    assert 'prefabs' not in mm and b'MSH1' not in wb, w
    assert all(b['id'] not in ('chime', 'powered') for b in mm['world_entities'].get('bindings', [])), w
print('variants ok')
EOF
) > "$out/variants.log" || { cat "$out/variants.log"; fail "OAL could not build the variants"; }

# ---- 2. identity ------------------------------------------------------------------------
oal_key() { (cd "$oal" && "$py" -m assetlab world-key "$here/$out/$1/maps/$2.oalmap" --packages-dir "$here/$out/$1") | sed -n 's/.*"world_key": "\(.*\)".*/\1/p'; }
eng_key() { ./"$build"/megamod-content --trial "$trial" --bundle "$out/$1" --world $2 > "$out/content-$1-$2.log" 2>&1 ||
                { cat "$out/content-$1-$2.log"; fail "the engine could not load the $1 package set"; }
            sed -n 's/.* key \([0-9a-f]*\) .*/\1/p' "$out/content-$1-$2.log"; }
kg=$(eng_key bundle x7_facility_world); k2=$(eng_key bundle x7_second_world)
km=$(eng_key libmod x7_facility_world); ka=$(eng_key amountmod x7_facility_world); kp=$(eng_key libprov x7_facility_world)
kl=$(eng_key loop x7_facility_world)
[ -n "$kg" ] && [ "$kg" = "$(oal_key bundle x7_facility_world)" ] || fail "OAL's world key ($(oal_key bundle x7_facility_world)) is not the engine's ($kg)"
[ -n "$k2" ] && [ "$k2" = "$(oal_key bundle x7_second_world)" ] || fail "OAL's second-world key is not the engine's ($k2)"
[ "$km" = "$(oal_key libmod x7_facility_world)" ] && [ "$ka" = "$(oal_key amountmod x7_facility_world)" ] &&
    [ "$kp" = "$(oal_key libprov x7_facility_world)" ] || fail "OAL and the engine disagree on a variant's key"
[ "$km" != "$kg" ] || fail "a changed prefab binding kept the world key"
[ "$ka" != "$kg" ] || fail "a changed damage amount kept the world key"
[ "$kp" = "$kg" ] || fail "a provenance-only change changed the world key"
cmp -s "$out/bundle/maps/x7_facility_world.oalmap" "$out/libmod/maps/x7_facility_world.oalmap" || fail "libmod's world bytes should be unchanged"
for w in x7_facility_world x7_second_world; do
    ./"$build"/megamod-resources --bundle "$out/bundle" --world $w > "$out/resolved-$w.json" || fail "megamod-resources refused $w"
done
r=$out/resolved-x7_facility_world.json
"$py" - "$r" "$out/resolved-x7_second_world.json" <<'EOF' || fail "megamod-resources' bindings are not what they should be"
import json, sys
d = json.load(open(sys.argv[1]))
b = d['bindings']
assert len(b) == 2 + 7 + 7, len(b)
by = {(x['id'], x['origin']['instance'] if isinstance(x['origin'], dict) else 'world'): x for x in b}
nt, st = by[('toggle_door', 'north_door')], by[('toggle_door', 'south_door')]
assert nt['source'] == 'x7:entity/north_door__button' and nt['actions'][0]['target'] == 'x7:entity/north_door__door', nt
assert nt['conditions'][0] == {'condition': 'relay_state', 'entity': 'x7:entity/north_door__power', 'index': nt['conditions'][0]['index'], 'is': 'active'}
assert st['source'] == 'x7:entity/south_door__button' and st['actions'][0]['target'] == 'x7:entity/south_door__door', st
assert nt['actions'][0]['index'] != st['actions'][0]['index']
ch = by[('chime', 'north_door')]
assert ch['event'] == 'opened' and ch['actions'][0]['sound'] == 'x7:sound/chime' and ch['actions'][0]['at'] == 'x7:entity/north_door__door'
sh = by[('shock', 'world')]
assert [a['action'] for a in sh['actions']] == ['damage', 'teleport', 'play_sound'] and sh['actions'][0]['amount'] == 40
assert b[0]['id'] == 'maintenance_click' and b[1]['id'] == 'shock' and b[2]['origin']['instance'] == 'north_door'
s = json.load(open(sys.argv[2]))
assert len(s['bindings']) == 7 and all(x['origin']['instance'] == 'gate' for x in s['bindings'])
EOF
d1=$(sed -n 's/.*"x7.facility", "direct": true, "digest": "\([0-9a-f]*\)".*/\1/p' "$r")
d2=$(sed -n 's/.*"x7.facility", "direct": true, "digest": "\([0-9a-f]*\)".*/\1/p' "$out/resolved-x7_second_world.json")
[ -n "$d1" ] && [ "$d1" = "$d2" ] || fail "the two consumers do not share one prefab library ($d1, $d2)"
for w in x7_facility_world x7_second_world; do
    (cd "$oal" && "$py" -m assetlab resources check "$here/$out/bundle/maps/$w.oalmap" --packages-dir "$here/$out/bundle") \
        > "$out/oal-check-$w.log" || { cat "$out/oal-check-$w.log"; fail "OAL's checker refused $w"; }
done

# ---- 3. refusals: OAL first, the engine regardless -------------------------------------
refuse() {   # variant, message both must give
    local v=$1 want=$2
    (cd "$oal" && "$py" -m assetlab resources check "$here/$out/$v/maps/x7_facility_world.oalmap" --packages-dir "$here/$out/$v") \
        > "$out/oal-check-$v.log" 2>&1 && fail "OAL's checker accepted the $v variant"
    grep -qF -- "$want" "$out/oal-check-$v.log" || { cat "$out/oal-check-$v.log"; fail "OAL's checker did not say '$want' for $v"; }
    ./"$build"/megamod-resources --bundle "$out/$v" --world x7_facility_world > "$out/eng-$v.json" &&
        fail "the engine accepted the $v variant"
    grep -qF -- "$want" "$out/eng-$v.json" || { cat "$out/eng-$v.json"; fail "the engine did not say '$want' for $v"; }
    echo "  refused $v: $want"
}
refuse schema5 "world_entities: bindings need schema 6"
refuse badevent "binding shock: event 'opened' is not supported by trigger entity x7:entity/shock_pad (mover emits it)"
refuse unknownevent "binding shock: unknown event 'left' (a binding listens to used, activated, entered, deactivated, opened, closed)"
refuse badtarget "binding shock: action teleport targets x7:entity/shock_pad, a trigger, which does not afford teleport (teleport needs a teleport)"
refuse missingtarget "binding shock references missing placed entity x7:entity/pad_dst"
refuse amount0 "binding shock: action damage: amount must be in (0, 500]"
refuse unknownaction "binding shock: unknown action 'explode' (open, close, toggle, activate, deactivate, teleport, damage, play_sound, use)"
refuse soundtype "binding maintenance_click: sound x7:prefab/security_door is a prefab, expected a sound"
refuse soundunimported "binding maintenance_click: sound x7:sound/locked is provided by package x7.facility, which package x7.facility_world requires but does not import it from"
refuse order "world_entities: bindings are not in canonical (byte) order of id at a_shock"
refuse childmissing "prefab x7:prefab/security_door binding chime references missing child 'dor'"
refuse prefabtarget "prefab x7:prefab/security_door binding toggle_door: action toggle targets 'power', a relay, which does not afford toggle (toggle needs a mover)"
refuse prefabactor "prefab x7:prefab/security_door binding chime: action damage acts on the event's actor, and 'opened' never carries one"
refuse prefabcond "prefab x7:prefab/security_door binding power_off: condition relay_state: unknown value 'on' (inactive, active)"
refuse prefabschema "prefab x7:prefab/security_door: bindings need prefab schema 2 (this member is schema 1)"
(cd "$oal" && "$py" -m assetlab resources check "$here/$out/loop/maps/x7_facility_world.oalmap" --packages-dir "$here/$out/loop") \
    > "$out/oal-check-loop.log" 2>&1 && fail "OAL's checker accepted a binding cycle with no condition"
grep -qF "binding cycle with no condition: loop_a -> loop_b -> loop_a" "$out/oal-check-loop.log" ||
    { cat "$out/oal-check-loop.log"; fail "OAL did not name the binding cycle"; }
./"$build"/megamod-resources --bundle "$out/loop" --world x7_facility_world > "$out/eng-loop.json" ||
    fail "the engine refused the loop world (it should load it and bound it at run time)"
echo "  refused loop (OAL only): binding cycle with no condition: loop_a -> loop_b -> loop_a"

# ---- 4. play ------------------------------------------------------------------------------
port=$((35500 + $$ % 400))
export HTA_TRIAL_DIR="$trial"
J=(./"$build"/megamod-join 127.0.0.1 "$port" --world x7_facility_world --map "$trial/bloodgulch.map" --preset low)
./"$build"/megamod-match --bundle "$out/bundle" --world x7_facility_world --bots 0 --seconds 110 --trace-events \
    --host "$port" > "$out/host.log" 2>&1 &
host=$!
trap 'kill $host 2>/dev/null || true' EXIT
sleep 1.5
# A: blocked; north's button unpowered (locked, still blocked); north's power: open + chime; through.
a=$(timeout 60 "${J[@]}" --bundle "$out/bundle" --auto 25 \
    --route "-1.0,3.0;L0,3.0;B1.0,3.0;-0.9,2.3;L-0.16,2.3;E;w1.5;-1.0,3.0;L0,3.0;B1.0,3.0;-0.9,3.7;L-0.16,3.7;E;w2.5;-0.8,3.0;1.5,3.0;w1") ||
    { echo "$a"; fail "joiner A"; }
echo "$a" > "$out/a.log"
# B: late; north's button (powered) closes north; maintenance twice (Lua opens north on the second).
b=$(timeout 60 "${J[@]}" --bundle "$out/bundle" --auto 30 \
    --route "-0.9,2.3;L-0.16,2.3;E;w2.5;-5.3,0.5;L-5.84,0.5;E;w1.2;E;w3") || { echo "$b"; fail "late joiner B"; }
echo "$b" > "$out/b.log"
# C: through north's (open) doorway onto the shock pad.
c=$(timeout 50 "${J[@]}" --bundle "$out/bundle" --auto 25 --route "-1.0,3.0;1.2,3.0;2.5,-2.5;w3") || { echo "$c"; fail "joiner C"; }
echo "$c" > "$out/c.log"
m=$(timeout 30 "${J[@]}" --bundle "$out/libmod" --auto 4 || true); echo "$m" > "$out/c-libmod.log"
n=$(timeout 30 "${J[@]}" --bundle "$out/amountmod" --auto 4 || true); echo "$n" > "$out/c-amountmod.log"
p=$(timeout 30 "${J[@]}" --bundle "$out/libprov" --auto 4 || true); echo "$p" > "$out/c-libprov.log"
wait $host 2>/dev/null || true
# the second consumer: one powered door, turned 90
port2=$((port + 1))
./"$build"/megamod-match --bundle "$out/bundle" --world x7_second_world --bots 0 --seconds 25 --host "$port2" > "$out/host2.log" 2>&1 &
host=$!
sleep 1.5
s=$(timeout 35 ./"$build"/megamod-join 127.0.0.1 "$port2" --world x7_second_world --map "$trial/bloodgulch.map" --preset low \
    --bundle "$out/bundle" --auto 20 --route "1.5,-1.0;L1.5,0;B1.5,1.0;0.8,-0.9;L0.8,-0.16;E;w2.5;1.5,-1.0;1.5,1.0;w1") ||
    { echo "$s"; fail "second world joiner"; }
echo "$s" > "$out/s.log"
wait $host 2>/dev/null || true
# 5. the loop world
port3=$((port + 2))
./"$build"/megamod-match --bundle "$out/loop" --world x7_facility_world --bots 0 --seconds 25 --host "$port3" > "$out/host-loop.log" 2>&1 &
host=$!
sleep 1.5
l=$(timeout 35 ./"$build"/megamod-join 127.0.0.1 "$port3" --world x7_facility_world --map "$trial/bloodgulch.map" --preset low \
    --bundle "$out/loop" --auto 20 --route "-5.3,0.5;L-5.84,0.5;E;w1.5;-0.9,3.7;L-0.16,3.7;E;w2.5") || { echo "$l"; fail "loop joiner"; }
echo "$l" > "$out/l.log"
wait $host 2>/dev/null || true
trap - EXIT

H=$out/host.log
grep -q "\[world\] 16 event bindings (2 the world's own, 14 from prefab instances): run here (host)" "$H" || fail "the host did not load 16 bindings"
grep -q "\[script\] 1 scripts loaded (megamod.v1, host only)" "$H" || fail "the host did not load the maintenance script"
# A: the condition (unpowered: locked, no door), then the chain
grep -q "\[bind\]   x7:prefab/security_door binding locked (north_door): relay_state x7:entity/north_door__power is inactive -> true" "$H" ||
    fail "north's button did not read its power as inactive"
echo "$a" | grep -q "join: sound x7:sound/locked at x7:entity/north_door__button (the host's binding)" || fail "A did not hear locked"
[ "$(echo "$a" | grep -c "blocked at .* short of (1.00 3.00), as expected")" = 2 ] || fail "north's door did not block A twice (the locked press opened it?)"
for want in "event x7:entity/north_door__power_button used by unit" \
            "binding power_on (north_door): action activate x7:entity/north_door__power queued" \
            "event x7:entity/north_door__power activated by unit" \
            "binding powered (north_door): action open x7:entity/north_door__door queued" \
            "event x7:entity/north_door__door opened (depth 1)" \
            "binding chime (north_door): action play_sound x7:entity/north_door__door queued"; do
    grep -qF "$want" "$H" || fail "the chain's trace lacks '$want'"
done
echo "$a" | grep -q "join: sound x7:sound/chime at x7:entity/north_door__door (the host's binding)" || fail "A did not hear the chime"
echo "$a" | grep -q "route stuck" && fail "A could not walk through north's doorway"
echo "$a" | grep -q "at the end: mover x7:entity/south_door__door closed (t 0.00" || fail "north's power moved south's door"
grep -q "match: relay x7:entity/south_door__power inactive" "$H" || fail "north's power switched south's relay"
grep -q "match: relay x7:entity/north_door__power active" "$H" || fail "north's relay did not stay active"
# B: late join is state; the powered button closes; Lua and a binding on one press, in order
echo "$b" | grep -q "on joining mover x7:entity/north_door__door open (t 1.00" || fail "late joiner B did not find north open"
echo "$b" | grep -q "on joining mover x7:entity/south_door__door closed (t 0.00" || fail "late joiner B did not find south shut"
grep -q "binding toggle_door (north_door): relay_state x7:entity/north_door__power is active -> true" "$H" || fail "the powered button did not toggle"
grep -q "\[script\] x7:script/maintenance: maintenance press 2" "$H" || fail "the maintenance script did not count two presses"
grep -q "\[script\] x7:script/maintenance: maintenance override opens entity x7:entity/north_door__door which was closed" "$H" ||
    fail "the maintenance script did not open north's closed door"
"$py" - "$H" <<'EOF' || fail "a Lua request and a binding's action on one press were not dispatched binding first"
import sys
lines = open(sys.argv[1]).read().splitlines()
i = max(k for k, s in enumerate(lines) if 'dispatch play_sound -> x7:entity/maintenance (binding maintenance_click)' in s)
j = next(k for k in range(i, len(lines)) if 'dispatch open -> x7:entity/north_door__door (a direct request (Lua))' in lines[k])
assert all('[bind]' in s for s in lines[i:j + 1] if s.startswith('[')), lines[i:j + 1]
EOF
echo "$b" | grep -q "at the end: mover x7:entity/north_door__door open (t 1.00" || fail "north did not end open after the Lua override"
[ "$(grep -c "binding chime (north_door): action play_sound" "$H")" = 2 ] || fail "north's chime did not answer both opens (the power and Lua's)"
# C: the pad -- damage through the game, one teleport, a sound
grep -q "\[world\] unit [0-9]* hurt 40 by x7:entity/shock_pad (binding shock)" "$H" || fail "the pad did not hurt C"
[ "$(grep -c "hurt 40 by x7:entity/shock_pad" "$H")" = 1 ] || fail "the pad hurt more than once"
echo "$c" | grep -q "join: the host says we were hurt" || fail "C did not see its damage"
echo "$c" | grep -qE "join: the host moved us to \(-4.5[0-9] -4.0[0-9]" || fail "C was not teleported to the pad's destination"
[ "$(echo "$c" | grep -c "join: the host moved us")" = 1 ] || fail "C was teleported more than once"
echo "$c" | grep -q "join: sound x7:sound/locked at x7:entity/shock_pad (the host's binding)" || fail "C did not hear the pad"
grep -q "match: bindings: 16; .* 1 damage applied" "$H" || fail "the host's binding summary"
for who in a b c s l; do grep -qE "\[script\]|\[bind\]" "$out/$who.log" && fail "joiner $who ran a script or a binding"; done
# the second world
echo "$s" | grep -q "blocked at .* short of (1.50 1.00), as expected" || fail "the second world's gate did not block"
echo "$s" | grep -q "join: sound x7:sound/chime at x7b:entity/gate__door (the host's binding)" || fail "the second world's gate did not chime"
echo "$s" | grep -q "at the end: mover x7b:entity/gate__door open (t 1.00" || fail "the second world's gate did not open"
echo "$s" | grep -q "route stuck" && fail "the second world's joiner could not walk through"
# 5. the loop: bounded, named, and the world goes on
grep -q "\[world\] world events: chain too long (a cycle?) at x7:prefab/\|\[world\] world events: chain too long (a cycle?) at binding loop_" \
    "$out/host-loop.log" || fail "the loop was not stopped by the chain limit, named"
echo "$l" | grep -q "join: sound x7:sound/chime at x7:entity/north_door__door (the host's binding)" || fail "the loop host did not go on"
grep -q "match: .* simulated" "$out/host-loop.log" || fail "the loop host did not finish"
# 6. compatibility
echo "$m" | grep -q "REFUSED (not the host's map)" || fail "a changed prefab binding was not refused"
echo "$n" | grep -q "REFUSED (not the host's map)" || fail "a changed damage amount was not refused"
echo "$p" | grep -q "join: connected" || fail "a provenance-only change was not admitted"

echo "  contract and conformance: OAL's copies are this engine's"
echo "  world key $kg (OAL $(oal_key bundle x7_facility_world)); second world $k2; prefab binding $km, amount $ka, provenance $kp; loop $kl"
echo "  one prefab library, two consumers: digest $d1 in both"
grep -E "\[world\] (16 event|prefab)|\[bind\]|\[script\] x7|hurt|teleported" "$H" | head -80 | sed 's/^/  host /'
echo "$a" | grep -E "blocked|join: sound|at the end" | sed 's/^/  A /'
echo "$b" | grep -E "on joining|join: sound|at the end" | sed 's/^/  B /'
echo "$c" | grep -E "hurt|moved|join: sound" | sed 's/^/  C /'
echo "$s" | grep -E "blocked|join: sound|at the end" | sed 's/^/  second /'
grep -E "chain too long|bindings:" "$out/host-loop.log" | sed 's/^/  loop host /'
echo "  libmod:    $(echo "$m" | grep -E 'REFUSED|join: connected')"
echo "  amountmod: $(echo "$n" | grep -E 'REFUSED|join: connected')"
echo "  libprov:   $(echo "$p" | grep -E 'REFUSED|join: connected')"
echo "X7 test OK ($out)"
