#include "vitals.h"
#include "../asset/bsp.h"

#include <string.h>

/* Object, which the biped inherits. */
#define OBJ_COLLISION_MODEL  112u

/* ModelCollisionGeometry, 664, reconciles. */
#define COLL_MAX_BODY          8u
#define COLL_MAX_SHIELD      204u
#define COLL_RECHARGE_TIME   272u
#define COLL_SHIELD_RATE     448u

/* Globals, 428, reconciles. */
#define GLOBALS_FALLING      392u
#define FALL_DAMAGE           16u   /* TagDependency -> jpt! */
#define FALL_MAX_VELOCITY    140u
#define FALL_HARMFUL         144u   /* float bounds */

/* Halo keeps these velocities per TICK, at 30 a second, exactly as it does
 * the projectile speeds. The cyborg starts being hurt at 0.15, which is
 * 4.5 world units a second. */
#define TICKS_PER_SECOND 30.0f

void hta_vitals_reset(hta_vitals *v)
{
    if (!v) return;
    v->health = v->max_health;
    v->shield = v->max_shield;
    v->since_damage = v->recharge_delay;
    v->took_damage = false;
    v->shield_broke = false;
    v->died = false;
}

bool hta_vitals_load(hta_vitals *v, const hta_cache *c)
{
    if (!v || !c) return false;
    memset(v, 0, sizeof(*v));

    /* The multiplayer cyborg: Blood Gulch is a multiplayer map. Falling
     * back to the campaign one keeps this working on a map without it. */
    int32_t bi = -1;
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(c, i, &t) || t.indexed) continue;
        if (t.primary_class != HTA_TAG_BIPD) continue;
        char path[128];
        hta_cache_tag_path(c, &t, path, sizeof(path));
        if (strstr(path, "cyborg_mp")) { bi = (int32_t)i; break; }
        if (bi < 0 && strstr(path, "cyborg")) bi = (int32_t)i;
    }
    if (bi < 0) return false;

    hta_tag_entry bt;
    uint32_t bb;
    if (!hta_cache_tag(c, (uint32_t)bi, &bt)) return false;
    if (!hta_cache_ptr_to_offset(c, bt.tag_data_ptr, &bb)) return false;

    uint32_t coll = 0;
    hta_rd_u32(c, bb + OBJ_COLLISION_MODEL + 12u, &coll);
    if (!coll || coll == 0xFFFFFFFFu) return false;
    int32_t ci = hta_cache_find_tag_by_id(c, coll);
    if (ci < 0) return false;
    hta_tag_entry ct;
    uint32_t cb;
    if (!hta_cache_tag(c, (uint32_t)ci, &ct)) return false;
    if (!hta_cache_ptr_to_offset(c, ct.tag_data_ptr, &cb)) return false;

    hta_rd_f32(c, cb + COLL_MAX_BODY, &v->max_health);
    hta_rd_f32(c, cb + COLL_MAX_SHIELD, &v->max_shield);
    hta_rd_f32(c, cb + COLL_RECHARGE_TIME, &v->recharge_delay);
    float rate = 0.0f;
    hta_rd_f32(c, cb + COLL_SHIELD_RATE, &rate);
    /* A fraction of the full shield per tick: the cyborg's 0.01 is 30% a
     * second, so a broken shield is back in a little over three. */
    v->recharge_rate = rate * TICKS_PER_SECOND;

    if (!(v->max_health > 0.0f)) v->max_health = 75.0f;
    if (!(v->max_shield > 0.0f)) v->max_shield = 75.0f;

    /* Falling. */
    int32_t gi = hta_cache_find_tag_by_class(c, HTA_TAG_MATG);
    if (gi >= 0) {
        hta_tag_entry gt;
        uint32_t gb;
        if (hta_cache_tag(c, (uint32_t)gi, &gt) &&
            hta_cache_ptr_to_offset(c, gt.tag_data_ptr, &gb)) {
            uint32_t n = 0, p = 0, off = 0;
            if (hta_read_reflexive(c, gb + GLOBALS_FALLING, &n, &p) && n &&
                hta_cache_ptr_to_offset(c, p, &off)) {
                float h0 = 0.0f, h1 = 0.0f, mx = 0.0f;
                hta_rd_f32(c, off + FALL_HARMFUL, &h0);
                hta_rd_f32(c, off + FALL_HARMFUL + 4u, &h1);
                hta_rd_f32(c, off + FALL_MAX_VELOCITY, &mx);
                v->fall_harmful_min = h0 * TICKS_PER_SECOND;
                v->fall_harmful_max = h1 * TICKS_PER_SECOND;
                v->fall_fatal       = mx * TICKS_PER_SECOND;
            }
        }
    }
    if (!(v->fall_harmful_min > 0.0f)) v->fall_harmful_min = 4.5f;
    if (!(v->fall_harmful_max > v->fall_harmful_min))
        v->fall_harmful_max = v->fall_harmful_min * 1.4f;
    if (!(v->fall_fatal > v->fall_harmful_max))
        v->fall_fatal = v->fall_harmful_max * 1.6f;

    v->loaded = true;
    hta_vitals_reset(v);
    return true;
}

void hta_vitals_damage(hta_vitals *v, float amount)
{
    if (!v || amount <= 0.0f || v->died) return;
    v->took_damage = true;
    v->since_damage = 0.0f;

    /* The shield takes it first, and only what is left reaches the body. */
    if (v->shield > 0.0f) {
        float from_shield = amount < v->shield ? amount : v->shield;
        v->shield -= from_shield;
        amount -= from_shield;
        if (v->shield <= 0.0f) { v->shield = 0.0f; v->shield_broke = true; }
    }
    if (amount > 0.0f) {
        v->health -= amount;
        if (v->health <= 0.0f) { v->health = 0.0f; v->died = true; }
    }
}

float hta_vitals_land(hta_vitals *v, float speed)
{
    if (!v || !v->loaded) return 0.0f;
    if (speed <= v->fall_harmful_min) return 0.0f;      /* a free landing */

    if (speed >= v->fall_fatal) {
        float all = v->shield + v->health;
        hta_vitals_damage(v, all);
        return all;
    }
    /* Between the two harmful speeds Halo scales the hurt; past the upper
     * one it keeps climbing until the fatal speed. */
    float span = v->fall_fatal - v->fall_harmful_min;
    float t = (speed - v->fall_harmful_min) / (span > 0.0f ? span : 1.0f);
    float amount = t * (v->max_shield + v->max_health);
    hta_vitals_damage(v, amount);
    return amount;
}

void hta_vitals_update(hta_vitals *v, float dt)
{
    if (!v || !v->loaded) return;
    v->took_damage = false;
    v->shield_broke = false;
    if (dt <= 0.0f) return;

    v->since_damage += dt;
    if (v->died) return;
    if (v->since_damage < v->recharge_delay) return;
    if (v->shield >= v->max_shield) return;

    v->shield += v->max_shield * v->recharge_rate * dt;
    if (v->shield > v->max_shield) v->shield = v->max_shield;
}

float hta_vitals_health_fraction(const hta_vitals *v)
{
    if (!v || v->max_health <= 0.0f) return 1.0f;
    return v->health / v->max_health;
}

float hta_vitals_shield_fraction(const hta_vitals *v)
{
    if (!v) return 1.0f;
    if (v->max_shield <= 0.0f) return 0.0f;     /* no shield at all (a normal person): an empty bar */
    return v->shield / v->max_shield;
}
