#include "effect.h"
#include "bsp.h"     /* hta_read_reflexive */

#include <stdio.h>

#include <string.h>

/* Effect (64) */
#define EFF_LOCATIONS       40u
#define EFF_EVENTS          52u
/* EffectLocation (32): a marker name, and nothing else. */
#define EFFLOC_SIZE         32u
/* EffectEvent (68) */
#define EFFEVENT_SIZE       68u
#define EFFEVENT_PARTS      44u
#define EFFEVENT_PARTICLES  56u
/* EffectPart (104) */
#define EFFPART_SIZE       104u
#define EFFPART_TYPE_CLASS  20u
#define EFFPART_TYPE        24u   /* TagDependency; tag id at +12 */
/* EffectParticle (232) */
#define EFFPARTICLE_SIZE   232u
#define EFFP_CREATE_IN       0u
#define EFFP_CREATE          4u
#define EFFP_LOCATION        8u
#define EFFP_TYPE           84u   /* TagDependency; tag id at +12 */
#define EFFP_RADIUS        160u   /* float bounds */
/* Particle (356) */
#define PART_BITMAP          4u   /* TagDependency; tag id at +12 */
#define PART_LIFESPAN       56u   /* float bounds */
#define PART_FADE_IN        64u
#define PART_FADE_OUT       68u
#define PART_ORIENTATION   172u
#define PART_BLEND         218u

static bool events_of(const hta_cache *c, uint32_t effect_tag_id,
                      uint32_t *out_off, uint32_t *out_count, uint32_t *out_base)
{
    if (!c || !effect_tag_id || effect_tag_id == 0xFFFFFFFFu) return false;
    int32_t ti = hta_cache_find_tag_by_id(c, effect_tag_id);
    if (ti < 0) return false;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return false;
    uint32_t base;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return false;
    uint32_t n = 0, ptr = 0, off = 0;
    if (!hta_read_reflexive(c, base + EFF_EVENTS, &n, &ptr)) return false;
    if (!n || !hta_cache_ptr_to_offset(c, ptr, &off)) return false;
    *out_off = off;
    *out_count = n;
    if (out_base) *out_base = base;
    return true;
}

uint32_t hta_effect_first_sound(const hta_cache *c, uint32_t effect_tag_id)
{
    uint32_t ev_off = 0, ev_count = 0;
    if (!events_of(c, effect_tag_id, &ev_off, &ev_count, NULL)) return 0;

    for (uint32_t e = 0; e < ev_count; e++) {
        uint32_t pc = 0, pp = 0, po = 0;
        if (!hta_read_reflexive(c, ev_off + e * EFFEVENT_SIZE + EFFEVENT_PARTS, &pc, &pp))
            continue;
        if (!pc || !hta_cache_ptr_to_offset(c, pp, &po)) continue;
        for (uint32_t k = 0; k < pc; k++) {
            uint32_t pk = po + k * EFFPART_SIZE;
            uint32_t cls = 0, id = 0;
            if (!hta_rd_u32(c, pk + EFFPART_TYPE_CLASS, &cls)) continue;
            if (cls != HTA_FOURCC('s','n','d','!')) continue;
            if (!hta_rd_u32(c, pk + EFFPART_TYPE + 12u, &id)) continue;
            if (id && id != 0xFFFFFFFFu) return id;
        }
    }
    return 0;
}

/* Marker name of location `index`, or NULL-terminated empty on failure. */
static bool location_marker(const hta_cache *c, uint32_t effect_base,
                            uint32_t index, char out[32])
{
    out[0] = '\0';
    uint32_t n = 0, ptr = 0, off = 0;
    if (!hta_read_reflexive(c, effect_base + EFF_LOCATIONS, &n, &ptr)) return false;
    if (index >= n || !hta_cache_ptr_to_offset(c, ptr, &off)) return false;
    if (!hta_rd_bytes(c, off + index * EFFLOC_SIZE, out, 31)) return false;
    out[31] = '\0';
    return true;
}

