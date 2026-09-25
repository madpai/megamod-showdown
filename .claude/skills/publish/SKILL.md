---
name: publish
description: Publish a Megamod build to the owner's phone -- the personal APK with their maps, and the asset-free guest APK -- on the Tailscale sideload page. Use when a verified change should reach the device.
---

# Publish to the phone

Only after the `verify` skill passed with the Trial map.

1. Update `docs/HANDOFF.md` **CURRENT TESTING OBJECTIVE**: what to try on the
   phone, in order, and what a failure looks like. Commit it.
2. Check no game data is in history, then publish:
   ```sh
   git rev-list --all --objects | grep -iE '\.(map|wav|ogg|oalmap|oalasset)$'   # must print nothing
   scripts/publish_apk.sh --with-assets --title "<headline>" --notes-text "<what to try; invented numbers>"
   ```
   It builds the personal APK (Trial maps + `~/assetlab-private/bundle`
   packages) and the guest APK, serves both on http://100.89.1.14:8733/,
   and backs everything up. Name any invented constant in the notes and add
   it to HANDOFF's ledger.
3. Confirm the page shows the new commit and both checksums pass.
4. `git push megamod halo-sandbox:main` (never `origin`: that is Open Halo).

Never: give anyone the `--with-assets` APK; commit maps, packages or
screenshots (the repository is public). LAN needs the same build and the
same map packages on both phones -- say so in the notes when either changed.

Report: the published commit, the page URL, and the first testing item.
