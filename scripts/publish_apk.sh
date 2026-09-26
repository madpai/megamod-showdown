#!/bin/sh
# Publish the current debug APK to the private Tailscale sideload page.
#
# This is the standard way to get a build onto the phone. It builds, copies the
# APK into the serve root, refreshes SHA256SUMS, stamps the page with the commit
# and build time, and starts the server if it is not already up.
#
#   scripts/publish_apk.sh --notes scratch/notes.html
#   scripts/publish_apk.sh --title "animated FP guns" --notes-text "Reinstall. ..."
#   scripts/publish_apk.sh --no-build          # publish what is already built
#   scripts/publish_apk.sh --with-assets ...   # PERSONAL build: the owner's own
#       Trial maps inside the APK (from HTA_DATA, default ~/halo-trial-data/
#       extract/maps). Such an APK must never be given to anyone else.
#
# The serve root lives in scratch/ (gitignored) so it survives across sessions.
# It used to sit in a session scratchpad under /tmp, which silently went stale.
set -e
cd "$(dirname "$0")/.."
# MEGAMOD SHOWDOWN has its own page, port and file names, so publishing it
# never replaces Open Halo's build on :8731.
ROOT=${HTA_SERVE_ROOT:-$PWD/scratch/serve-megamod}
BIND=${HTA_SERVE_BIND:-100.89.1.14}
PORT=${HTA_SERVE_PORT:-8733}
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk}
export ANDROID_HOME=${ANDROID_HOME:-$HOME/android/sdk}
GRADLE=${GRADLE:-$HOME/android/gradle-8.9/bin/gradle}
APK=android/app/build/outputs/apk/debug/app-debug.apk
SOURCE=$(git log -1 --format=%h 2>/dev/null || echo unknown)
if ! git diff --quiet || ! git diff --cached --quiet; then
  SOURCE="$SOURCE-dirty"
fi

BUILD=1
WITH_ASSETS=0
HTA_DATA=${HTA_DATA:-$HOME/halo-trial-data/extract/maps}
# Imported maps (Open Asset Lab .oalmap packages) for the personal build.
# Converted from the owner's own game files: never committed, never in the
# guest APK or a GitHub release.
HTA_IMPORTED=${HTA_IMPORTED:-$HOME/assetlab-private/bundle}
TITLE=""
NOTES_FILE=""
NOTES_TEXT=""
EMULATOR_APK=""
while [ $# -gt 0 ]; do
  case "$1" in
    --no-build)   BUILD=0 ;;
    --with-assets) WITH_ASSETS=1 ;;
    --title)      TITLE=$2; shift ;;
    --notes)      NOTES_FILE=$2; shift ;;
    --notes-text) NOTES_TEXT=$2; shift ;;
    # Build for the desktop's Android emulator too (x86_64 beside arm64),
    # write the APK to PATH, and stop: nothing is published or backed up.
    --emulator-apk) EMULATOR_APK=$2; shift ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done

if [ "$BUILD" = 1 ]; then
  echo "building APK…"
  PROPS="-PhtaVersionCode=$(git rev-list --count HEAD) -PhtaVersionName=$SOURCE"
  if [ "$WITH_ASSETS" = 1 ]; then
    STAGE=$PWD/scratch/apk-assets
    rm -rf "$STAGE"; mkdir -p "$STAGE/maps"
    for f in bloodgulch.map bitmaps.map sounds.map ui.map; do
      [ -f "$HTA_DATA/$f" ] || { echo "missing $HTA_DATA/$f" >&2; exit 1; }
      ln "$HTA_DATA/$f" "$STAGE/maps/$f" 2>/dev/null || cp "$HTA_DATA/$f" "$STAGE/maps/$f"
    done
    for f in "$HTA_IMPORTED"/*.oalmap; do
      [ -f "$f" ] || continue
      base=$(basename "$f" .oalmap)
      case "$base" in
        mcdonalds|mcronalds) echo "skipping $base"; continue ;;
      esac
      ln "$f" "$STAGE/maps/" 2>/dev/null || cp "$f" "$STAGE/maps/"
      echo "bundling imported map $base"
    done
    # Imported characters and weapons (.oalasset), same private source.
    for kind in characters weapons sounds; do
      for f in "$HTA_IMPORTED/$kind"/*.oalasset; do
        [ -f "$f" ] || continue
        mkdir -p "$STAGE/$kind"
        ln "$f" "$STAGE/$kind/" 2>/dev/null || cp "$f" "$STAGE/$kind/"
        echo "bundling imported $kind $(basename "$f" .oalasset)"
      done
    done
    PROPS="$PROPS -PhtaAssetsDir=$STAGE"
    # SEND REPORT posts to this page's /report: the owner's build only.
    PROPS="$PROPS -PhtaReportUrl=http://$BIND:$PORT/report"
    echo "bundling the owner's Trial data from $HTA_DATA (personal build)"
  fi
  [ -n "$EMULATOR_APK" ] && PROPS="$PROPS -PhtaAbis=arm64-v8a,x86_64"
  (cd android && $GRADLE --no-daemon -q :app:assembleDebug $PROPS)
fi
if [ -n "$EMULATOR_APK" ]; then
  [ -f "$APK" ] || { echo "no APK at $APK" >&2; exit 1; }
  mkdir -p "$(dirname "$EMULATOR_APK")"
  cp --reflink=never "$APK" "$EMULATOR_APK"
  echo "emulator APK ($SOURCE): $EMULATOR_APK"
  exit 0
fi
[ -f "$APK" ] || { echo "no APK at $APK (run without --no-build)" >&2; exit 1; }

mkdir -p "$ROOT/uploads" "$ROOT/symbols"
# The unstripped native library, per build: a crash report's lib+offsets
# become functions and lines with scripts/symbolize_report.py.
SYM=$(ls -t android/app/build/intermediates/cxx/Debug/*/obj/arm64-v8a/libhta_native.so 2>/dev/null | head -1)
if [ -n "$SYM" ]; then cp "$SYM" "$ROOT/symbols/libhta_native-$SOURCE.so"; echo "kept symbols for $SOURCE"; fi
# Materialize the copy before Gradle cleans its source; verify before
# replacing the served APK so a failed read cannot publish a broken file.
cp --reflink=never "$APK" "$ROOT/megamod-showdown.apk.tmp"
sha256sum "$ROOT/megamod-showdown.apk.tmp" >/dev/null
mv "$ROOT/megamod-showdown.apk.tmp" "$ROOT/megamod-showdown.apk"

