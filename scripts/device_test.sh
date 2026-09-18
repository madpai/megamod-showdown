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
chk "\[probe\] mmap at 0x40440000" "Stage-2 fixed-map probe ran"

echo
echo "Now: touch the screen (colour should change), press BACK (should exit cleanly),"
echo "then re-run: $ADB logcat -d -s halo-trial-android | tail -20"
