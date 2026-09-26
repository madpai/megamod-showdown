#!/bin/bash
# Boot the desktop's Android emulator (AVD "megamod", x86_64, host GPU,
# no window), build the emulator APK (x86_64 beside arm64, with the owner's
# assets) and install it. Then drive it with scripts/emu/emu.py.
#   scripts/emu/start.sh [--no-build]
set -e
SDK=${ANDROID_SDK:-$HOME/android/sdk}
ADB=$SDK/platform-tools/adb
APK=scratch/emulator/megamod-emulator.apk
if ! $ADB devices | grep -q "^emulator-"; then
  nohup "$SDK/emulator/emulator" -avd megamod -no-window -gpu host -no-snapshot-save -no-boot-anim \
    > scratch/emulator/emulator.log 2>&1 &
  $ADB wait-for-device
  until [ "$($ADB shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do sleep 2; done
fi
[ "$1" = "--no-build" ] || scripts/publish_apk.sh --with-assets --emulator-apk "$APK"
$ADB install -r -g "$APK" | tail -1
# The game's UDP port into the emulator, for hosting tests (see the skill).
$ADB emu redir add udp:32270:32270 >/dev/null 2>&1 || true
echo "emulator ready: $($ADB shell getprop ro.build.version.release | tr -d '\r'), $APK installed"
