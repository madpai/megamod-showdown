/* Health, shield, and the things that take them away.
 *
 * Every number is the Trial's own. The cyborg's `coll`
 * (ModelCollisionGeometry, 664, reconciles) carries the maxima and how the
 * shield comes back:
 *
 *   maximum body vitality  +8     maximum shield vitality +204
 *   recharge time        +272     shield recharge rate    +448
 *
 * and `globals` carries falling damage at +392 (GlobalsFallingDamage, 152):
 * a harmful velocity range and the speed at which a fall is simply fatal.
 *
 * Note the units: those falling velocities are per TICK, like the
 * projectile speeds -- the cyborg starts taking damage at 0.15, which is
 * 4.5 world units a second, not 0.15.
 */
#ifndef HTA_VITALS_H
#define HTA_VITALS_H

#include "../asset/cache.h"

typedef struct {
    float health, shield;
    float max_health, max_shield;

    float recharge_delay;    /* seconds of no damage before the shield returns */
    float recharge_rate;     /* fraction of the full shield per second */
    float since_damage;

    /* Landing speeds, in world units per second. */
    float fall_harmful_min;  /* below this a landing is free */
    float fall_harmful_max;  /* at this it costs the full harmful amount */
    float fall_fatal;        /* and at this it is simply fatal */

    bool  loaded;
    /* One-shot events, true for the update in which they happened. */
    bool  took_damage;
    bool  shield_broke;
    bool  died;
} hta_vitals;

/* Reads the player biped's collision model and the globals. False leaves
 * everything zeroed. */
bool hta_vitals_load(hta_vitals *v, const hta_cache *c);

/* Full health and shield. */
void hta_vitals_reset(hta_vitals *v);

/* Shield regrowth and the delay before it. */
void hta_vitals_update(hta_vitals *v, float dt);

/* Halo spends the shield first and the body only once it is gone. */
void hta_vitals_damage(hta_vitals *v, float amount);

/* Hitting the ground at `speed` world units a second. Returns what it
 * cost. */
float hta_vitals_land(hta_vitals *v, float speed);

/* 0..1, for the HUD. */
float hta_vitals_health_fraction(const hta_vitals *v);
float hta_vitals_shield_fraction(const hta_vitals *v);

#endif
