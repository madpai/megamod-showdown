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
#define HTA_SBSP_COLLISION_MATERIALS 0x0A4u /* TagReflexive, 20 bytes each */
#define HTA_SBSP_COLL_MAT_SIZE       20u
#define HTA_SBSP_COLL_MAT_TYPE       18u    /* MaterialType */
#define HTA_SBSP_COLLISION_BSP      0x0B0u  /* TagReflexive */
/* World bounds are three min/max PAIRS (6 floats, 24 bytes), not 3 floats.
 * Invader's generated definition lists them as three scalars, which is what
 * made its struct total come out 12 bytes short; verified against real Trial
 * data, where these read as Blood Gulch's actual extents. Everything after
 * this point is therefore 12 bytes later than a naive reading suggests. */
#define HTA_SBSP_WORLD_BOUNDS       0x0C8u  /* float[6]: x0,x1,y0,y1,z0,z1 */
#define HTA_SBSP_LEAVES             0x0E0u  /* TagReflexive */
#define HTA_SBSP_LEAF_SURFACES      0x0ECu  /* TagReflexive */
#define HTA_SBSP_SURFACES           0x0F8u  /* TagReflexive, 6 bytes each */
#define HTA_SBSP_LIGHTMAPS          0x104u  /* TagReflexive, 32 bytes each */

#define HTA_LIGHTMAP_MATERIALS_OFF  0x014u  /* TagReflexive within lightmap */
#define HTA_LIGHTMAP_ENTRY_SIZE     0x020u

#define HTA_MATERIAL_ENTRY_SIZE     0x100u
#define HTA_MAT_SHADER              0x000u  /* TagDependency */
#define HTA_MAT_SURFACES            0x014u  /* i32: first surface index */
#define HTA_MAT_SURFACE_COUNT       0x018u  /* i32 */
#define HTA_MAT_RENDERED_VTX_TYPE   0x0B0u  /* u16 VertexType */
#define HTA_MAT_RENDERED_VTX_COUNT  0x0B4u
#define HTA_MAT_RENDERED_VTX_OFFSET 0x0B8u
/* On PC/Trial the BSP compiled header's vertex pointers are ZERO and the
 * material's "rendered vertices offset" is 0 as well. The real location is the
 * uncompressed_vertices TagDataOffset, whose `pointer` resolves against the
 * BSP's own base address. The blob holds rendered verts (56 bytes each)
 * immediately followed by lightmap verts (20 bytes each). Verified against
 * bloodgulch.map. */
#define HTA_MAT_UNCOMPRESSED_VERTS  0x0D8u  /* TagDataOffset */
#define HTA_TAGDATAOFFSET_SIZE      0x00u
#define HTA_TAGDATAOFFSET_POINTER   0x0Cu
#define HTA_VERTEX_LIGHTMAP_SIZE    20u

/* VertexType values we accept */
#define HTA_VTX_ENV_UNCOMPRESSED 0u
#define HTA_VTX_ENV_COMPRESSED   1u
#define HTA_VERTEX_ENV_UNCOMPRESSED_SIZE 56u

typedef struct {
    float pos[3];
    float normal[3];
    float uv[2];
    float lm_uv[2];
} hta_vertex; /* 40 bytes */

typedef struct {
    uint32_t first_index;
    uint32_t index_count;
    uint32_t shader_tag_id;
    uint32_t lightmap_index;  /* 0xFFFF = none */
    uint32_t albedo_tex;      /* index into hta_bsp_mesh.textures, ~0u = none */
    uint32_t lightmap_tex;
    /* Halo layers a high-frequency detail map over the base map; it is what
     * keeps ground from being one flat repeat of a low-resolution texture.
     * ~0u and 0.0 mean the surface has none. */
    uint32_t detail_tex;
    float    detail_scale;    /* repeats across the base map's span */
    /* Halo blends two detail maps by the base map's ALPHA: Blood Gulch's
     * ground is sand at 100x where the alpha is high and grass at 60x
     * where it is low, out of a single shader. */
    uint32_t detail2_tex;
    float    detail2_scale;
    /* A model shader can GATE its detail map by one channel of the
     * multipurpose map: `ShaderModelDetailMask` at +214 names which, and
     * whether to invert it. Channels are R auxiliary, G self-illumination,
     * B change colour, A reflection. 0 means no mask. */
    uint32_t multi_tex;
    uint8_t  detail_mask;
    uint8_t  draw_mode;       /* HTA_DRAW_* */
    bool     scene_lit;       /* placed model: scene light, not a BSP lightmap */
    /* HUD overlays only: the tag's own colour for this element. Halo's HUD
     * art is white with an alpha mask, so the colour lives here rather than
     * in the texture. {0,0,0,0} means untinted. */
    float    tint[4];
    /* HUD meters only: 0..1 fill. Negative means this is an ordinary sprite
     * and the meter path is off. */
    float    meter;
    /* HUD only: draw the art as a pure alpha MASK in the tint's colour,
     * ignoring its RGB. Halo's `hud_ammo_alphas` is exactly that -- black
     * art whose shape is the empty pip grid -- and drawing its colour paints
     * a black grid over the corner. */
    float    mask;
    /* HUD meters only: the colour Halo paints the part of the bar the fill
     * has NOT reached. It does not discard that part -- the assault rifle's
     * unfired pips are drawn in a dark navy, which is what makes a full
     * magazine read as full. w > 0 means this element has one. */
    float    empty[4];
    /* A chicago (schi / scex) layer: up to three maps, each with its own
     * repeat, folded together by the shader's colour and alpha functions.
     * The maps ride in albedo_tex, detail_tex and detail2_tex. Skies are
     * built this way -- `sky clear blue` is star twinkle x12 times stars x8,
     * added over the blue. `chicago` is the map count, 0 for none. */
    uint8_t  chicago;
    uint8_t  chicago_color[3], chicago_alpha[3];   /* ShaderColorFunctionType */
    float    chicago_scale[3][2];
} hta_submesh;

