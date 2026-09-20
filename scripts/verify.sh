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
if ./build-host/test_cache  >/dev/null 2>&1; then ok "cache parser tests";  else bad "cache parser tests"; fi
if ./build-host/test_bsp    >/dev/null 2>&1; then ok "bsp extraction tests"; else bad "bsp extraction tests"; fi
if ./build-host/test_camera >/dev/null 2>&1; then ok "camera/projection tests"; else bad "camera/projection tests"; fi
if ./build-host/test_player >/dev/null 2>&1; then ok "player/collision tests"; else bad "player/collision tests"; fi
if ./build-host/test_bitmap >/dev/null 2>&1; then ok "bitmap decode tests"; else bad "bitmap decode tests"; fi
if ./build-host/test_sound  >/dev/null 2>&1; then ok "Xbox ADPCM decoder tests"; else bad "Xbox ADPCM decoder tests"; fi
if ./build-host/test_audio  >/dev/null 2>&1; then ok "voice mixer tests"; else bad "voice mixer tests"; fi
if ./build-host/test_ammo   >/dev/null 2>&1; then ok "magazine/reload tests"; else bad "magazine/reload tests"; fi
if ./build-host/test_projectile >/dev/null 2>&1; then ok "projectile tests"; else bad "projectile tests"; fi
if ./build-host/test_particle   >/dev/null 2>&1; then ok "particle tests"; else bad "particle tests"; fi
if [ -n "$HTA_MAP" ] && [ -f "$HTA_MAP" ]; then
  if ./build-host/test_biped "$HTA_MAP" >/dev/null 2>&1; then ok "biped/globals physics from Trial map"; else bad "biped/globals physics from Trial map"; fi
  if ./build-host/test_anim "$HTA_MAP" >/dev/null 2>&1; then ok "FP animation graph + skinned viewmodel"; else bad "FP animation graph + skinned viewmodel"; fi
  if ./build-host/test_sound "$HTA_MAP" >/dev/null 2>&1; then ok "snd! tags + Xbox ADPCM from Trial data"; else bad "snd! tags + Xbox ADPCM from Trial data"; fi
  if ./build-host/test_ammo "$HTA_MAP" >/dev/null 2>&1; then ok "magazine values from the Trial weapon tag"; else bad "magazine values from the Trial weapon tag"; fi
  if ./build-host/test_hud "$HTA_MAP" >/dev/null 2>&1; then ok "crosshair from the weapon HUD tag"; else bad "crosshair from the weapon HUD tag"; fi
  if ./build-host/test_weapons "$HTA_MAP" >/dev/null 2>&1; then ok "roster animations + zoom from the weapon tags"; else bad "roster animations + zoom from the weapon tags"; fi
  if ./build-host/test_projectile "$HTA_MAP" >/dev/null 2>&1; then ok "projectiles from the projectile tags"; else bad "projectiles from the projectile tags"; fi
  if ./build-host/test_particle "$HTA_MAP" >/dev/null 2>&1; then ok "effect particles from the effect tags"; else bad "effect particles from the effect tags"; fi
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
    rm -rf "$VM"
  fi
else
  echo "  SKIP  htaview not built (host Vulkan missing)"
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

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
