#!/bin/sh
# Back the whole project up to the owner's local backup drive: the working
# tree exactly as it is (builds, scratch, uploads, upstream clones included),
# a git bundle of every branch, the owner's own Trial data, and every
# published APK -- personal and guest -- kept per build.
#
#   scripts/backup_local.sh [--title "what this build is"]
#
# publish_apk.sh runs this after every publish. The destination is LOCAL
# ONLY: it holds the owner's Trial data and the map-bundled personal APK,
# neither of which may ever be shared or pushed anywhere.
#
# HTA_BACKUP_DIR overrides the destination (default below). If the drive is
# not mounted the backup is skipped with a warning, never an error, so a
# publish still succeeds.
set -eu
cd "$(dirname "$0")/.."

DEST=${HTA_BACKUP_DIR:-/mnt/media/backups/halo-trial-android}
DATA=${HTA_DATA_ROOT:-$HOME/halo-trial-data}
SERVE=${HTA_SERVE_ROOT:-$PWD/scratch/serve-megamod}   # MEGAMOD SHOWDOWN's page
TITLE=""
[ "${1:-}" = "--title" ] && TITLE=${2:-}

PARENT=$(dirname "$DEST")
if ! mountpoint -q /mnt/media 2>/dev/null && [ "${DEST#/mnt/media}" != "$DEST" ]; then
  echo "backup: /mnt/media is not mounted; skipped" >&2; exit 0
fi
[ -d "$PARENT" ] || { echo "backup: $PARENT does not exist; skipped" >&2; exit 0; }
APKS=$DEST/apks-megamod   # MEGAMOD SHOWDOWN builds, apart from Open Halo's apks/
mkdir -p "$APKS"

REV=$(git rev-parse --short HEAD)
git diff --quiet HEAD 2>/dev/null || REV="$REV-dirty"
STAMP=$(date '+%Y%m%d-%H%M%S')

# 1. The project, mirrored. --delete keeps it an exact copy of today's tree;
#    history lives in the bundle and the per-build APKs below.
rsync -a --delete ./ "$DEST/project/"

# 2. Every branch and commit in one file, restorable with `git clone`.
git bundle create "$DEST/halo-trial-android.bundle" --all 2>/dev/null

# 3. The owner's own Trial copy. Never deleted from the backup.
[ -d "$DATA" ] && rsync -a "$DATA/" "$DEST/halo-trial-data/"
#    And the installer it came from, wherever the owner keeps it.
for f in "$DATA"/*[Tt]rial[Ss]etup*.exe "$HOME"/projects/*[Tt]rial[Ss]etup*.exe; do
  [ -f "$f" ] && rsync -a "$f" "$DEST/halo-trial-data/installer/"
done

# 4. The published APKs, once per distinct build.
if [ -f "$SERVE/megamod-showdown.apk" ]; then
  SUM=$(sha256sum "$SERVE/megamod-showdown.apk" | cut -c1-12)
  if ! grep -qs "$SUM" "$APKS/INDEX"; then
    # Named for the commit the APK was built from, which the published
    # page records -- not whatever HEAD is now.
    BUILT=$(grep -o 'build [0-9a-f]\{7,\}[-a-z]*' "$SERVE/index.html" 2>/dev/null | head -1 | cut -d' ' -f2)
    REV=${BUILT:-$REV}
    OUT="$APKS/$STAMP-$REV"
    mkdir -p "$OUT"
    cp "$SERVE/megamod-showdown.apk" "$OUT/megamod-showdown-personal.apk"
    [ -f "$SERVE/megamod-showdown-guest.apk" ] && cp "$SERVE/megamod-showdown-guest.apk" "$OUT/megamod-showdown-guest.apk"
    [ -f "$SERVE/index.html" ] && cp "$SERVE/index.html" "$OUT/release-notes.html"
    (cd "$OUT" && sha256sum *.apk > SHA256SUMS)
    printf '%s  %s  %s  %s\n' "$STAMP" "$REV" "$SUM" \
      "${TITLE:-$(git log -1 --format=%s)}" >> "$APKS/INDEX"
    ln -sfn "$STAMP-$REV" "$APKS/latest"
    echo "backup: build $REV archived as apks-megamod/$STAMP-$REV"
  fi
fi

cat > "$DEST/README.txt" <<EOF
halo-trial-android -- local backup (PRIVATE: holds Trial data and the
map-bundled personal APK; never share or upload this folder).

project/                   the working tree as of the last backup
halo-trial-android.bundle  every branch: git clone halo-trial-android.bundle
halo-trial-data/           the owner's own Trial files (installer/ too)
apks/latest/               the newest build: megamod-showdown-personal.apk
                           (maps inside) and megamod-showdown-guest.apk
apks/INDEX                 every archived build: date, commit, hash, title

Last backup: $(date '+%Y-%m-%d %H:%M')  (commit $REV)
EOF
echo "backup: $DEST up to date ($(du -sh "$DEST" | cut -f1))"