/* Clears a submesh to sane defaults. Use this rather than memset: several
 * of the texture slots mean "none" at ~0u, and 0 is a VALID texture index.
 * Leaving a zeroed detail_tex behind had the renderer bind each mesh's
 * first texture as its own detail map and multiply it in, which halved the
 * brightness of every weapon and every piece of scenery. */
void hta_submesh_init(hta_submesh *sm);

#define HTA_DRAW_OPAQUE 0u
#define HTA_DRAW_ALPHA  1u
#define HTA_DRAW_ADD    2u
#define HTA_DRAW_SKIP   3u
#define HTA_MATERIAL_NONE 0xFFu  /* sky portals: don't draw, let the sky show through */

typedef struct {
    uint32_t tag_id;
    uint32_t index;
    /* Halo tints a particle's sprite rather than shipping one bitmap per
     * colour: the needler's shards and its impact are the same white art
     * multiplied by magenta. We bake that into the decoded pixels, so the
     * tint is part of the cache key -- 0xFFFFFF is untinted. Doing it here
     * rather than in the shader costs one extra copy of a small sprite
     * sheet and keeps the push constants (already full at 128 bytes) and
     * the vertex format alone. */
    uint32_t tint;            /* 0xRRGGBB */
    uint32_t width, height;
    uint8_t *rgba;            /* malloc'd RGBA8 */
} hta_bsp_texture;

typedef struct {
    hta_vertex  *vertices;
    uint32_t     vertex_count;
    uint32_t    *indices;
    uint32_t     index_count;
    hta_submesh *submeshes;
    uint32_t     submesh_count;

    /* Collision meshes only: Halo's MaterialType per triangle (0 dirt,
     * 1 sand, 2 stone, ...), or HTA_MATERIAL_NONE. What you are standing on
     * decides which footstep the biped's `foot` tag plays. */
    uint8_t     *tri_material;   /* index_count / 3 entries */

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

    uint32_t         lightmaps_bitmap_id;
    hta_bsp_texture *textures;
    uint32_t         texture_count;
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

/* Flatten the structure collision BSP into a triangle mesh for the pawn.
 * Falls back to false if the collision BSP is missing; caller should keep
 * the render mesh. */
bool hta_bsp_load_collision(const hta_cache *c, hta_bsp_mesh *out,
                            char *err, size_t errlen);

/* Flatten one ModelCollisionGeometryBSP (object `coll` tag, cache pointers)
 * onto `dst`, running each vertex through `xform` if given. */
typedef void (*hta_coll_xform_fn)(float out[3], const float in[3], void *user);
/* `mat_lut` maps a collision surface's material index to Halo's MaterialType;
 * pass NULL when there is none and every triangle comes back
 * HTA_MATERIAL_NONE. */
bool hta_coll_bsp_append_mat(hta_bsp_mesh *dst, const hta_cache *c, uint32_t cb_off,
                             const uint8_t *mat_lut, uint32_t mat_count,
                             hta_coll_xform_fn xform, void *user,
                             char *err, size_t errlen);
bool hta_coll_bsp_append(hta_bsp_mesh *dst, const hta_cache *c, uint32_t cb_off,
                         hta_coll_xform_fn xform, void *user,
                         char *err, size_t errlen);

/* Reads up to max player spawn points from the scenario. Returns count read. */
uint32_t hta_scenario_spawns(const hta_cache *c, hta_spawn_point *out, uint32_t max);

/* Which spawn to come back at, given where you just died.
 *
 * Halo picks the spawn furthest from whatever is dangerous. With nobody else
 * in the map the only thing known to be dangerous is the spot that just
 * killed you, so that is what is avoided -- which also stops a fall into the
 * same hole repeating forever. `avoid` may be NULL, in which case one is
 * chosen at random. Returns an index, or 0 when there are none. */
uint32_t hta_scenario_spawn_pick(const hta_spawn_point *spawns, uint32_t count,
                                 const float avoid[3], uint32_t *rng);

/* Reads a TagReflexive (count, pointer) at a file offset. */
bool hta_read_reflexive(const hta_cache *c, uint32_t off,
                        uint32_t *count, uint32_t *ptr);

#endif