# A guest needs the same code but must import their own Trial data. Build and
# publish that variant alongside the owner's personal APK for multiplayer QA.
if [ "$BUILD" = 1 ] && [ "$WITH_ASSETS" = 1 ]; then
  echo "building asset-free guest APK…"
  (cd android && $GRADLE --no-daemon -q :app:clean :app:assembleDebug \
      -PhtaVersionCode="$(git rev-list --count HEAD)" -PhtaVersionName="$SOURCE")
  if unzip -Z1 "$APK" | grep -qE '^assets/(maps|characters|weapons|sounds)/'; then
    echo "guest APK unexpectedly contains Trial maps" >&2; exit 1
  fi
  cp "$APK" "$ROOT/megamod-showdown-guest.apk"
fi

# Hashes so the phone can confirm it got the build you meant.
( cd "$ROOT" && : > SHA256SUMS
  for f in megamod-showdown.apk megamod-showdown-guest.apk bloodgulch.map bitmaps.map sounds.map; do
    if [ -f "$f" ]; then sha256sum "$f" >> SHA256SUMS; fi
  done )

[ -n "$TITLE" ] || TITLE=$(git log -1 --format=%s 2>/dev/null || echo "current build")
COMMIT=$SOURCE
STAMP=$(date '+%Y-%m-%d %H:%M')
SIZE=$(du -h "$ROOT/megamod-showdown.apk" | cut -f1)

if [ -n "$NOTES_FILE" ]; then
  NOTES=$(cat "$NOTES_FILE")
elif [ -n "$NOTES_TEXT" ]; then
  NOTES="<p>$NOTES_TEXT</p>"
else
  NOTES="<p>Reinstall this APK and report what you see.</p>"
fi

TITLE="$TITLE" NOTES="$NOTES" BUILD_LINE="build $COMMIT · $STAMP" SIZE="$SIZE" \
python3 - "$ROOT/index.html" scripts/sideload/index.html.tmpl <<'PY'
import html, os, sys
out, tmpl = sys.argv[1], sys.argv[2]
s = open(tmpl).read()
section = "<h2>Current test — %s</h2>\n%s" % (
    html.escape(os.environ["TITLE"]), os.environ["NOTES"])
s = (s.replace("{{TEST_SECTION}}", section)
      .replace("{{BUILD}}", html.escape(os.environ["BUILD_LINE"]))
      .replace("{{APK_SIZE}}", html.escape(os.environ["SIZE"])))
open(out, "w").write(s)
PY

# Start the server if nothing is already listening on this port.
if ! (ss -ltn 2>/dev/null || netstat -ltn 2>/dev/null) | grep -q ":$PORT "; then
  echo "starting server on $BIND:$PORT…"
  nohup python3 scripts/serve_poc.py --root "$ROOT" --bind "$BIND" --port "$PORT" \
      --mirror "$PWD/scratch/uploads" >> scratch/serve.log 2>&1 &
  sleep 1
else
  RUNNING_ROOT=$(tr '\0' '\n' < "/proc/$(pgrep -f "serve_poc.py.*--port $PORT" | head -1)/cmdline" 2>/dev/null \
                 | grep -A1 -- --root | tail -1)
  if [ -n "$RUNNING_ROOT" ] && [ "$RUNNING_ROOT" != "$ROOT" ]; then
    echo "WARNING: a server is running with root $RUNNING_ROOT, not $ROOT." >&2
    echo "         It will keep serving the old files. Kill it and rerun." >&2
  fi
fi

echo "published $SIZE  ->  http://$BIND:$PORT/"
echo "  $TITLE (build $COMMIT)"

# And onto the owner's backup drive: the project, the Trial data and this
# build's APKs. Skipped (not failed) when the drive is not mounted.
scripts/backup_local.sh --title "$TITLE" || echo "backup failed; the publish itself is fine" >&2
