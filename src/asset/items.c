#include "items.h"
#include "bsp.h"

#include <string.h>
#include <stdio.h>

/* Scenario (1456): netgame equipment at +900, 144 bytes each. */
#define SCN_NETGAME_EQUIP   900u
#define NETEQ_SIZE          144u
#define NETEQ_SPAWN_TIME     14u   /* int16 seconds; 0 = use the collection's */
#define NETEQ_POSITION       64u
#define NETEQ_FACING         76u
#define NETEQ_COLLECTION     80u   /* TagDependency; id at +12 */

/* ItemCollection (92): permutations at +0, default spawn time at +12.
 * ItemCollectionPermutation (84): weight at +32, item at +36. */
#define ITMC_PERMUTATIONS     0u
#define ITMC_DEFAULT_SPAWN   12u
#define ITMCPERM_SIZE        84u
#define ITMCPERM_WEIGHT      32u
#define ITMCPERM_ITEM        36u

/* Object (380) then Item (396) then Equipment's own, so these are absolute.
 * Equipment declares 944 = 776 + 168, and the powerup types that come out
 * are exactly right -- camouflage 3, overshield 2, health 5, grenade 6, and
 * 0 for the ammo powerups -- which is the check. */
#define OBJ_MODEL            40u
#define EQIP_POWERUP_TYPE   776u
#define EQIP_GRENADE_TYPE   778u
#define EQIP_POWERUP_TIME   780u
#define EQIP_PICKUP_SOUND   784u

/* Halo's own default where neither the placement nor the collection says.
 * The base weapons in Blood Gulch are written that way -- both zero -- and
 * in the real game they are back almost at once. */
#define HTA_ITEM_RESPAWN_DEFAULT 15.0f

hta_item_kind hta_item_kind_of(const hta_cache *c, uint32_t tag_id,
                               char *out_path, size_t pathlen)
{
    return hta_item_describe(c, tag_id, out_path, pathlen, NULL);
}

hta_item_kind hta_item_describe(const hta_cache *c, uint32_t tag_id,
                                char *out_path, size_t pathlen,
                                hta_item_choice *out)
{
    if (out_path && pathlen) out_path[0] = '\0';
    if (!c || !tag_id || tag_id == 0xFFFFFFFFu) return HTA_ITEM_NONE;
    int32_t ti = hta_cache_find_tag_by_id(c, tag_id);
    if (ti < 0) return HTA_ITEM_NONE;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return HTA_ITEM_NONE;

    char path[192];
    if (!hta_cache_tag_path(c, &t, path, sizeof(path))) path[0] = '\0';
    if (out_path && pathlen) snprintf(out_path, pathlen, "%s", path);

    uint32_t base = 0;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return HTA_ITEM_NONE;
    if (out) {
        uint32_t model = 0;
        hta_rd_u32(c, base + OBJ_MODEL + 12u, &model);
        out->model_id = (model == 0xFFFFFFFFu) ? 0u : model;
    }

    if (t.primary_class == HTA_FOURCC('w','e','a','p')) return HTA_ITEM_WEAPON;
    if (t.primary_class != HTA_FOURCC('e','q','i','p')) return HTA_ITEM_NONE;

    /* The equipment says what it is. Its PATH does not: the overshield's
     * model is the camouflage's and the camouflage's is the overshield's,
     * swapped in Bungie's own tags, so anything keying off names here would
     * hand out the wrong powerup. */
    uint16_t ptype = 0, gtype = 0;
    float ptime = 0.0f;
    uint32_t snd = 0;
    hta_rd_u16(c, base + EQIP_POWERUP_TYPE, &ptype);
    hta_rd_u16(c, base + EQIP_GRENADE_TYPE, &gtype);
    hta_rd_f32(c, base + EQIP_POWERUP_TIME, &ptime);
    hta_rd_u32(c, base + EQIP_PICKUP_SOUND + 12u, &snd);
    if (out) {
        out->powerup_time = ptime;
        out->grenade_type = gtype;
        out->pickup_snd = (snd == 0xFFFFFFFFu) ? 0u : snd;
    }

    switch (ptype) {
        case 1u: return HTA_ITEM_SPEED;
        case 2u: return HTA_ITEM_OVERSHIELD;
        case 3u: return HTA_ITEM_CAMOUFLAGE;
        case 4u: return HTA_ITEM_VISION;
        case 5u: return HTA_ITEM_HEALTH;
        case 6u: return HTA_ITEM_GRENADE;
        default: break;
    }
    return HTA_ITEM_NONE;    /* the ammo powerups; the Trial places none */
}

