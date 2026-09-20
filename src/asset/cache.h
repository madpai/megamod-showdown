/* Halo cache (.map) reader.
 *
 * Portable C11. No allocation, no exceptions, no STL: the caller supplies the
 * file bytes and this reads through them with bounds checks on every access.
 * Written from the format description documented in
 * docs/INVADER_ASSET_PIPELINE.md.
 *
 * Supports the Gearbox Halo PC Trial (CACHE_FILE_DEMO) and retail/Custom
 * Edition layouts, because the Trial header is a *permutation* of retail's.
 */
#ifndef HTA_CACHE_H
#define HTA_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* engine (cache version) values */
enum {
    HTA_ENGINE_XBOX           = 5,
    HTA_ENGINE_DEMO           = 6,   /* Halo PC Trial */
    HTA_ENGINE_RETAIL         = 7,
    HTA_ENGINE_CUSTOM_EDITION = 609,
};

/* header magic */
enum {
    HTA_LIT_HEAD      = 0x68656164u, /* "head" */
    HTA_LIT_FOOT      = 0x666F6F74u, /* "foot" */
    HTA_LIT_TAGS      = 0x74616773u, /* "tags" */
    HTA_LIT_HEAD_DEMO = 0x45686564u, /* "Ehed" */
    HTA_LIT_FOOT_DEMO = 0x47666F74u, /* "Gfot" */
};

/* base addresses that tag-space pointers assume */
#define HTA_BASE_GEARBOX_RETAIL 0x40440000u
#define HTA_BASE_GEARBOX_DEMO   0x4BF10000u   /* the Trial is different! */
#define HTA_TAG_SPACE_GEARBOX   (23u * 1024u * 1024u)

#define HTA_CACHE_HEADER_SIZE   0x800u
#define HTA_TAG_ENTRY_SIZE      0x20u
#define HTA_ERRLEN              192

/* FourCCs we care about for the Blood Gulch slice */
#define HTA_FOURCC(a,b,c,d) ((uint32_t)(a)<<24 | (uint32_t)(b)<<16 | (uint32_t)(c)<<8 | (uint32_t)(d))
#define HTA_TAG_SCNR HTA_FOURCC('s','c','n','r')
#define HTA_TAG_SBSP HTA_FOURCC('s','b','s','p')
#define HTA_TAG_BITM HTA_FOURCC('b','i','t','m')
#define HTA_TAG_SENV HTA_FOURCC('s','e','n','v')
#define HTA_TAG_SCHI HTA_FOURCC('s','c','h','i')
#define HTA_TAG_SCEX HTA_FOURCC('s','c','e','x')
#define HTA_TAG_MOD2 HTA_FOURCC('m','o','d','2')
#define HTA_TAG_SCEN HTA_FOURCC('s','c','e','n')
#define HTA_TAG_VEHI HTA_FOURCC('v','e','h','i')
#define HTA_TAG_COLL HTA_FOURCC('c','o','l','l')
#define HTA_TAG_WEAP HTA_FOURCC('w','e','a','p')
#define HTA_TAG_SKY  HTA_FOURCC('s','k','y',' ')
#define HTA_TAG_BIPD HTA_FOURCC('b','i','p','d')
#define HTA_TAG_MATG HTA_FOURCC('m','a','t','g')
#define HTA_TAG_LSND HTA_FOURCC('l','s','n','d')

typedef struct {
    uint32_t primary_class, secondary_class, tertiary_class;
    uint32_t tag_id;
    uint32_t tag_path_ptr;
    uint32_t tag_data_ptr;
    uint32_t indexed;         /* nonzero: data lives in an external resource map */
} hta_tag_entry;

typedef struct {
    const uint8_t *data;
    size_t         size;

    bool     is_demo_layout;   /* which header permutation matched */
    uint32_t engine;
    uint32_t tag_data_offset;
    uint32_t tag_data_size;
    uint32_t decompressed_file_size;
    uint32_t crc32;
    uint16_t map_type;
    char     name[33];
    char     build[33];

    /* tag data header */
    uint32_t base_address;
    uint32_t tag_array_ptr;
    uint32_t tag_array_offset;  /* resolved file offset */
    uint32_t scenario_tag_id;
    uint32_t tag_count;
    uint32_t model_data_file_offset;
    uint32_t model_data_size;
    uint32_t vertex_size;
    uint32_t tags_literal;
} hta_cache;

/* Open/validate. `data` must remain alive for the lifetime of `c`.
 * On failure returns false and writes a reason into err. */
bool hta_cache_open(hta_cache *c, const uint8_t *data, size_t size,
                    char *err, size_t errlen);

/* Bounds-checked little-endian reads at absolute file offsets. */
bool hta_rd_u8 (const hta_cache *c, uint32_t off, uint8_t  *out);
bool hta_rd_u16(const hta_cache *c, uint32_t off, uint16_t *out);
bool hta_rd_u32(const hta_cache *c, uint32_t off, uint32_t *out);
bool hta_rd_f32(const hta_cache *c, uint32_t off, float    *out);
bool hta_rd_bytes(const hta_cache *c, uint32_t off, void *dst, uint32_t len);

/* tag-space pointer -> file offset, using the main tag data region. */
bool hta_cache_ptr_to_offset(const hta_cache *c, uint32_t ptr, uint32_t *out_off);

/* Generic translation for regions with their own base (e.g. a BSP). */
bool hta_translate(uint32_t ptr, uint32_t base, uint32_t region_off,
                   uint32_t region_size, uint32_t *out_off);

bool hta_cache_tag(const hta_cache *c, uint32_t index, hta_tag_entry *out);

/* Copies the tag path string. Returns false if unreadable/unterminated. */
bool hta_cache_tag_path(const hta_cache *c, const hta_tag_entry *t,
                        char *dst, size_t dstlen);

/* Finds the first tag of a class. Returns index or -1. */
int32_t hta_cache_find_tag_by_class(const hta_cache *c, uint32_t fourcc);
int32_t hta_cache_find_tag_by_id(const hta_cache *c, uint32_t tag_id);

const char *hta_engine_name(uint32_t engine);
void hta_fourcc_str(uint32_t fourcc, char out[5]);

#endif
