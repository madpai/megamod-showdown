#!/bin/sh
# Milestone verification. Every check is pass/fail, no judgement calls.
# Usage: scripts/verify.sh
set -e
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk}
export ANDROID_HOME=${ANDROID_HOME:-$HOME/android/sdk}
NDK=$ANDROID_HOME/ndk/28.0.13004108
TOOLS=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
GRADLE=${GRADLE:-$HOME/android/gradle-8.9/bin/gradle}
pass=0; fail=0
ok()   { echo "  PASS  $1"; pass=$((pass+1)); }
bad()  { echo "  FAIL  $1"; fail=$((fail+1)); }

echo "== 1. host engine build + unit tests =="
cmake -B build-host -S . -G Ninja >/dev/null 2>&1 || true
if cmake --build build-host >/dev/null 2>&1; then ok "host build"; else bad "host build"; fi
if ./build-host/test_engine >/dev/null 2>&1; then ok "engine unit tests"; else bad "engine unit tests"; fi
if ./build-host/test_net >/dev/null 2>&1; then ok "UDP protocol and two-client session tests"; else bad "UDP protocol and two-client session tests"; fi
if ./build-host/test_cache  >/dev/null 2>&1; then ok "cache parser tests";  else bad "cache parser tests"; fi
if ./build-host/test_vehicle >/dev/null 2>&1; then ok "vehicle driving and collision tests"; else bad "vehicle driving and collision tests"; fi
if ./build-host/test_model >/dev/null 2>&1; then ok "model UV and lighting tests"; else bad "model UV and lighting tests"; fi
if ./build-host/test_bsp    >/dev/null 2>&1; then ok "bsp extraction tests"; else bad "bsp extraction tests"; fi
if ./build-host/test_camera >/dev/null 2>&1; then ok "camera/projection tests"; else bad "camera/projection tests"; fi
if ./build-host/test_player >/dev/null 2>&1; then ok "player/collision tests"; else bad "player/collision tests"; fi
if ./build-host/test_bitmap >/dev/null 2>&1; then ok "bitmap decode tests"; else bad "bitmap decode tests"; fi
if ./build-host/test_sound  >/dev/null 2>&1; then ok "Xbox ADPCM decoder tests"; else bad "Xbox ADPCM decoder tests"; fi
if ./build-host/test_audio  >/dev/null 2>&1; then ok "voice mixer tests"; else bad "voice mixer tests"; fi
if ./build-host/test_ammo   >/dev/null 2>&1; then ok "magazine/reload tests"; else bad "magazine/reload tests"; fi
if ./build-host/test_projectile >/dev/null 2>&1; then ok "projectile tests"; else bad "projectile tests"; fi
if ./build-host/test_particle   >/dev/null 2>&1; then ok "particle tests"; else bad "particle tests"; fi
if ./build-host/test_vitals     >/dev/null 2>&1; then ok "health/shield tests"; else bad "health/shield tests"; fi
if ./build-host/test_actor      >/dev/null 2>&1; then ok "world-space actor tests"; else bad "world-space actor tests"; fi
if ./build-host/test_pickup     >/dev/null 2>&1; then ok "pickup tests"; else bad "pickup tests"; fi
if ./build-host/test_bot        >/dev/null 2>&1; then ok "bot tests"; else bad "bot tests"; fi
if ./build-host/test_nav        >/dev/null 2>&1; then ok "nav grid tests"; else bad "nav grid tests"; fi
if ./build-host/test_game       >/dev/null 2>&1; then ok "game rules tests"; else bad "game rules tests"; fi
if ./build-host/test_ride       >/dev/null 2>&1; then ok "vehicle game rules (no map)"; else bad "vehicle game rules (no map)"; fi
if ./build-host/test_contrail   >/dev/null 2>&1; then ok "contrail tests (no map)"; else bad "contrail tests (no map)"; fi
if [ -n "$HTA_MAP" ] && [ -f "$HTA_MAP" ]; then
  if ./build-host/test_biped "$HTA_MAP" >/dev/null 2>&1; then ok "biped/globals physics from Trial map"; else bad "biped/globals physics from Trial map"; fi
  if ./build-host/test_anim "$HTA_MAP" >/dev/null 2>&1; then ok "FP animation graph + skinned viewmodel"; else bad "FP animation graph + skinned viewmodel"; fi
  if ./build-host/test_sound "$HTA_MAP" >/dev/null 2>&1; then ok "snd! tags + Xbox ADPCM from Trial data"; else bad "snd! tags + Xbox ADPCM from Trial data"; fi
  if ./build-host/test_ammo "$HTA_MAP" >/dev/null 2>&1; then ok "magazine values from the Trial weapon tag"; else bad "magazine values from the Trial weapon tag"; fi
  if ./build-host/test_hud "$HTA_MAP" >/dev/null 2>&1; then ok "crosshair from the weapon HUD tag"; else bad "crosshair from the weapon HUD tag"; fi
  if ./build-host/test_weapons "$HTA_MAP" >/dev/null 2>&1; then ok "roster animations + zoom from the weapon tags"; else bad "roster animations + zoom from the weapon tags"; fi
  if ./build-host/test_projectile "$HTA_MAP" >/dev/null 2>&1; then ok "projectiles from the projectile tags"; else bad "projectiles from the projectile tags"; fi
  if ./build-host/test_particle "$HTA_MAP" >/dev/null 2>&1; then ok "effect particles from the effect tags"; else bad "effect particles from the effect tags"; fi
  if ./build-host/test_vitals "$HTA_MAP" >/dev/null 2>&1; then ok "vitality and falling from the Trial tags"; else bad "vitality and falling from the Trial tags"; fi
  if ./build-host/test_vehicle "$HTA_MAP" >/dev/null 2>&1; then ok "Trial vehicles: all five types load, drive, fly and seat"; else bad "Trial vehicles: all five types load, drive, fly and seat"; fi
  if ./build-host/test_model "$HTA_MAP" >/dev/null 2>&1; then ok "Warthog textures and lighting"; else bad "Warthog textures and lighting"; fi
  if ./build-host/test_actor  "$HTA_MAP" >/dev/null 2>&1; then ok "the cyborg poses and dies"; else bad "the cyborg poses and dies"; fi
  if ./build-host/test_pickup "$HTA_MAP" >/dev/null 2>&1; then ok "what the map leaves on the ground"; else bad "what the map leaves on the ground"; fi
  if ./build-host/test_bot    "$HTA_MAP" >/dev/null 2>&1; then ok "a body to shoot at, and what hurts it"; else bad "a body to shoot at, and what hurts it"; fi
  if ./build-host/test_nav    "$HTA_MAP" >/dev/null 2>&1; then ok "a biped walks a planned path base to base"; else bad "a biped walks a planned path base to base"; fi
  if ./build-host/test_game   "$HTA_MAP" >/dev/null 2>&1; then ok "bots play Slayer to the score limit"; else bad "bots play Slayer to the score limit"; fi
  if ./build-host/test_contrail "$HTA_MAP" >/dev/null 2>&1; then ok "tracers and trails from the Trial's contrails"; else bad "tracers and trails from the Trial's contrails"; fi
  if ./build-host/test_ride   "$HTA_MAP" >/dev/null 2>&1; then ok "players drive, gun, splatter and bail from every vehicle"; else bad "players drive, gun, splatter and bail from every vehicle"; fi
