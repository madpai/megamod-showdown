---
name: convert-map
description: Convert a Source map (Garry's Mod, CS:S, TF2 BSP) into an OALMAP package for Megamod with Open Asset Lab, check it in the engine, and bundle it. Use for any new or re-imported map.
---

# Convert a map for Megamod

Open Asset Lab is at `~/projects/open-asset-lab`; packages live in
`~/assetlab-private/bundle` and are **never** committed anywhere.

1. Convert, mounting the game's own content:
   ```sh
   cd ~/projects/open-asset-lab
   python -m assetlab convert <map.bsp> --output ~/assetlab-private/<name>.oalmap \
       --game-dir "<game>/cstrike|tf|garrysmod" [--lightmaps]
   ```
   Breakable props, breakable windows (`func_breakable`) and the map's own
   weather come automatically. `--lightmaps` bakes Source's lighting (OALMAP
   v2): keep it OFF for bundled maps until the owner has approved lit maps on
   the phone; convert lit copies as `<name>-lit.oalmap` for comparison.
2. Check it in the engine, not just the converter:
   ```sh
   cd ~/projects/halo-trial-android
   build-host/open-halo-map-test ~/assetlab-private/<name>.oalmap scratch/<name>
   ```
   Every spawn must be usable. Read the compatibility report for missing
   textures and dropped features.
3. Bundle: copy into `~/assetlab-private/bundle` (keep the previous package
   beside it until the phone confirms the new one), then the `publish` skill.
   A re-imported map has a new identity: LAN guests need the new package too.

Report per map: breakables, windows, weather, lightmap pages (if lit), spawns
usable, and anything the report flagged.
