#include "cache.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

/* ---- demo (Trial) header field offsets; see INVADER_ASSET_PIPELINE.md §2.1 ---- */
#define DEMO_MAP_TYPE      0x002u
#define DEMO_HEAD_LITERAL  0x2C0u
#define DEMO_TAG_DATA_SIZE 0x2C4u
#define DEMO_BUILD         0x2C8u
#define DEMO_ENGINE        0x588u
#define DEMO_NAME          0x58Cu
#define DEMO_CRC32         0x5B0u
#define DEMO_FILE_SIZE     0x5E8u
#define DEMO_TAG_DATA_OFF  0x5ECu
#define DEMO_FOOT_LITERAL  0x5F0u

/* ---- retail header field offsets ---- */
#define RET_HEAD_LITERAL   0x000u
#define RET_ENGINE         0x004u
#define RET_FILE_SIZE      0x008u
#define RET_TAG_DATA_OFF   0x010u
#define RET_TAG_DATA_SIZE  0x014u
#define RET_NAME           0x020u
#define RET_BUILD          0x040u
#define RET_MAP_TYPE       0x060u
#define RET_CRC32          0x064u
#define RET_FOOT_LITERAL   0x7FCu

/* ---- tag data header ---- */
#define TDH_TAG_ARRAY_ADDR 0x00u
#define TDH_SCENARIO_TAG   0x04u
#define TDH_TAG_COUNT      0x0Cu
#define TDH_MODEL_PART_CNT 0x10u
#define TDH_MODEL_DATA_OFF 0x14u
#define TDH_VERTEX_SIZE    0x1Cu
#define TDH_MODEL_DATA_SZ  0x20u
#define TDH_TAGS_LITERAL   0x24u
#define TDH_SIZE           0x28u

