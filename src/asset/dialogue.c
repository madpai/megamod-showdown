#include "dialogue.h"
#include "bsp.h"

#include <string.h>

uint32_t hta_dialogue_tag(const hta_cache *c)
{
    if (!c) return 0;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t)) continue;
        if (t.primary_class != HTA_FOURCC('u','d','l','g')) continue;
        if (t.indexed) continue;
        return t.tag_id;
    }
    return 0;
}

uint32_t hta_dialogue_sound(const hta_cache *c, uint32_t udlg_tag_id,
                            uint32_t slot)
{
    if (!c || !udlg_tag_id || udlg_tag_id == 0xFFFFFFFFu) return 0;
    if (slot > 4096u) return 0;
    int32_t ti = hta_cache_find_tag_by_id(c, udlg_tag_id);
    if (ti < 0) return 0;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return 0;
    uint32_t base = 0;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return 0;
    uint32_t id = 0;
    if (!hta_rd_u32(c, base + slot + 12u, &id)) return 0;
    if (!id || id == 0xFFFFFFFFu) return 0;
    return id;
}
