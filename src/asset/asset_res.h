/* Package-backed asset resources (X5, docs/RESOURCES.md "Asset resources").
 *
 * Four resource types become real content a LIBRARY package provides and
 * other packages import through X4's typed references:
 *
 *   texture   x5shared:texture/crate    an RGBA8 image
 *   material  x5shared:material/crate   a surface: one texture, a draw mode
 *   model     x5shared:model/crate      a static mesh whose groups draw with
 *                                       its material slots
 *   sound     x5shared:sound/impact     one clip of signed 16-bit PCM
 *
 * A library declares them in its manifest's "assets" member (canonical
 * JSON, keys sorted, every list in canonical ID / path order):
 *
 *   "assets": {"schema": 1,
 *     "members":   [{"path": "models/crate.mesh", "size": 1234}, ...],
 *     "textures":  [{"format": "rgba8", "height": 64, "id": ..., "member": ..., "width": 64}],
 *     "materials": [{"draw": "opaque", "id": ..., "texture": "x5shared:texture/crate"}],
 *     "models":    [{"format": "mesh1", "id": ..., "materials": [...], "member": ...}],
 *     "sounds":    [{"channels": 1, "format": "pcm_s16le", "frames": N, "id": ..., "member": ..., "rate": 22050}]}
 *
 * The payload bytes follow the manifest in the OALASSET, member after
 * member in `members` order. A MEMBER PATH is package-local storage, never
 * identity: lowercase [a-z0-9_] segments joined by '/', one extension on
 * the last, at most 96 bytes and 6 segments -- no '..', no leading '/',
 * no '\\', no NUL -- and it is never joined to a host path: the payload is
 * found by offset inside the package bytes. Every member backs exactly one
 * resource; every resource names a member that exists.
 *
 * Payload formats reuse what the engine already reads: rgba8 is an OALMAP
 * texture record's pixels, pcm_s16le an OALASSET sound record's samples,
 * and mesh1 is an OALMAP's vertex (40 bytes), index (u32) records behind
 * a 16-byte header ("MSH1", u32 vertex, index and group counts) with 12-byte
 * groups (u32 first index, count, material slot).
 *
 * References: a material's texture and a model's material slots resolve
 * through hta_res_resolve -- the library's own resources or ones it
 * imports -- so a slot never takes a texture, and a texture can live in
 * another library. A world's prop names a model; a mover definition may
 * name a sound (world_def.h).
 *
 * Runtime: every library's assets are decoded once, when the package set
 * loads, into one hta_asset_table the world owns (external_map.h):
 * textures first by package ID then resource ID, and so on per type, so an
 * index is the same on every peer. Models share their materials' textures
 * (a model's mesh borrows the texture pixels; nothing is copied). A
 * resource used by many placements, or imported by two consumers, exists
 * once in the table. Gameplay and rendering hold indices only.
 *
 * Portable C11; bounded; every payload offset and count checked. */
#ifndef HTA_ASSET_RES_H
#define HTA_ASSET_RES_H

#include "bsp.h"
#include "resource.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_ASSET_SCHEMA          2u
#define HTA_ASSET_MAX_PER_TYPE    64u                 /* one library, one type */
#define HTA_ASSET_MAX_MEMBERS     256u
#define HTA_ASSET_MEMBER_MAX      96u                 /* bytes in a member path */
#define HTA_ASSET_MEMBER_SEGS     6u
#define HTA_ASSET_MAX_PAYLOAD     (96u * 1024u * 1024u)   /* one library's members together */
#define HTA_ASSET_TEX_MAX         2048u               /* texels on a side */
#define HTA_ASSET_MESH_MAX_VERTS  262144u
#define HTA_ASSET_MESH_MAX_INDICES 786432u
#define HTA_ASSET_MESH_MAX_GROUPS 64u
#define HTA_ASSET_MAX_SLOTS       16u                 /* materials one model draws with */
#define HTA_ASSET_SOUND_MAX_FRAMES (48000u * 60u)
#define HTA_ASSET_MESH_MAGIC      "MSH1"
#define HTA_ASSET_NONE            0xFFFFu

