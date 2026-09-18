# Invader & the Halo Trial Asset Pipeline

**Date:** 2026-09-18
**Purpose:** decide exactly what we reuse from Invader, what we implement ourselves, and what the Trial cache format actually looks like.

---

## 1. Decision up front

**Invader is used as a reference and an offline verification oracle. It is NOT embedded in our engine or the APK.**

| | Choice | Why |
|---|---|---|
| Runtime parsing | **Our own C parser** | Invader is C++ with exceptions/STL, built around a desktop toolkit (`std::vector<std::byte>` copies of whole maps). Our engine must run on Android with predictable memory and no surprise allocations. |
| Format knowledge | **From Invader's struct definitions** | Invader is **GPL-3.0-only** and its definitions are the best-maintained public description of the format. |
| Verification | **Invader as oracle** | `invader-info` / `invader-dependency` on the desktop give us a trusted second opinion to diff our parser against. |
| Preprocessing step | **Rejected for now** | A desktop "import" step would mean the user must run a converter on a PC before playing. The stated goal is *load my own Trial data on the phone*. We parse the original `.map` directly on-device. |

**Licence position:** Invader is GPL-3.0-only; our project is GPLv3, so reuse would be permitted. We nonetheless **did not copy Invader code**. What we used is the *format description* — field order, offsets, sizes, magic numbers — which is factual data about a file format, independently documented across community wikis, the official HEK, and multiple tools. Our parser is written from that description. This keeps our C code clean-room in structure while staying licence-compatible either way.

---

## 2. CACHE_FILE_DEMO: the Trial is not just "retail with different numbers"

This is the most important finding in this document.

`CacheFileEngine::CACHE_FILE_DEMO = 6`, and the engine record is `GAME_ENGINE_GEARBOX_DEMO`:

```
build_string          = "01.00.00.0576"   (not enforced)
base_memory_address   = 0x4BF10000        <-- NOT the retail 0x40440000
tag_space_length      = 23 MiB
supports_external_bitmaps_map = true
supports_external_sounds_map  = true
```

### 2.1 The Trial header layout is scrambled, not merely shifted

Retail and Trial both use a 0x800-byte header, but **the Trial permutes the fields**. Verified by summing Invader's `CacheFileDemoHeader` field/pad list — it totals exactly `0x800`:

| Offset | Field | Retail offset (for contrast) |
|---|---|---|
| `0x002` | `map_type` (u16) | `0x060` |
| `0x2C0` | **`head_literal`** | `0x000` |
| `0x2C4` | `tag_data_size` | `0x014` |
| `0x2C8` | `build` (32-byte string) | `0x040` |
| `0x588` | `engine` (u32, == 6) | `0x004` |
| `0x58C` | `name` (32-byte string) | `0x020` |
| `0x5B0` | `crc32` | `0x064` |
| `0x5E8` | `decompressed_file_size` | `0x008` |
| `0x5EC` | `tag_data_offset` | `0x010` |
| `0x5F0` | **`foot_literal`** | `0x7FC` |

### 2.2 The Trial even uses different magic values

```
retail:  head = 0x68656164 "head"     foot = 0x666F6F74 "foot"
TRIAL:   head = 0x45686564 "Ehed"     foot = 0x47666F74 "Gfot"
```

**Consequence:** any parser that sniffs `"head"` at offset 0 will reject a Trial map outright. Detection must probe **both** layouts. Our parser does, and reports which it matched.

### 2.3 Correction to a Phase 1 assumption

Phase 1's on-device probe used `0x40440000`, taken from Demon's `TAG_CACHE_BASE_ADDRESS`. That is the **retail/Custom Edition** base. **The Trial uses `0x4BF10000`.** The probe has been updated to test both, and the engine uses offset-based addressing regardless so neither value is load-bearing.

---

## 3. Layout we implement

### 3.1 Tag data header (at `tag_data_offset`, 0x28 bytes)

```
0x00  u32  tag_array_address     (a pointer in tag space)
0x04  u32  scenario_tag          (TagID)
0x08  u32  tag_file_checksums
0x0C  u32  tag_count
0x10  u32  model_part_count
0x14  u32  model_data_file_offset
0x18  u32  model_part_count_again
0x1C  u32  vertex_size
0x20  u32  model_data_size
0x24  u32  tags_literal          ("tags" = 0x74616773)
```

### 3.2 Tag array entry (0x20 bytes each, `static_assert`-confirmed in Invader)

```
0x00  u32  primary_class    (FourCC, e.g. 'scnr' 'sbsp' 'bitm' 'shdr')
0x04  u32  secondary_class
0x08  u32  tertiary_class
0x0C  u32  tag_id
0x10  u32  tag_path         (pointer into tag space -> NUL-terminated string)
0x14  u32  tag_data         (pointer into tag space -> the tag's struct)
0x18  u32  indexed          (nonzero = data lives in an external resource map)
0x1C  u32  pad
```

### 3.3 Pointer translation — the one rule everything depends on

Tag-space pointers are absolute, assuming the tag data region is mapped at `base_memory_address`. To convert to a file offset:

```
file_offset = tag_space_pointer - base_memory_address + tag_data_offset
```

We do this arithmetic explicitly and bounds-check every result. **We deliberately do not `mmap` at a fixed address**, so the engine is 64-bit-clean and immune to the address-space question Phase 1 raised.

### 3.4 Container primitives

