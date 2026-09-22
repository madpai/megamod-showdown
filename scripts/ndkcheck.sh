#!/bin/sh
# Syntax/type check of the Android-only glue with the NDK, in a second.
# verify.sh (gradle) is still the real gate.
cd "$(dirname "$0")/.."
NDK=${ANDROID_HOME:-$HOME/android/sdk}/ndk/28.0.13004108
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang
"$CC" -fsyntax-only -Wall -Wextra -Wno-missing-field-initializers -I src \
  -I "$NDK/sources/android/native_app_glue" src/platform/platform_android.c "$@"