fi

# Optional: validate against the user's own Trial data if HTA_MAP points at it.
if [ -n "$HTA_MAP" ] && [ -f "$HTA_MAP" ]; then
  echo "== 1a. real Trial data ($HTA_MAP) =="
  R=$(./build-host/htainfo "$HTA_MAP" --bsp --spawns 2>&1) || true
  echo "$R" | grep -q "DEMO/Trial"            && ok "detects Trial header" || bad "detects Trial header"
  echo "$R" | grep -q "engine          6"     && ok "engine == 6"          || bad "engine == 6"
  echo "$R" | grep -qE "spawn point\(s\)"   && ok "reads spawn points"   || bad "reads spawn points"
  echo "$R" | grep -q "submeshes"             && ok "extracts BSP geometry"|| bad "extracts BSP geometry"
  if echo "$R" | grep -q "skipped: 0 compressed, 0 malformed"; then ok "no materials skipped"; else bad "some materials skipped"; fi
else
  echo "  SKIP  real-data checks (set HTA_MAP=/path/to/bloodgulch.map to enable)"
fi

echo "== 1b. end-to-end CLI on a synthetic fixture =="
FIX=$(mktemp -d)/fix.map
if ./build-host/mkfixture "$FIX" 256 80 8 >/dev/null 2>&1; then ok "fixture generated"; else bad "fixture generated"; fi
if ./build-host/htainfo "$FIX" --bsp --spawns >/dev/null 2>&1; then ok "htainfo parses fixture (header+tags+bsp+spawns)"; else bad "htainfo parses fixture"; fi
if ./build-host/htainfo "$FIX" | grep -q 'DEMO/Trial'; then ok "htainfo detects Trial header layout"; else bad "htainfo detects Trial header layout"; fi
if ./build-host/htainfo /dev/null >/dev/null 2>&1; then bad "htainfo rejects junk input"; else ok "htainfo rejects junk input"; fi
rm -rf "$(dirname "$FIX")"

