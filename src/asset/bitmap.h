/* Bitmap tag decoder + environment-shader base-map lookup.
 *
 * Gearbox Trial stores pixel bytes in bitmaps.map (BitmapData flag
 * "external"), not in bloodgulch.map. The tag-array `indexed` field is a
 * different mechanism and is zero for every Blood Gulch tag.
 *
 * We decode mip 0 to tightly packed RGBA8. Formats actually used by the
 * Trial (measured): DXT1/3/5, R5G6B5, A1R5G5B5, A4R4G4B4, X8R8G8B8, A8R8G8B8.
 */
#ifndef HTA_BITMAP_H
#define HTA_BITMAP_H

#include "cache.h"
#include "bsp.h"

#define HTA_BITM_DATA_REFLEXIVE  0x60u  /* TagReflexive of BitmapData */
#define HTA_BITM_DATA_SIZE       48u
#define HTA_BITM_DATA_WIDTH      0x04u
#define HTA_BITM_DATA_HEIGHT     0x06u
#define HTA_BITM_DATA_FORMAT     0x0Cu
#define HTA_BITM_DATA_FLAGS      0x0Eu
#define HTA_BITM_DATA_PIXEL_OFF  0x18u
#define HTA_BITM_DATA_PIXEL_SIZE 0x1Cu

#define HTA_BITM_FLAG_EXTERNAL   (1u << 8)

#define HTA_FMT_R5G6B5    6u
#define HTA_FMT_A1R5G5B5  8u
#define HTA_FMT_A4R4G4B4  9u
#define HTA_FMT_X8R8G8B8  10u
#define HTA_FMT_A8R8G8B8  11u
#define HTA_FMT_DXT1      14u
#define HTA_FMT_DXT3      15u
#define HTA_FMT_DXT5      16u

/* ShaderEnvironment.base_map TagDependency, after the 40-byte Shader header. */
#define HTA_SENV_BASE_MAP  0x88u

typedef struct {
    const uint8_t *data;
    size_t         size;
    uint32_t       type;   /* 1 = bitmaps */
} hta_resource_map;

typedef struct {
    uint32_t  width, height;
    uint8_t  *rgba;        /* width*height*4, malloc'd; free with hta_bitmap_free */
} hta_bitmap;

void hta_bitmap_free(hta_bitmap *b);

/* Opens a Gearbox resource map. Does not copy the bytes.
 * Type is the first word: 1 = bitmaps, 2 = sounds, 3 = loc. */
#define HTA_RESOURCE_BITMAPS 1u
#define HTA_RESOURCE_SOUNDS  2u
bool hta_resource_open_typed(hta_resource_map *r, const uint8_t *data, size_t size,
                             uint32_t expect_type, char *err, size_t errlen);
/* bitmaps.map, for the call sites that only ever want that. */
bool hta_resource_open(hta_resource_map *r, const uint8_t *data, size_t size,
                       char *err, size_t errlen);

/* Decode bitmap_data[index] of a 'bitm' tag to RGBA8.
 * `bitmaps` may be NULL if the pixel data is in the cache (it is not, on Trial). */
bool hta_bitmap_decode(const hta_cache *c, const hta_resource_map *bitmaps,
                       uint32_t tag_id, uint32_t index,
                       hta_bitmap *out, char *err, size_t errlen);

/* First bitmap tag referenced by a shader (senv base map, else first 'bitm'
 * TagDependency). Returns 0 if none. */
uint32_t hta_shader_base_bitmap(const hta_cache *c, uint32_t shader_tag_id);

/* Decode every unique albedo + lightmap referenced by the mesh. Fills
 * mesh->textures and per-submesh albedo_tex/lightmap_tex. Missing resource
 * map is not fatal: those slots stay ~0u (renderer uses a default). */
bool hta_bsp_load_textures(const hta_cache *c, const hta_resource_map *bitmaps,
                           hta_bsp_mesh *mesh, char *err, size_t errlen);

/* Intern a decoded bitmap into mesh->textures. Returns slot or ~0u. */
uint32_t hta_mesh_intern_bitmap(hta_bsp_mesh *mesh, const hta_cache *c,
                                const hta_resource_map *bitmaps,
                                uint32_t tag_id, uint32_t index);

uint8_t hta_shader_draw_mode(const hta_cache *c, uint32_t shader_tag_id);

/* `shader_transparent_chicago` numeric counter limit: nonzero means the
 * shader is a readout whose texture frame is chosen by a counter, rather
 * than a fixed picture. The assault rifle's two ammo digits carry 60 (its
 * magazine); its compass carries 8 (the points of the compass). */
uint8_t hta_shader_numeric_limit(const hta_cache *c, uint32_t shader_tag_id);

/* How many frames a bitmap holds (its bitmap_data count). A digit readout is
 * a ten-frame bitmap: one image per digit, not a sprite sheet. */
uint32_t hta_bitmap_frame_count(const hta_cache *c, uint32_t tag_id);

/* Decode `count` frames of `tag_id` side by side into one RGBA atlas and
 * intern it as a mesh texture, so a quad can pick a frame by shifting U.
 * Returns the texture slot, or ~0u. */
uint32_t hta_mesh_intern_atlas(hta_bsp_mesh *mesh, const hta_cache *c,
                               const hta_resource_map *bitmaps,
                               uint32_t tag_id, uint32_t count);

/* Sprite-sheet bitmaps (`type` 3). Halo stores each variant as its own
 * "bitmap group sequence" holding a single sprite, and picks a sequence at
 * random when it spawns a particle -- that is why one muzzle flash never
 * looks quite like the last.
 *
 * Bitmap 108: sequences at +84, bitmap data at +96.
 * BitmapGroupSequence 64: sprites at +52.
 * BitmapGroupSprite 32: bitmap index at 0, then left/right/top/bottom. */
typedef struct {
    uint32_t bitmap_index;   /* which bitmap_data the sprite lives in */
    float    u0, u1, v0, v1;
} hta_bitmap_sprite;

uint32_t hta_bitmap_sequence_count(const hta_cache *c, uint32_t tag_id);
/* First sprite of sequence `seq`. */
bool hta_bitmap_sprite_at(const hta_cache *c, uint32_t tag_id, uint32_t seq,
                          hta_bitmap_sprite *out);

/* Decode a raw pixel blob (used by unit tests; no cache involved). */
bool hta_bitmap_decode_pixels(uint16_t format, uint32_t w, uint32_t h,
                              const uint8_t *src, uint32_t src_len,
                              hta_bitmap *out, char *err, size_t errlen);

#endif