static void read_collection(const hta_cache *c, uint32_t itmc,
                            hta_item_spawn *out, float placement_respawn)
{
    out->collection_id = itmc;
    out->choice_count = 0;
    out->respawn = placement_respawn;

    int32_t ti = hta_cache_find_tag_by_id(c, itmc);
    if (ti < 0) return;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return;
    uint32_t base = 0;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return;

    if (out->respawn <= 0.0f) {
        int16_t def = 0;
        hta_rd_u16(c, base + ITMC_DEFAULT_SPAWN, (uint16_t *)&def);
        if (def > 0) out->respawn = (float)def;
    }
    if (out->respawn <= 0.0f) out->respawn = HTA_ITEM_RESPAWN_DEFAULT;

    uint32_t count = 0, ptr = 0, off = 0;
    if (!hta_read_reflexive(c, base + ITMC_PERMUTATIONS, &count, &ptr)) return;
    if (!count || !hta_cache_ptr_to_offset(c, ptr, &off)) return;

    for (uint32_t i = 0; i < count && out->choice_count < HTA_ITEM_MAX_CHOICES; i++) {
        uint32_t e = off + i * ITMCPERM_SIZE;
        uint32_t item = 0;
        float weight = 0.0f;
        hta_rd_f32(c, e + ITMCPERM_WEIGHT, &weight);
        if (!hta_rd_u32(c, e + ITMCPERM_ITEM + 12u, &item)) continue;
        if (!item || item == 0xFFFFFFFFu) continue;

        hta_item_choice *ch = &out->choice[out->choice_count];
        memset(ch, 0, sizeof(*ch));
        ch->tag_id = item;
        ch->weight = weight > 0.0f ? weight : 1.0f;
        ch->kind = hta_item_describe(c, item, ch->path, sizeof(ch->path), ch);
        if (ch->kind == HTA_ITEM_NONE) continue;   /* nothing we can hand over */
        out->choice_count++;
    }
}

uint32_t hta_scenario_items(const hta_cache *c, hta_item_spawn *out,
                            uint32_t max)
{
    if (!c || !out || !max) return 0;
    int32_t si = hta_cache_find_tag_by_id(c, c->scenario_tag_id);
    if (si < 0) return 0;
    hta_tag_entry st;
    if (!hta_cache_tag(c, (uint32_t)si, &st)) return 0;
    uint32_t base = 0;
    if (!hta_cache_ptr_to_offset(c, st.tag_data_ptr, &base)) return 0;

    uint32_t count = 0, ptr = 0, off = 0;
    if (!hta_read_reflexive(c, base + SCN_NETGAME_EQUIP, &count, &ptr)) return 0;
    if (!count || !hta_cache_ptr_to_offset(c, ptr, &off)) return 0;

    uint32_t n = 0;
    for (uint32_t i = 0; i < count && n < max; i++) {
        uint32_t e = off + i * NETEQ_SIZE;
        hta_item_spawn *s = &out[n];
        memset(s, 0, sizeof(*s));

        for (int k = 0; k < 3; k++)
            hta_rd_f32(c, e + NETEQ_POSITION + 4u * (uint32_t)k, &s->position[k]);
        hta_rd_f32(c, e + NETEQ_FACING, &s->facing);

        int16_t when = 0;
        hta_rd_u16(c, e + NETEQ_SPAWN_TIME, (uint16_t *)&when);

        uint32_t itmc = 0;
        if (!hta_rd_u32(c, e + NETEQ_COLLECTION + 12u, &itmc)) continue;
        if (!itmc || itmc == 0xFFFFFFFFu) continue;

        read_collection(c, itmc, s, when > 0 ? (float)when : 0.0f);
        if (!s->choice_count) continue;     /* nothing this build can give */
        n++;
    }
    return n;
}

uint32_t hta_item_pick(const hta_item_spawn *s, uint32_t *rng)
{
    if (!s || !s->choice_count) return 0;
    if (s->choice_count == 1) return 0;

    float total = 0.0f;
    for (uint32_t i = 0; i < s->choice_count; i++) total += s->choice[i].weight;
    if (!(total > 0.0f)) return 0;

    uint32_t seed = rng ? *rng : 1u;
    seed = seed * 1103515245u + 12345u;
    if (rng) *rng = seed;

    float r = (float)((seed >> 8) & 0xFFFFu) / 65535.0f * total;
    float run = 0.0f;
    for (uint32_t i = 0; i < s->choice_count; i++) {
        run += s->choice[i].weight;
        if (r <= run) return i;
    }
    return s->choice_count - 1u;
}