echo "== 1c. offscreen renderer (needs a host GPU; skipped if absent) =="
if [ -x ./build-host/htaview ]; then
  GRID=$(mktemp -d)/grid.map
  ./build-host/mkfixture "$GRID" --grid 64 64 4 >/dev/null 2>&1
  OUT=$(cd "$(dirname "$GRID")" && "$OLDPWD/build-host/htaview" "$GRID" --out r --width 320 --height 240 --shots 2 2>&1) || true
  if echo "$OUT" | grep -q "coverage"; then
    COV=$(echo "$OUT" | grep coverage | head -1 | sed -E 's/.*coverage +([0-9]+)\..*/\1/')
    if [ "${COV:-0}" -ge 3 ] 2>/dev/null; then ok "renderer draws geometry (coverage ${COV}%)"; else bad "renderer produced a blank frame (coverage ${COV}%)"; fi
  else
    echo "  SKIP  offscreen render (no usable GPU here)"
  fi
  rm -rf "$(dirname "$GRID")"

  # The viewmodel has to survive a real render, not just the unit test.
  if [ -n "$HTA_MAP" ] && [ -f "$HTA_MAP" ]; then
    VM=$(mktemp -d)
    OUT=$(cd "$VM" && "$OLDPWD/build-host/htaview" "$HTA_MAP" --fp idle --out vm --width 320 --height 240 --shots 1 2>&1) || true
    if echo "$OUT" | grep -q "verts.*hands.*gun"; then ok "first-person viewmodel renders"; else bad "first-person viewmodel renders"; fi
    OUT=$(cd "$VM" && "$OLDPWD/build-host/htaview" "$HTA_MAP" --drive 2 --out drive --width 320 --height 240 --shots 1 2>&1) || true
    if echo "$OUT" | grep -q "drive .*vehicles)" && echo "$OUT" | grep -q "coverage"; then
      ok "moving Warthog and chase camera render"
    else
      bad "moving Warthog and chase camera render"
    fi
    OUT=$(cd "$VM" && "$OLDPWD/build-host/htamatch" "$HTA_MAP" --bots 3 --seconds 4 --shots 1 --out bots --width 320 --height 240 2>&1) || true
    if echo "$OUT" | grep -q "bodies" && echo "$OUT" | grep -qE "holding [a-z]+, [1-9]"; then
      ok "bots render with their weapons in hand"
    else
      bad "bots render with their weapons in hand"
    fi
    OUT=$(cd "$VM" && "$OLDPWD/build-host/htamatch" "$HTA_MAP" --mode ctf --bots 4 --seconds 2 --shots 1 --out ctf --width 320 --height 240 2>&1) || true
    if echo "$OUT" | grep -qE "teams: red 0 blue 0, 4 flag parts drawn"; then
      ok "CTF renders both flags, pole and cloth, with the bots in team colours"
    else
      bad "CTF renders both flags, pole and cloth, with the bots in team colours"
    fi
    UI_MAP="$(dirname "$HTA_MAP")/ui.map"
    if [ -f "$UI_MAP" ]; then
      OUT=$(cd "$VM" && "$OLDPWD/build-host/htamenu" "$UI_MAP" --out menu --width 320 --height 180 2>&1) || true
      if echo "$OUT" | grep -qE "ring [1-9][0-9]* verts, sky [1-9]" && echo "$OUT" | grep -q "menu_00.ppm"; then
        ok "main menu renders from ui.map"
      else
        bad "main menu renders from ui.map"
      fi
      if echo "$OUT" | grep -q "shell          16/16 art, 48/48 words"; then
        ok "submenu art and words come from ui.map"
      else
        bad "submenu art and words come from ui.map"
      fi
    fi
    rm -rf "$VM"
  fi
