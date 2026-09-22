#include "strings.h"
#include "bsp.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define USTR_ENTRY 20u

uint32_t hta_ustr_find(const hta_cache *c, const char *path)
{
    if (!c || !path) return 0;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.primary_class != HTA_FOURCC('u','s','t','r'))
            continue;
        char p[256];
        if (hta_cache_tag_path(c, &t, p, sizeof(p)) && !strcasecmp(p, path))
            return t.tag_id;
    }
    return 0;
}

static bool list(const hta_cache *c, uint32_t tag_id, uint32_t *count, uint32_t *first)
{
    int32_t ti = hta_cache_find_tag_by_id(c, tag_id);
    hta_tag_entry t;
    uint32_t base, ptr;
    if (ti < 0 || !hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed ||
        t.primary_class != HTA_FOURCC('u','s','t','r') ||
        !hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base) ||
        !hta_read_reflexive(c, base, count, &ptr))
        return false;
    if (!*count) { *first = 0; return true; }
    return hta_cache_ptr_to_offset(c, ptr, first);
}

uint32_t hta_ustr_count(const hta_cache *c, uint32_t tag_id)
{
    uint32_t n = 0, first = 0;
    if (!c || !list(c, tag_id, &n, &first)) return 0;
    return n;
}

bool hta_ustr_get(const hta_cache *c, uint32_t tag_id, uint32_t index,
                  char *out, size_t outlen)
{
    if (!out || !outlen) return false;
    out[0] = 0;
    uint32_t n = 0, first = 0;
    if (!c || !list(c, tag_id, &n, &first) || index >= n) return false;
    uint32_t e = first + index * USTR_ENTRY, size = 0, ptr = 0, at = 0;
    if (!hta_rd_u32(c, e + 0u, &size) || !hta_rd_u32(c, e + 12u, &ptr)) return false;
    if (!size) return true;
    if (!hta_cache_ptr_to_offset(c, ptr, &at) || (uint64_t)at + size > c->size) return false;
    size_t w = 0;
    for (uint32_t k = 0; k + 1u < size && w + 1u < outlen; k += 2u) {
        uint16_t ch = (uint16_t)(c->data[at + k] | (c->data[at + k + 1u] << 8));
        if (!ch) break;
        if (ch == '\r') continue;
        out[w++] = ch < 128u ? (char)ch : '?';
    }
    out[w] = 0;
    return true;
}
