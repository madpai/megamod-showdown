#!/usr/bin/env bash
# X10: real OAL content through the engine loader and a host-owned UDP race.
set -euo pipefail
cd "$(dirname "$0")/.."
oal=${OAL_DIR:-../open-asset-lab}
trial=${HTA_TRIAL_DIR:-/home/commander/halo-trial-data/extract/maps}
oal=$(cd "$oal" && pwd)
py="$oal/.venv/bin/python"
build=${BUILD:-build-host}
cmake --build "$build" --target megamod-match megamod-resources htanet test_racing_core mkfixture >/dev/null
"$build/test_racing_core"
out=$(mktemp -d "$PWD/scratch/x10-racing.XXXXXX")
engine=$(realpath "$build/megamod-resources")
(cd "$oal" && "$py" -m assetlab project verify projects/megamod_racing \
  --output "$out/verified" --engine "$engine")
bundle="$out/verified/bundle"
"$py" - "$out/verified/build.json" "$out/verified/verification.json" <<'PY'
import json, sys
a,v=(json.load(open(p)) for p in sys.argv[1:])
assert v['ok'], v['errors']
e=v['worlds'][0]['engine']
assert e['racing']=={'present':True,'laps':3,'grid':8,'gates':8,'pads':3,'max_speed':38.0,'boost_speed':52.0}
assert a['worlds'][0]['budget']['total']==27
print('race content key',e['world_key'],'and original prefabs/geometry agree')
PY
mkdir -p "$out/public/maps"
"$build/mkfixture" "$out/public/maps/bloodgulch.map" 256 80 8 >/dev/null
HTA_TRIAL_DIR="$out/public/maps" "$build/megamod-match" --bundle "$bundle" --world cinder_circuit \
  --bots 0 --seconds 18 --race-smoke > "$out/public.log" 2>&1
grep -q 'original Racing world: no Trial weapons required' "$out/public.log"
grep -q 'pad hits 1' "$out/public.log"
if [ ! -f "$trial/bloodgulch.map" ]; then trial="$out/public/maps"; fi
HTA_TRIAL_DIR="$trial" "$build/megamod-match" --bundle "$bundle" --world cinder_circuit \
  --bots 0 --seconds 18 --race-smoke > "$out/smoke.log" 2>&1
grep -q 'race smoke phase 2' "$out/smoke.log"
grep -q 'pad hits 1' "$out/smoke.log"
PYTHONPATH="$oal/projects/megamod_racing" "$py" -c \
  'import race_track01 as r; [print(*p) for p,_,_,_ in r.stations()]' > "$out/route.txt"
HTA_TRIAL_DIR="$trial" "$build/megamod-match" --bundle "$bundle" --world cinder_circuit \
  --bots 0 --seconds 205 --race-route "$out/route.txt" > "$out/drive.log" 2>&1
grep -q 'race route result .*finished 1' "$out/drive.log"
grep -q 'race route result .*round reset 1' "$out/drive.log"

port=$((43000 + $$ % 1000))
HTA_TRIAL_DIR="$trial" "$build/megamod-match" --bundle "$bundle" --world cinder_circuit \
  --bots 0 --seconds 21 --host "$port" > "$out/host.log" 2>&1 &
host=$!
trap 'kill "$host" 2>/dev/null || true' EXIT
for _ in {1..40}; do
  grep -q '\[net\] map check' "$out/host.log" && break
  sleep 0.1
done
map=$(sed -n 's/.*\[net\] map check \([0-9a-f]*\), content.*/\1/p' "$out/host.log" | head -1)
content=$(sed -n 's/.*content \([0-9a-f]*\) (.*/\1/p' "$out/host.log" | head -1)
[ -n "$map" ] && [ -n "$content" ]
if "$build/htanet" race-client 127.0.0.1 "$port" 2 00000000 "$content" > "$out/refused.log" 2>&1; then
  echo 'mismatched track joined' >&2; exit 1
fi
grep -q 'race-client refused reason=2' "$out/refused.log"
"$build/htanet" race-client 127.0.0.1 "$port" 16 "$map" "$content" > "$out/peer.log" 2>&1
wait "$host"
trap - EXIT
grep -q 'race-client connected=1 go=1 racer=1 motion=1' "$out/peer.log"
grep -q '1 peers at the end' "$out/host.log" || grep -q '0 peers at the end' "$out/host.log"
echo "X10 Racing: public synthetic bootstrap, deterministic original package, engine key, full three-lap route, wrong-key refusal, host/peer GO and motion ($out)"
