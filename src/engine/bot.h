/* Somebody to shoot at.
 *
 * A body in the world with health, a shield and a shape that stops bullets.
 * It does not think yet -- it stands where it is put, reacts to being hit,
 * dies and comes back. That is deliberately the whole of it: melee, grenades
 * and every weapon need a target before they can be said to work at all, and
 * the thinking is a separate problem to solve on top of a body that already
 * takes damage properly.
 *
 * Everything under it is already shared. `hta_actor` is the skinned cyborg
 * from the death camera, `hta_vitals` is the same health and shield the
 * player runs on, and the hit shape is the biped tag's own collision
 * cylinder -- the one the player is already walking around inside.
 */
#ifndef HTA_BOT_H
#define HTA_BOT_H

#include "actor.h"
#include "vitals.h"
#include "../asset/biped.h"

typedef enum {
    HTA_BOT_ALIVE = 0,
    HTA_BOT_DYING,      /* playing a kill animation */
    HTA_BOT_GONE        /* waiting to come back */
} hta_bot_state;

typedef struct {
    hta_actor     actor;
    hta_vitals    vitals;
    float         pos[3];       /* feet */
    float         yaw;
    /* The biped's own collision cylinder. The Trial's cyborg is 0.35 wu
     * across and 0.7 tall -- a bit over a metre wide and two metres high. */
    float         radius;
    float         height;

    hta_bot_state state;
    float         timer;        /* dying: clip left. gone: until it returns */
    uint32_t      rng;
    float         flinch;       /* seconds left of a hit reaction */
    bool          loaded;

    /* One-shots, true for the update in which they happened. */
    bool          took_damage;
    bool          died;
    float         last_hit[3];  /* where the last damage landed */
    bool          have_last_hit;
} hta_bot;

/* The cyborg's model, animations, vitals and collision shape. */
bool hta_bot_load(hta_bot *b, const hta_cache *c,
                  const hta_resource_map *bitmaps, uint32_t bipd_tag_id,
                  char *err, size_t errlen);
void hta_bot_free(hta_bot *b);

/* Put a weapon in its hand -- see hta_actor_hold. `stand rifle idle` poses
 * the hands to hold a rifle, and without one the body reads as a man
 * standing with his arms out. */
bool hta_bot_arm(hta_bot *b, const hta_cache *c,
                 const hta_resource_map *bitmaps, uint32_t model_tag_id,
                 char *err, size_t errlen);

/* Stand it somewhere, whole. */
void hta_bot_spawn(hta_bot *b, const float pos[3], float yaw);

/* Animation, dying, and coming back. */
void hta_bot_update(hta_bot *b, float dt);

/* A ray against its cylinder. Only while it is alive -- a corpse does not
 * stop bullets. `out_t` is the distance along `dir`. */
bool hta_bot_ray(const hta_bot *b, const float orig[3], const float dir[3],
                 float max_t, float *out_t, float out_hit[3]);

/* Is `pos` within `reach` of its body? For melee, which does not care where
 * you are aiming so much as how close you are. */
bool hta_bot_near(const hta_bot *b, const float pos[3], float reach);

/* Hurt it. `at` may be NULL. Spends the shield first, exactly as the player
 * does, and starts a death when the body runs out. */
void hta_bot_damage(hta_bot *b, float amount, const float at[3]);

/* The centre of its chest, for aiming a camera or a blast at. */
void hta_bot_centre(const hta_bot *b, float out[3]);

#endif