static void fail(char *err, size_t n, const char *fmt, ...)
{
    if (!err || !n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

/* ------------------------- bounds-checked readers ------------------------- */

bool hta_rd_bytes(const hta_cache *c, uint32_t off, void *dst, uint32_t len)
{
    if (!c || !c->data) return false;
    /* off + len must not overflow and must stay inside the file */
    if ((uint64_t)off + (uint64_t)len > (uint64_t)c->size) return false;
    memcpy(dst, c->data + off, len);
    return true;
}

bool hta_rd_u8(const hta_cache *c, uint32_t off, uint8_t *out)
{
    return hta_rd_bytes(c, off, out, 1);
}

bool hta_rd_u16(const hta_cache *c, uint32_t off, uint16_t *out)
{
    uint8_t b[2];
    if (!hta_rd_bytes(c, off, b, 2)) return false;
    *out = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return true;
}

bool hta_rd_u32(const hta_cache *c, uint32_t off, uint32_t *out)
{
    uint8_t b[4];
    if (!hta_rd_bytes(c, off, b, 4)) return false;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

bool hta_rd_f32(const hta_cache *c, uint32_t off, float *out)
{
    uint32_t u;
    if (!hta_rd_u32(c, off, &u)) return false;
    /* IEEE-754 bit reinterpretation without aliasing UB */
    memcpy(out, &u, 4);
    return true;
}

static void read_tagstring(const hta_cache *c, uint32_t off, char dst[33])
{
    memset(dst, 0, 33);
    if (!hta_rd_bytes(c, off, dst, 32)) return;
    dst[32] = 0;
    /* scrub anything non-printable so a corrupt map can't emit control codes */
    for (int i = 0; i < 32; i++) {
        if (dst[i] == 0) break;
        if ((unsigned char)dst[i] < 0x20 || (unsigned char)dst[i] > 0x7E) { dst[i] = 0; break; }
    }
}

/* ---------------------------- pointer maths ----------------------------- */

bool hta_translate(uint32_t ptr, uint32_t base, uint32_t region_off,
                   uint32_t region_size, uint32_t *out_off)
{
    if (ptr < base) return false;
    uint64_t rel = (uint64_t)ptr - (uint64_t)base;
    if (rel >= (uint64_t)region_size) return false;
    uint64_t off = rel + (uint64_t)region_off;
    if (off > 0xFFFFFFFFull) return false;
    *out_off = (uint32_t)off;
    return true;
}

bool hta_cache_ptr_to_offset(const hta_cache *c, uint32_t ptr, uint32_t *out_off)
{
    if (!c) return false;
    return hta_translate(ptr, c->base_address, c->tag_data_offset,
                         c->tag_data_size, out_off);
}

/* ------------------------------ open/validate --------------------------- */

static bool detect_layout(const hta_cache *c, bool *is_demo)
{
    uint32_t h;
    if (hta_rd_u32(c, DEMO_HEAD_LITERAL, &h) && h == HTA_LIT_HEAD_DEMO) { *is_demo = true;  return true; }
    if (hta_rd_u32(c, RET_HEAD_LITERAL,  &h) && h == HTA_LIT_HEAD)      { *is_demo = false; return true; }
    return false;
}

bool hta_cache_open(hta_cache *c, const uint8_t *data, size_t size,
                    char *err, size_t errlen)
{
    if (!c) return false;
    memset(c, 0, sizeof(*c));
    if (!data) { fail(err, errlen, "no data"); return false; }
    c->data = data;
    c->size = size;

    if (size < HTA_CACHE_HEADER_SIZE) {
        fail(err, errlen, "file too small: %zu bytes, need at least %u", size, HTA_CACHE_HEADER_SIZE);
        return false;
    }

    if (!detect_layout(c, &c->is_demo_layout)) {
        fail(err, errlen, "not a Halo cache file: no 'head' at 0x0 and no 'Ehed' at 0x2C0");
        return false;
    }

    uint32_t foot = 0;
    if (c->is_demo_layout) {
        uint16_t mt = 0;
        hta_rd_u16(c, DEMO_MAP_TYPE, &mt);
        c->map_type = mt;
        hta_rd_u32(c, DEMO_TAG_DATA_SIZE, &c->tag_data_size);
        read_tagstring(c, DEMO_BUILD, c->build);
        hta_rd_u32(c, DEMO_ENGINE, &c->engine);
        read_tagstring(c, DEMO_NAME, c->name);
        hta_rd_u32(c, DEMO_CRC32, &c->crc32);
        hta_rd_u32(c, DEMO_FILE_SIZE, &c->decompressed_file_size);
        hta_rd_u32(c, DEMO_TAG_DATA_OFF, &c->tag_data_offset);
        hta_rd_u32(c, DEMO_FOOT_LITERAL, &foot);
        if (foot != HTA_LIT_FOOT_DEMO) {
            fail(err, errlen, "demo header: bad foot literal 0x%08X (expected 'Gfot')", foot);
            return false;
        }
    } else {
        uint16_t mt = 0;
        hta_rd_u16(c, RET_MAP_TYPE, &mt);
        c->map_type = mt;
        hta_rd_u32(c, RET_TAG_DATA_SIZE, &c->tag_data_size);
        read_tagstring(c, RET_BUILD, c->build);
        hta_rd_u32(c, RET_ENGINE, &c->engine);
        read_tagstring(c, RET_NAME, c->name);
        hta_rd_u32(c, RET_CRC32, &c->crc32);
        hta_rd_u32(c, RET_FILE_SIZE, &c->decompressed_file_size);
        hta_rd_u32(c, RET_TAG_DATA_OFF, &c->tag_data_offset);
        hta_rd_u32(c, RET_FOOT_LITERAL, &foot);
        if (foot != HTA_LIT_FOOT) {
            fail(err, errlen, "retail header: bad foot literal 0x%08X (expected 'foot')", foot);
            return false;
        }
    }

    /* base address depends on the engine, and the Trial is the odd one out */
    switch (c->engine) {
    case HTA_ENGINE_DEMO:           c->base_address = HTA_BASE_GEARBOX_DEMO;   break;
    case HTA_ENGINE_RETAIL:
    case HTA_ENGINE_CUSTOM_EDITION: c->base_address = HTA_BASE_GEARBOX_RETAIL; break;
    default:
        fail(err, errlen, "unsupported engine %u (%s)", c->engine, hta_engine_name(c->engine));
        return false;
    }

    /* tag data region must be inside the file */
    if ((uint64_t)c->tag_data_offset + (uint64_t)c->tag_data_size > (uint64_t)size) {
        fail(err, errlen, "tag data region [0x%X +0x%X] exceeds file size %zu",
             c->tag_data_offset, c->tag_data_size, size);
        return false;
    }
    if (c->tag_data_size < TDH_SIZE) {
        fail(err, errlen, "tag data size 0x%X too small for header", c->tag_data_size);
        return false;
    }

    /* tag data header sits at the start of the tag data region */
    uint32_t h = c->tag_data_offset;
    hta_rd_u32(c, h + TDH_TAG_ARRAY_ADDR, &c->tag_array_ptr);
    hta_rd_u32(c, h + TDH_SCENARIO_TAG,   &c->scenario_tag_id);
    hta_rd_u32(c, h + TDH_TAG_COUNT,      &c->tag_count);
    hta_rd_u32(c, h + TDH_MODEL_DATA_OFF, &c->model_data_file_offset);
    hta_rd_u32(c, h + TDH_VERTEX_SIZE,    &c->vertex_size);
    hta_rd_u32(c, h + TDH_MODEL_DATA_SZ,  &c->model_data_size);
    hta_rd_u32(c, h + TDH_TAGS_LITERAL,   &c->tags_literal);

    if (c->tags_literal != HTA_LIT_TAGS) {
        fail(err, errlen, "bad tags literal 0x%08X at 0x%X (expected 'tags')",
             c->tags_literal, h + TDH_TAGS_LITERAL);
        return false;
    }

    /* A sane upper bound: the original engine's own limit region is 23 MiB of
     * tag space and entries are 0x20 bytes. Reject absurd counts before we
     * ever multiply. */
    if (c->tag_count == 0 || c->tag_count > (HTA_TAG_SPACE_GEARBOX / HTA_TAG_ENTRY_SIZE)) {
        fail(err, errlen, "implausible tag count %u", c->tag_count);
        return false;
    }

    if (!hta_cache_ptr_to_offset(c, c->tag_array_ptr, &c->tag_array_offset)) {
        fail(err, errlen, "tag array pointer 0x%08X outside tag space (base 0x%08X)",
             c->tag_array_ptr, c->base_address);
        return false;
    }

    uint64_t array_bytes = (uint64_t)c->tag_count * HTA_TAG_ENTRY_SIZE;
    if ((uint64_t)c->tag_array_offset + array_bytes > (uint64_t)size) {
        fail(err, errlen, "tag array [0x%X +%llu] exceeds file size %zu",
             c->tag_array_offset, (unsigned long long)array_bytes, size);
        return false;
    }
    return true;
}

/* ------------------------------ tag access ------------------------------ */

bool hta_cache_tag(const hta_cache *c, uint32_t index, hta_tag_entry *out)
{
    if (!c || !out || index >= c->tag_count) return false;
    uint32_t o = c->tag_array_offset + index * HTA_TAG_ENTRY_SIZE;
    return hta_rd_u32(c, o + 0x00, &out->primary_class) &&
           hta_rd_u32(c, o + 0x04, &out->secondary_class) &&
           hta_rd_u32(c, o + 0x08, &out->tertiary_class) &&
           hta_rd_u32(c, o + 0x0C, &out->tag_id) &&
           hta_rd_u32(c, o + 0x10, &out->tag_path_ptr) &&
           hta_rd_u32(c, o + 0x14, &out->tag_data_ptr) &&
           hta_rd_u32(c, o + 0x18, &out->indexed);
}

bool hta_cache_tag_path(const hta_cache *c, const hta_tag_entry *t,
                        char *dst, size_t dstlen)
{
    if (!c || !t || !dst || dstlen == 0) return false;
    dst[0] = 0;
    uint32_t off;
    if (!hta_cache_ptr_to_offset(c, t->tag_path_ptr, &off)) return false;

    size_t i = 0;
    for (; i + 1 < dstlen; i++) {
        uint8_t ch;
        if (!hta_rd_u8(c, off + (uint32_t)i, &ch)) { dst[i] = 0; return false; }
        if (ch == 0) break;
        /* keep it printable; paths are ASCII with backslashes */
        dst[i] = (ch >= 0x20 && ch <= 0x7E) ? (char)ch : '?';
    }
    dst[i] = 0;
    return true;
}

int32_t hta_cache_find_tag_by_class(const hta_cache *c, uint32_t fourcc)
{
    if (!c) return -1;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (hta_cache_tag(c, i, &t) && t.primary_class == fourcc) return (int32_t)i;
    }
    return -1;
}

int32_t hta_cache_find_tag_by_id(const hta_cache *c, uint32_t tag_id)
{
    if (!c) return -1;
    /* The low 16 bits of a TagID are the array index in practice, but that is
     * an optimisation, not a guarantee. Check it, then fall back to a scan. */
    uint32_t guess = tag_id & 0xFFFFu;
    hta_tag_entry t;
    if (guess < c->tag_count && hta_cache_tag(c, guess, &t) && t.tag_id == tag_id)
        return (int32_t)guess;
    for (uint32_t i = 0; i < c->tag_count; i++)
        if (hta_cache_tag(c, i, &t) && t.tag_id == tag_id) return (int32_t)i;
    return -1;
}

const char *hta_engine_name(uint32_t engine)
{
    switch (engine) {
    case HTA_ENGINE_XBOX:           return "Xbox";
    case HTA_ENGINE_DEMO:           return "Halo PC Trial (Gearbox demo)";
    case HTA_ENGINE_RETAIL:         return "Halo PC retail (Gearbox)";
    case HTA_ENGINE_CUSTOM_EDITION: return "Halo Custom Edition";
    default:                        return "unknown";
    }
}

void hta_fourcc_str(uint32_t fourcc, char out[5])
{
    out[0] = (char)((fourcc >> 24) & 0xFF);
    out[1] = (char)((fourcc >> 16) & 0xFF);
    out[2] = (char)((fourcc >> 8) & 0xFF);
    out[3] = (char)(fourcc & 0xFF);
    out[4] = 0;
    for (int i = 0; i < 4; i++)
        if ((unsigned char)out[i] < 0x20 || (unsigned char)out[i] > 0x7E) out[i] = '?';
}