bool hta_effect_fp_flash(const hta_cache *c, uint32_t effect_tag_id,
                         const char *marker_name, hta_effect_particle *out)
{
    if (!out || !marker_name) return false;
    memset(out, 0, sizeof(*out));

    uint32_t ev_off = 0, ev_count = 0, base = 0;
    if (!events_of(c, effect_tag_id, &ev_off, &ev_count, &base)) return false;

    for (uint32_t e = 0; e < ev_count; e++) {
        uint32_t qc = 0, qp = 0, qo = 0;
        if (!hta_read_reflexive(c, ev_off + e * EFFEVENT_SIZE + EFFEVENT_PARTICLES,
                                &qc, &qp))
            continue;
        if (!qc || !hta_cache_ptr_to_offset(c, qp, &qo)) continue;

        for (uint32_t k = 0; k < qc; k++) {
            uint32_t qk = qo + k * EFFPARTICLE_SIZE;
            uint16_t create_in = 0, create = 0, loc = 0;
            uint32_t part_id = 0;
            hta_rd_u16(c, qk + EFFP_CREATE_IN, &create_in);
            hta_rd_u16(c, qk + EFFP_CREATE, &create);
            hta_rd_u16(c, qk + EFFP_LOCATION, &loc);
            if (!hta_rd_u32(c, qk + EFFP_TYPE + 12u, &part_id)) continue;
            if (!part_id || part_id == 0xFFFFFFFFu) continue;

            /* Underwater variants are a different particle entirely. */
            if (create_in != HTA_FX_IN_ANY && create_in != HTA_FX_IN_AIR) continue;
            /* What the other player sees is not what we see. */
            if (create == HTA_FX_CAM_THIRD) continue;

            char marker[32];
            if (!location_marker(c, base, loc, marker)) continue;
            if (strcmp(marker, marker_name) != 0) continue;

            int32_t pti = hta_cache_find_tag_by_id(c, part_id);
            if (pti < 0) continue;
            hta_tag_entry pt;
            if (!hta_cache_tag(c, (uint32_t)pti, &pt) || pt.indexed) continue;
            uint32_t pb;
            if (!hta_cache_ptr_to_offset(c, pt.tag_data_ptr, &pb)) continue;

            uint16_t blend = 0;
            hta_rd_u16(c, pb + PART_BLEND, &blend);
            /* A flash adds light to the frame. Smoke at the same marker
             * alpha-blends, and this is what tells them apart. */
            if (blend != HTA_FX_BLEND_ADD) continue;

            uint32_t bitmap = 0;
            if (!hta_rd_u32(c, pb + PART_BITMAP + 12u, &bitmap)) continue;
            if (!bitmap || bitmap == 0xFFFFFFFFu) continue;

            uint16_t orient = 0;
            float ls0 = 0.0f, ls1 = 0.0f, r0 = 0.0f, r1 = 0.0f;
            hta_rd_u16(c, pb + PART_ORIENTATION, &orient);
            hta_rd_f32(c, pb + PART_LIFESPAN, &ls0);
            hta_rd_f32(c, pb + PART_LIFESPAN + 4u, &ls1);
            hta_rd_f32(c, pb + PART_FADE_IN, &out->fade_in);
            hta_rd_f32(c, pb + PART_FADE_OUT, &out->fade_out);
            hta_rd_f32(c, qk + EFFP_RADIUS, &r0);
            hta_rd_f32(c, qk + EFFP_RADIUS + 4u, &r1);

            out->part_id = part_id;
            out->bitmap_id = bitmap;
            out->blend = (uint8_t)blend;
            out->orientation = (uint8_t)orient;
            out->lifespan = ls1 > ls0 ? ls1 : ls0;
            out->radius_min = r0;
            out->radius_max = r1 > r0 ? r1 : r0;
            snprintf(out->marker, sizeof(out->marker), "%s", marker);
            return true;
        }
    }
    return false;
}