| Primitive | Size | Layout (32-bit cache) |
|---|---|---|
| `TagDependency` | `0x10` | fourcc(4), path_pointer(4), path_size(4), tag_id(4) |
| `TagReflexive` (array) | `0x0C` | count(4), pointer(4), unused(4) |
| `TagDataOffset` (blob) | `0x14` | size(4), external(4), file_offset(4), pointer(4), unused(4) |

Invader models the pointer fields as `Pointer64`, which lands on the same total size because the upper 4 bytes are zero in 32-bit maps. We read them as explicit `u32` for clarity.

---

## 4. Geometry path to Blood Gulch

Traced through Invader's generated definitions:

```
Scenario ('scnr', tag_data_header.scenario_tag)
 └─ structure_bsps  TagReflexive -> ScenarioBSP (32 bytes)
      0x00 u32 bsp_start     file offset of the BSP block
      0x04 u32 bsp_size
      0x08 u32 bsp_address   base address for pointers INSIDE the BSP
      0x0C pad(4)
      0x10 TagDependency structure_bsp ('sbsp')
        └─ ScenarioStructureBSP (648 bytes)
             ├─ lightmaps_bitmap      TagDependency ('bitm')  <- lightmap texture atlas
             ├─ default ambient / distant light 0+1 colour & direction  <- simple lighting, free
             ├─ collision_bsp         TagReflexive -> ModelCollisionGeometryBSP
             ├─ surfaces              TagReflexive -> 6 bytes = u16 v0,v1,v2   <-- INDEX BUFFER
             └─ lightmaps             TagReflexive -> ScenarioStructureBSPLightmap (32 bytes)
                  ├─ bitmap          Index into lightmaps_bitmap
                  └─ materials       TagReflexive -> ScenarioStructureBSPMaterial (256 bytes)
                       ├─ shader                   TagDependency
                       ├─ surfaces (i32), surface_count (i32)   <- slice of the index buffer
                       ├─ rendered_vertices_type/count/offset   <-- VERTEX BUFFER
                       ├─ lightmap_vertices_type/count/offset
                       ├─ uncompressed_vertices    TagDataOffset
                       └─ compressed_vertices      TagDataOffset
```

**Critical quirk:** a BSP is **not** inside the main tag data region. It sits at its own `bsp_start` file offset, and pointers within it resolve against `bsp_address`, not the cache base. Two different pointer bases in one file.

### 4.1 Vertex formats

Taken from Demon's `source/rasterizer/rasterizer_geometry.h` (GPLv3; the author explicitly declares struct layouts to be public information), and consistent with Invader's `VertexType` enum:

```c
/* _rasterizer_vertex_type_environment_uncompressed  == 56 bytes */
struct environment_vertex_uncompressed {
    float position[3];   /*  0 */
    float normal[3];     /* 12 */
    float binormal[3];   /* 24 */
    float tangent[3];    /* 36 */
    float texcoord[2];   /* 48 */
};                       /* 56 */

/* _rasterizer_vertex_type_environment_lightmap_uncompressed == 20 bytes */
struct environment_lightmap_vertex_uncompressed {
    float incident_radiosity[3]; /*  0 */
    float texcoord[2];           /* 12 */
};                               /* 20 */
```

For the vertical slice we need **position** (and later `texcoord` + `normal`). Binormal/tangent are ignorable until we do normal-mapped shaders.

---

## 5. What we implement vs. skip for the vertical slice

| Asset class | FourCC | Slice? | Note |
|---|---|---|---|
| Scenario | `scnr` | **Yes** | entry point; BSP list, spawn points |
| Structure BSP | `sbsp` | **Yes** | the geometry |
| Bitmap | `bitm` | **Yes (later)** | lightmaps + base textures |
| Shader (environment) | `shdr`/`senv` | **Minimal** | just enough to find the base texture |
| Collision geometry | (in `sbsp`) | **Yes (stage 8)** | player collision |
| Model / gbxmodel | `mod2` | No | no characters or vehicles in the slice |
| Sound, AI, weapons, effects, UI | — | No | out of scope |

**Explicitly deferred:** compressed vertices, detail objects, decals, lens flares, fog, weather, pathfinding, breakable surfaces, sky. Blood Gulch will look wrong before it looks right; that is acceptable and intended.

---

## 6. External resource maps — an open risk

The Trial record sets `supports_external_bitmaps_map = true` and `supports_external_sounds_map = true`. Tag array entries carry an `indexed` flag; when set, the tag's data lives in `bitmaps.map` / `sounds.map` beside `bloodgulch.map`, not in the map itself.

**Consequence for us:** geometry should be self-contained in the BSP, but **textures may well require `bitmaps.map`**. So the data directory the user supplies must include the whole `maps/` folder, not a single file.

**Status: UNKNOWN until tested against real Trial data** — specifically, how many of Blood Gulch's `bitm` tags are flagged `indexed`. Our parser counts and reports this, so the first real run answers it. Geometry (Phase 2's actual milestone) does not depend on the answer.

---

## 7. What we reuse, concretely

| Item | Source | Form |
|---|---|---|
| Demo header layout + magic | Invader `include/invader/hek/map.hpp` | **facts**, reimplemented |
| Base address, tag space, build string | Invader `src/hek/map.cpp` | **facts**, reimplemented |
| Tag array / data header layout | Invader | **facts**, reimplemented |
| BSP → lightmap → material → vertex path | Invader generated definitions (`src/tag/hek/definition/*.json`) | **facts**, reimplemented |
| Vertex struct layouts | Demon `rasterizer_geometry.h` | **facts** (declared public info) |
| Cross-check of our parser's output | `invader-info` binary | **oracle**, offline |

No Invader or Demon source files are compiled into our engine or shipped in the APK.
