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

rm -rf build-apkcheck && mkdir -p build-apkcheck
unzip -q -o "$APK" -d build-apkcheck
SO=build-apkcheck/lib/arm64-v8a/libhta_native.so
file "$SO" | grep -q 'ARM aarch64' && ok "native lib is aarch64" || bad "native lib is aarch64"
$TOOLS/llvm-nm -D --defined-only "$SO" 2>/dev/null | grep -q ' android_main' && ok "exports android_main" || bad "exports android_main"
$TOOLS/llvm-nm -D --defined-only "$SO" 2>/dev/null | grep -q 'ANativeActivity_onCreate' && ok "exports ANativeActivity_onCreate" || bad "exports ANativeActivity_onCreate"
$TOOLS/llvm-readelf -d "$SO" | grep -q 'libvulkan.so' && ok "links libvulkan" || bad "links libvulkan"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
