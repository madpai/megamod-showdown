/* Blood Gulch geometry extraction.
 *
 * Walks: Scenario -> structure_bsps -> ScenarioStructureBSP
 *        -> lightmaps -> materials -> (vertices, surfaces)
 * and flattens the result into one vertex buffer + one u32 index buffer with a
 * submesh per material. See docs/BLOOD_GULCH_ASSETS.md for the dependency graph.
 *
 * Deliberately narrow: uncompressed environment vertices only. No compressed
 * vertices, no detail objects, no decals, no fog, no sky.
 */
#ifndef HTA_BSP_H
#define HTA_BSP_H

#include "cache.h"

/* ---- offsets, derived from Invader's definitions (validated: struct totals
 * match the declared sizes). See docs/INVADER_ASSET_PIPELINE.md §4. ---- */
#define HTA_SCENARIO_PLAYER_SPAWNS_OFF 0x354u  /* TagReflexive */
#define HTA_SCENARIO_STRUCTURE_BSPS_OFF 0x5A4u /* TagReflexive */

#define HTA_SCENARIO_BSP_ENTRY_SIZE 0x20u
#define HTA_BSP_COMPILED_HEADER_SIZE 0x18u

#define HTA_SBSP_LIGHTMAPS_BITMAP   0x000u  /* TagDependency */
#define HTA_SBSP_AMBIENT_COLOR      0x02Cu  /* ColorRGB */
#define HTA_SBSP_LIGHT0_COLOR       0x03Cu  /* ColorRGB */
#define HTA_SBSP_LIGHT0_DIRECTION   0x048u  /* Vector3D */
#define HTA_SBSP_COLLISION_BSP      0x0B0u  /* TagReflexive */
#define HTA_SBSP_WORLD_BOUNDS_X     0x0C8u
#define HTA_SBSP_SURFACES           0x0ECu  /* TagReflexive, 6 bytes each */
#define HTA_SBSP_LIGHTMAPS          0x0F8u  /* TagReflexive, 32 bytes each */

#define HTA_LIGHTMAP_MATERIALS_OFF  0x014u  /* TagReflexive within lightmap */
#define HTA_LIGHTMAP_ENTRY_SIZE     0x020u

#define HTA_MATERIAL_ENTRY_SIZE     0x100u
#define HTA_MAT_SHADER              0x000u  /* TagDependency */
#define HTA_MAT_SURFACES            0x014u  /* i32: first surface index */
#define HTA_MAT_SURFACE_COUNT       0x018u  /* i32 */
#define HTA_MAT_RENDERED_VTX_TYPE   0x0B0u  /* u16 VertexType */
#define HTA_MAT_RENDERED_VTX_COUNT  0x0B4u
#define HTA_MAT_RENDERED_VTX_OFFSET 0x0B8u

/* VertexType values we accept */
#define HTA_VTX_ENV_UNCOMPRESSED 0u
#define HTA_VTX_ENV_COMPRESSED   1u
#define HTA_VERTEX_ENV_UNCOMPRESSED_SIZE 56u

typedef struct { float pos[3]; float normal[3]; float uv[2]; } hta_vertex; /* 32 bytes */

typedef struct {
    uint32_t first_index;
    uint32_t index_count;
    uint32_t shader_tag_id;
    uint32_t lightmap_index;
} hta_submesh;

typedef struct {
    hta_vertex  *vertices;
    uint32_t     vertex_count;
    uint32_t    *indices;
    uint32_t     index_count;
    hta_submesh *submeshes;
    uint32_t     submesh_count;

    /* simple lighting straight out of the BSP */
    float ambient[3];
    float light0_color[3];
    float light0_dir[3];

    float bounds_min[3];
    float bounds_max[3];

    /* diagnostics */
    uint32_t materials_seen;
    uint32_t materials_skipped_compressed;
    uint32_t materials_skipped_bad;
} hta_bsp_mesh;

typedef struct {
    float    position[3];
    float    facing;      /* radians */
    uint16_t team_index;
    uint16_t bsp_index;
} hta_spawn_point;

/* Loads the first structure BSP referenced by the scenario.
 * Allocates into `out` (free with hta_bsp_free). Returns false + err on failure. */
bool hta_bsp_load_first(const hta_cache *c, hta_bsp_mesh *out,
                        char *err, size_t errlen);
void hta_bsp_free(hta_bsp_mesh *m);

/* Reads up to max player spawn points from the scenario. Returns count read. */
uint32_t hta_scenario_spawns(const hta_cache *c, hta_spawn_point *out, uint32_t max);

/* Reads a TagReflexive (count, pointer) at a file offset. */
bool hta_read_reflexive(const hta_cache *c, uint32_t off,
                        uint32_t *count, uint32_t *ptr);

#endif
