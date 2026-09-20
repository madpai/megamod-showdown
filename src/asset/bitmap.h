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
/* ShaderTransparentGlass reconciles at 480. Its FIRST bitmap dependency is
 * the reflection cube map, which is not what the surface looks like -- the
 * needler's glowing needles came out textured with a dark grey cubemap. */
/* ShaderModel reconciles at 440. Every model detail map in the Trial uses
 * the same "double biased multiply" the environment shader does, so they
 * share one code path. The `detail mask` at 214 -- which channel of the
 * multipurpose map gates it -- is not read yet. */
#define HTA_SOSO_DETAIL_SCALE 216u
#define HTA_SOSO_DETAIL       220u
#define HTA_SOSO_MULTIPURPOSE 188u
#define HTA_SOSO_DETAIL_MASK  214u

#define HTA_SGLA_BACKGROUND_TINT 100u
#define HTA_SGLA_DIFFUSE         344u
/* ShaderEnvironment reconciles at 836. The detail maps are what give Halo's
 * ground its texture up close -- without them a grass shader is one flat
 * repeat of a low-resolution base map. */
#define HTA_SENV_PRIMARY_DETAIL_SCALE   180u
#define HTA_SENV_PRIMARY_DETAIL         184u
#define HTA_SENV_SECONDARY_DETAIL_SCALE 200u
#define HTA_SENV_SECONDARY_DETAIL       204u

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

/* An environment shader's primary detail map and how many times it repeats
 * across the base map's span. 0 when the shader has none (or is not an
 * `senv`), which is the case for every object shader. */
uint32_t hta_shader_detail_bitmap(const hta_cache *c, uint32_t shader_tag_id,
                                  float *out_scale);

/* The second detail map. Halo blends the two by the BASE map's alpha, which
 * is how Blood Gulch's ground is sand at 100x in some places and grass at
 * 60x in others out of one shader. 0 when there is no second one. */
uint32_t hta_shader_detail2_bitmap(const hta_cache *c, uint32_t shader_tag_id,
                                   float *out_scale);

/* A model shader's multipurpose map and which of its channels gates the
 * detail map (`ShaderModelDetailMask`: 0 none, then pairs of
 * inverse/straight for reflection, self-illumination, change colour and
 * auxiliary). 0 when the shader is not a model or has no mask. */
uint32_t hta_shader_multipurpose(const hta_cache *c, uint32_t shader_tag_id,
                                 uint8_t *out_mask);

/* Decode every unique albedo + lightmap referenced by the mesh. Fills
 * mesh->textures and per-submesh albedo_tex/lightmap_tex. Missing resource
 * map is not fatal: those slots stay ~0u (renderer uses a default). */
bool hta_bsp_load_textures(const hta_cache *c, const hta_resource_map *bitmaps,
                           hta_bsp_mesh *mesh, char *err, size_t errlen);

/* Intern a decoded bitmap into mesh->textures. Returns slot or ~0u. */
uint32_t hta_mesh_intern_bitmap(hta_bsp_mesh *mesh, const hta_cache *c,
                                const hta_resource_map *bitmaps,
                                uint32_t tag_id, uint32_t index);

/* The same, multiplied by a colour. `tint` is 0xRRGGBB; 0xFFFFFF is the
 * plain call above and shares its slot. A tinted copy is its own slot, so
 * the needler's magenta shards and something else's white use of the same
 * sheet do not fight over one decode. Alpha is untouched -- it is the
 * sprite's shape, and on the additive path its brightness. */
uint32_t hta_mesh_intern_bitmap_tinted(hta_bsp_mesh *mesh, const hta_cache *c,
                                       const hta_resource_map *bitmaps,
                                       uint32_t tag_id, uint32_t index,
                                       uint32_t tint);

/* Pack a 0..1 float colour into the 0xRRGGBB a tinted intern takes. Values
 * are clamped and quantised to 8 bits, which is what makes two tints that
 * differ in the third decimal share one texture instead of two. */
uint32_t hta_tint_pack(const float rgb[3]);

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

/* How many sprites a sequence holds, and sprite `index` of it. A sequence
 * is not always one sprite: the sniper's scope labels live as "2x" and "8x"
 * side by side in a single sequence, and which one is drawn is the zoom
 * level. */
uint32_t hta_bitmap_sprite_count(const hta_cache *c, uint32_t tag_id, uint32_t seq);
bool hta_bitmap_sprite_in(const hta_cache *c, uint32_t tag_id, uint32_t seq,
                          uint32_t index, hta_bitmap_sprite *out);

/* Decode a raw pixel blob (used by unit tests; no cache involved). */
bool hta_bitmap_decode_pixels(uint16_t format, uint32_t w, uint32_t h,
                              const uint8_t *src, uint32_t src_len,
                              hta_bitmap *out, char *err, size_t errlen);

#endif