else
  echo "  SKIP  htaview not built (host Vulkan missing)"
fi

if [ -x ./build-host/htaplay ] && [ -n "$HTA_MAP" ] && [ -f "$HTA_MAP" ]; then
  if scripts/test_two_players.sh >/dev/null 2>&1; then
    ok "two desktop Blood Gulch clients replicate movement and actions"
  else
    bad "two desktop Blood Gulch clients replicate movement and actions"
  fi
fi

echo "== 2. android APK build =="
if (cd android && $GRADLE --no-daemon -q :app:assembleDebug >/dev/null 2>&1); then
  ok "gradle assembleDebug"
else
  bad "gradle assembleDebug"; echo "  (aborting APK checks)"; echo; echo "$pass passed, $fail failed"; exit 1
fi

APK=android/app/build/outputs/apk/debug/app-debug.apk
[ -f "$APK" ] && ok "APK exists" || bad "APK exists"

echo "== 3. APK contents =="
unzip -l "$APK" | grep -q 'lib/arm64-v8a/libhta_native.so' && ok "arm64-v8a native lib present" || bad "arm64-v8a native lib present"
unzip -l "$APK" | grep -q 'lib/armeabi-v7a\|lib/x86' && bad "no unwanted ABIs" || ok "no unwanted ABIs"
unzip -l "$APK" | grep -qE 'classes[0-9]*\.dex' && ok "DEX present" || bad "DEX present"
AAPT=$(ls "$ANDROID_HOME"/build-tools/*/aapt 2>/dev/null | tail -1)
DEXDUMP=$(ls "$ANDROID_HOME"/build-tools/*/dexdump 2>/dev/null | tail -1)
if [ -n "$AAPT" ] && "$AAPT" dump badging "$APK" | grep -q "launchable-activity: name='net.hta.halotrial.SetupActivity'"; then
  ok "launcher is SetupActivity"
else
  bad "launcher is SetupActivity"
fi
if [ -n "$AAPT" ] && "$AAPT" dump permissions "$APK" | grep -q "uses-permission: name='android.permission.INTERNET'"; then
  ok "Android INTERNET permission present"
else
  bad "Android INTERNET permission present"
fi
if [ -n "$DEXDUMP" ] && "$DEXDUMP" "$APK" 2>/dev/null | grep -q "Lnet/hta/halotrial/SetupActivity;"; then
  ok "SetupActivity in DEX"
else
  bad "SetupActivity in DEX"
fi

rm -rf build-apkcheck && mkdir -p build-apkcheck
unzip -q -o "$APK" -d build-apkcheck
SO=build-apkcheck/lib/arm64-v8a/libhta_native.so
file "$SO" | grep -q 'ARM aarch64' && ok "native lib is aarch64" || bad "native lib is aarch64"
$TOOLS/llvm-nm -D --defined-only "$SO" 2>/dev/null | grep -q ' android_main' && ok "exports android_main" || bad "exports android_main"
$TOOLS/llvm-nm -D --defined-only "$SO" 2>/dev/null | grep -q 'ANativeActivity_onCreate' && ok "exports ANativeActivity_onCreate" || bad "exports ANativeActivity_onCreate"
$TOOLS/llvm-readelf -d "$SO" | grep -q 'libvulkan.so' && ok "links libvulkan" || bad "links libvulkan"
$TOOLS/llvm-readelf -d "$SO" | grep -q 'libaaudio.so' && ok "links libaaudio" || bad "links libaaudio"
# Nothing in the APK may be an audio asset: the user supplies sounds.map.
unzip -l "$APK" | grep -qiE '\.(wav|ogg|mp3|m4a|aac|opus)$' && bad "no audio bundled in the APK" || ok "no audio bundled in the APK"
# The gate builds the shareable APK: the owner's maps only ever go into a
# personal build made by publish_apk.sh --with-assets.
unzip -l "$APK" | grep -qiE '\.map$' && bad "no Trial maps in the shareable APK" || ok "no Trial maps in the shareable APK"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
