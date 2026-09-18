# Blood Gulch: asset dependency graph

**Source:** the user's own `bloodgulch.map` from `HaloTrialSetup.exe`.
**Status:** measured from real data on 2026-09-18, not inferred.

---

## 1. The file

| Property | Value |
|---|---|
| Size | 14,459,752 bytes (13.8 MiB) |
| Header layout | **DEMO/Trial permutation** (`Ehed` @ 0x2C0, `Gfot` @ 0x5F0) |
| Engine | 6 (`CACHE_FILE_DEMO`) |
| Build | `01.00.00.0576` |
| Map type | 1 = multiplayer |
| Tag base address | `0x4BF10000` |
| Tag data | offset `0x851974`, size `0x5789F4` (5.47 MiB) |
| Tag count | **2410**, across **59 classes** |
| **Indexed tags** | **0 of 2410** |

### The external-resource question is settled

Phase 2a flagged as UNKNOWN whether Blood Gulch needs `bitmaps.map`/`sounds.map`.
**Answer: zero tags are flagged `indexed`.** Every tag's data lives inside
`bloodgulch.map` itself. The Trial ships `bitmaps.map` (79 MB) and
`sounds.map` (77 MB), but Blood Gulch does not reference them through the
indexed mechanism.

**Practical consequence: the vertical slice needs exactly one 13.8 MiB file.**
That is a very convenient onboarding story for Android — the user copies one
file, not a 170 MB directory. *(Caveat: this is proven for tag data. Whether
bitmap pixel data resolves entirely in-map is not yet proven, because we do not
sample textures yet — see §5.)*

## 2. Tag classes present (top of 59)

```
  class     count  indexed
  bitm        448        0
  effe        350        0
  snd!        333        0
  DeLa        324        0
  soso        117        0
  part        106        0
  jpt!         81        0
  mod2         70        0
  ustr         54        0
  schi         44        0
  ligh         39        0
  coll         38        0
  deca         38        0
  pphy         30        0
  lens         28        0
  devc         23        0
  antr         22        0
  wphi         22        0
  proj         21        0
  weap         20        0
  scen         19        0
```

## 3. The path to drawable geometry

Everything the vertical slice needs, with real numbers:

```
Scenario 'scnr'  (tag 0, id 0xE1740000, tag data @ file 0x880714)
 ├─ player starting locations  @ +0x354  -> 72 spawn points, teams 0 and 1
 └─ structure bsps             @ +0x5A4  -> 1 entry
      ScenarioBSP: start=0x800  size=0x172800  address=0x4D49D800
        │  (NOTE: the BSP block sits at file offset 0x800 — BEFORE the tag
        │   data region at 0x851974 — and its internal pointers resolve
        │   against 0x4D49D800, a different base from the tag base.)
        │
        ├─ compiled header (24 B) @ 0x800
        │     +0x00 pointer          = 0x4D49D818  -> the sbsp struct
        │     +0x08 rendered verts   = 0          <-- ZERO on PC/Trial
        │     +0x10 lightmap verts   = 0          <-- ZERO on PC/Trial
        │     +0x14 signature        = 'sbsp'
        │
        └─ ScenarioStructureBSP @ file 0x818
             ├─ lightmaps bitmap   @ +0x000  TagDependency -> 'bitm'
             ├─ collision materials@ +0x0A4  11
             ├─ collision bsp      @ +0x0B0  1        <-- for Phase 3 collision
             ├─ nodes              @ +0x0BC  12159
             ├─ world bounds       @ +0x0C8  SIX floats (3 min/max pairs)
             │      x  5.907 .. 131.902
             │      y -190.219 .. -45.057
             │      z  -0.349 .. 26.233
             ├─ leaves             @ +0x0E0  5971
             ├─ leaf surfaces      @ +0x0EC  14150
             ├─ surfaces           @ +0x0F8  **5503**  (3 x u16 each = indices)
             └─ lightmaps          @ +0x104  **16**
                  └─ materials (256 B each) -> **79 total**
                       ├─ shader              @ +0x000  TagDependency
                       ├─ surfaces / count    @ +0x014 / +0x018 (slice of the index array)
                       ├─ rendered vtx type   @ +0x0B0  = 0 (env uncompressed) for all 79
                       ├─ rendered vtx count  @ +0x0B4
                       ├─ rendered vtx offset @ +0x0B8  = 0  <-- UNUSED on PC
                       └─ uncompressed verts  @ +0x0D8  TagDataOffset
                              size    = count * 76
                              pointer = resolves against the BSP base  <-- REAL LOCATION
```

### Two corrections real data forced

**(a) `world bounds` is 24 bytes, not 12.** Invader's generated definition lists
`world bounds x/y/z` as three scalars; they are actually three **min/max pairs**.
This is exactly the 12-byte gap between the computed struct total (636) and the
declared size (648). Every field after it shifts by 12: `surfaces` is at
`0x0F8` (not `0x0EC`) and `lightmaps` at `0x104` (not `0x0F8`).
Confirmed because the bounds then read back as Blood Gulch's true extents.

**(b) Vertices are not where the Xbox docs say.** The compiled header's vertex
pointers are **zero** on PC/Trial, and `rendered vertices offset` is zero too.
Each material points at its own blob through the `uncompressed_vertices`
TagDataOffset, whose **pointer** resolves against the BSP base. The blob is:

```
[ count x 56-byte render vertices ][ count x 20-byte lightmap vertices ]
```

`count * 76` equals the advertised blob size exactly, for every material.

## 4. Extraction result

| Metric | Value |
|---|---|
| Vertices | **5,762** |
| Triangles | **5,503** |
| Submeshes (materials) | **79** |
| Materials skipped | **0** (0 compressed, 0 malformed) |
| Vertex types seen | `0` (environment uncompressed) — all 79 |
| Computed bounds | identical to the BSP's own `world bounds` ✅ |
| Parse time (host) | ~0.6 ms |
| GPU vertex+index memory | 5,762x32 + 16,509x4 = **250 KiB** |

Blood Gulch is **tiny** by modern standards. 5.5k triangles is nothing for an
S24+; the renderer will be bound by texture and lightmap work, not geometry.

## 5. What the slice deliberately does NOT use yet

| Asset | Status |
|---|---|
| `bitm` (448 tags) incl. the lightmap atlas | **not sampled yet** — flat shading with a fallback key light |
| `senv`/`soso`/`schi` shaders (176 tags) | not interpreted; submesh shader tag ids are recorded but unused |
| `collision bsp` (1 block) | not used; ground collision currently ray-casts the render mesh |
| `mod2` models (70) | no bases, weapons, vehicles, or characters |
| `scen`/`eqip`/`weap`/`vehi` placements | ignored |
| `snd!` (333), `effe` (350), `DeLa` UI (324) | out of scope for Phase 2 |
| sky, fog, weather, decals, detail objects, lens flares | out of scope |

**The BSP's default ambient and distant lights are all zero.** Real lighting
lives in the lightmap textures. Until those are sampled, the engine substitutes
a neutral outdoor key light (`src/engine/scene_light.h`), otherwise the map
renders nearly black.

## 6. Next dependencies, in order

1. **`bitm` decoding** — the lightmap atlas referenced by `lightmaps bitmap`,
   then per-material base textures via the `shader` dependency. This is the
   single biggest visual win and the next real milestone.
2. **`senv` shader parsing** — just enough to resolve a base map per submesh.
3. **`collision bsp`** — replace the render-mesh ground query with real
   collision surfaces, needed before movement can feel correct.
4. **`scen`/`mod2`** — the bases, so the map is recognisably playable.
