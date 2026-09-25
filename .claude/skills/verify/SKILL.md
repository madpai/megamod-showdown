---
name: verify
description: Prove a Megamod change works before claiming it -- host tests, the NDK check, and verify.sh with the owner's Trial map. Use after any code change and before any commit, push or publish.
---

# Verify a change

Run the strongest gate this machine can run, and report the numbers.

1. **With the owner's Trial data** (the dev machine has it):
   ```sh
   HTA_MAP=/home/commander/halo-trial-data/extract/maps/bloodgulch.map scripts/verify.sh
   ```
   Green means every check passed (80+ as of 2026-09-25). This is the only
   gate that runs the Trial-data tests (`test_game`, `test_nav`'s Blood Gulch
   half, ...) and the real Android build.
2. **Without Trial data** (a cloud box), at least:
   ```sh
   cmake -S . -B build-host && cmake --build build-host -j8 && (cd build-host && ctest)
   scripts/ndkcheck.sh          # platform_android.c: nothing else compiles it
   ```
   Say plainly that the Trial-data tests did not run.
3. **Touched `src/net/protocol.c`?** Also run the fuzz test under ASan
   (HANDOFF.md "Tools"): `build-asan/test_net_fuzz`.
4. **Touched the renderer?** `test_gfx_render` needs Vulkan (lavapipe works:
   `VK_ICD_FILENAMES=.../lvp_icd*.json`); it exits 77 (skipped) without it.

A failure is real until proven otherwise: root-cause it (docs/JOURNAL.md,
searched by symptom), do not re-run it away. CI on GitHub runs step 2 plus
sanitizers and the Gradle APK on every push; check it went green too.

Report: which gate ran, the pass count, anything skipped and why.
