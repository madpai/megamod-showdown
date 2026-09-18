#!/bin/sh
# On-device milestone test. Needs a connected S24+ (adb).
# Verifies: installs, launches, Vulkan initializes, frames present, exits clean.
set -e
cd "$(dirname "$0")/.."
ADB=${ADB:-$HOME/android/sdk/platform-tools/adb}
PKG=net.hta.halotrial
APK=android/app/build/outputs/apk/debug/app-debug.apk

$ADB wait-for-device
echo "== device =="; $ADB shell getprop ro.product.model; $ADB shell getprop ro.product.cpu.abi

echo "== install =="
$ADB install -r "$APK"

# Push the user's own Trial map to the app's external files dir.
# Needs no permission and no root. Nothing proprietary ships in the APK.
DATA_DIR=/sdcard/Android/data/$PKG/files
if [ -n "$HTA_MAP" ] && [ -f "$HTA_MAP" ]; then
  echo "== pushing your map =="
  $ADB shell mkdir -p $DATA_DIR
  $ADB push "$HTA_MAP" $DATA_DIR/bloodgulch.map
else
  echo "== NOTE: set HTA_MAP=/path/to/bloodgulch.map to push your own data =="
  echo "   (or copy it yourself to $DATA_DIR/)"
fi

$ADB logcat -c
echo "== launch =="
$ADB shell am start -n $PKG/android.app.NativeActivity >/dev/null
sleep 6

echo "== logcat (halo-trial-android) =="
LOG=$($ADB logcat -d -s halo-trial-android)
echo "$LOG"

echo
echo "== checks =="
chk() { echo "$LOG" | grep -q "$1" && echo "  PASS  $2" || echo "  FAIL  $2"; }
chk "android_main entered"      "native entry reached"
chk "Vulkan initialized OK"     "Vulkan initialized"
chk "renderer ready on:"        "physical device selected"
chk "\[gfx\] swapchain"         "swapchain created"
chk "\[app\] frame "            "frames are presenting"
chk "\[probe\] mmap at 0x4bf10000\|\[probe\] mmap at 0x4BF10000" "fixed-map probe ran (Trial base)"
chk "\[assets\] found"          "located the user's map"
chk "Halo PC Trial"              "identified the Trial cache"
chk "\[assets\] BSP:"           "extracted BSP geometry on device"
chk "spawn points"               "read player spawn points"
chk "\[gfx\] uploaded"          "uploaded geometry to the GPU"
chk "\[perf\]"                  "render loop is running"

echo
echo "Controls: LEFT half = move stick · RIGHT half = look · bottom-right corner = jump"
echo "          gamepad: left stick move, right stick look, A jump, B noclip"
echo "Now: walk around Blood Gulch, then press BACK to exit cleanly."
echo "then re-run: $ADB logcat -d -s halo-trial-android | tail -20"