enum { HTA_ASSET_DRAW_OPAQUE = 0, HTA_ASSET_DRAW_ALPHA = 1 };

typedef struct {
    char     id[HTA_RID_MAX + 1];
    uint8_t  provider;        /* the set's dependency index + 1 */
    uint32_t width, height;
    uint8_t *rgba;            /* owned */
} hta_asset_texture;

typedef struct {
    char     id[HTA_RID_MAX + 1];
    uint8_t  provider;
    uint8_t  draw;            /* HTA_ASSET_DRAW_* */
    uint16_t texture;         /* the table's texture index, once linked */
    char     texture_ref[HTA_RID_MAX + 1];
    float    emissive;        /* schema 2: base texture RGB emits independently of lights, 0..4 */
    float    roughness;       /* schema 2: 0 sharp metal .. 1 matte; old default 0.75 */
    bool     extended;
} hta_asset_material;

typedef struct hta_asset_model_s {
    char     id[HTA_RID_MAX + 1];
    uint8_t  provider;
    uint32_t slot_count;
    uint16_t slot[HTA_ASSET_MAX_SLOTS];               /* material indices, once linked */
    char     slot_ref[HTA_ASSET_MAX_SLOTS][HTA_RID_MAX + 1];
    /* Model space, runtime units. vertices/indices/submeshes owned; once
     * linked, `textures` holds one BORROWED entry per slot (the texture
     * table owns the pixels) and each submesh's albedo_tex is its slot. */
    hta_bsp_mesh mesh;
} hta_asset_model;

typedef struct {
    char     id[HTA_RID_MAX + 1];
    uint8_t  provider;
    uint32_t rate, channels, frames;
    int16_t *samples;         /* owned; frames x channels, interleaved */
    uint64_t digest;          /* FNV-1a 64 of the samples: shared clips (game/world_sounds.h) */
} hta_asset_sound;

/* One library's assets (hta_pkg_dep.assets), or a world's whole set
 * (hta_external_map.assets). */
typedef struct hta_asset_table {
    hta_asset_texture  *texture;  uint32_t texture_count;
    hta_asset_material *material; uint32_t material_count;
    hta_asset_model    *model;    uint32_t model_count;
    hta_asset_sound    *sound;    uint32_t sound_count;
    uint32_t payload_bytes;
} hta_asset_table;

/* The "assets" member of a library manifest and its payload (the bytes
 * after the manifest): parsed strictly, every member path and descriptor
 * checked, every payload decoded and validated. No member: true with an
 * empty table, and the payload must be empty. `pkg` names the package in
 * messages. References stay unresolved (hta_asset_link). */
bool hta_asset_parse(const uint8_t *manifest, size_t len, const uint8_t *payload, size_t plen, const char *pkg,
                     hta_asset_table *out, bool *declared, char *err, size_t errlen);
void hta_asset_table_free(hta_asset_table *t);

/* Local index of `id` of resource type `type` (texture, material, model,
 * sound), or -1. */
int32_t hta_asset_find(const hta_asset_table *t, uint8_t type, const char *id);
uint32_t hta_asset_count(const hta_asset_table *t, uint8_t type);
/* Is `type` one this module owns? */
bool hta_asset_type(uint8_t type);

/* A member path, as a package may spell it: true, or false with why. */
bool hta_asset_member_valid(const char *path, char *why, size_t n);

/* Builds a linked model's mesh textures from `textures` (its slots
 * resolved to materials, those to textures): called once the set is
 * linked. */
bool hta_asset_model_bind(hta_asset_model *m, const hta_asset_material *materials, uint32_t material_count,
                          const hta_asset_texture *textures, uint32_t texture_count, char *err, size_t errlen);

#endif
