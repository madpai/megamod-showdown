# Building and publishing from a local agent

Cloud sessions cannot reach the phone, the Trial data or the sideload
server: they build and test on a container and push a branch. A local
agent (Claude Code on the owner's desktop, where the Trial data, the
Android SDK and Tailscale are) turns that branch into a build on the
phone. Hand it this file.

Everything here runs on the desktop, in
`/home/commander/projects/halo-trial-android`, on branch `halo-sandbox`.

## 1. Bring the branch in

```sh
cd /home/commander/projects/halo-trial-android
git switch halo-sandbox
git status --short                      # must be clean; stop and ask if not
git fetch megamod
git merge --no-ff megamod/<branch>      # e.g. megamod/claude/vibrant-euler-xwhpy5
                                        # or `git pull megamod main` once it is merged
```

A conflict in `src/platform/platform_android.c` or `docs/HANDOFF.md` is the
usual one. Resolve it by keeping both sides' changes; if the same lines
changed on both sides, stop and ask the owner.

## 2. Verify with the Trial data

```sh
HTA_MAP=/home/commander/halo-trial-data/extract/maps/bloodgulch.map scripts/verify.sh
```

It must end green. The cloud cannot run the tests that need the map
(`test_game`, `test_biped`, `test_nav`'s Blood Gulch half, ...): this is
where they run for the first time, so a failure here is real, not a flake.
Read the failing test's output, fix the cause, commit, re-run.

## 3. Publish the personal build to the sideload page

```sh
scripts/publish_apk.sh --with-assets \
    --title "<the branch's headline>" \
    --notes-text "<two sentences: what to try, from CURRENT TESTING OBJECTIVE>"
```

This builds the personal APK (the owner's Trial maps and imported
`.oalmap`/`.oalasset` packages from `~/assetlab-private/bundle` inside it)
and the asset-free guest APK, and serves both on the Tailscale-only page
**http://100.89.1.14:8733/** (bind and port: `HTA_SERVE_BIND`,
`HTA_SERVE_PORT`). It also backs everything up to
`/mnt/media/backups/halo-trial-android`.

- **The personal APK never leaves the owner's devices.** A LAN friend
  installs the guest APK and picks their own maps.
- Check the page loads from the phone (or `curl -sI http://100.89.1.14:8733/`)
  and that the commit it shows is the merged one.
- A LAN match needs both phones on this build: the protocol version
  (`HTA_NET_VERSION` in `src/net/protocol.h`) refuses older builds.

## 4. Imported maps after an Asset Lab change

Asset Lab changes that alter packages (breakables, weather, brush
breakables) take effect only on re-converted maps. Re-convert the owner's
maps into `~/assetlab-private/bundle` with the new Asset Lab before step 3
when the branch notes say so. Packages are never committed to either repo.

## 5. Push and report

```sh
git rev-list --all --objects | grep -iE '\.(map|wav|ogg|oalmap|oalasset)$'   # must print nothing
git push megamod halo-sandbox:main
```

Tell the owner: the page URL, the commit, verify's result, and the first
item of CURRENT TESTING OBJECTIVE in `docs/HANDOFF.md`.

## Never

- Push `halo-sandbox` to `origin` (Open Halo, strictly Halo).
- Commit Trial `.map` files, sounds, imported packages or Workshop
  downloads. The repository is **public** since 2026-09-25.
- Give the `--with-assets` APK to anyone.
