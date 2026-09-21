#include "bot.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

/* How long a body lies there before it is put back. Ours -- there is no
 * gametype in the map to ask, and the player's own five seconds is the
 * nearest thing to a precedent. */
#define HTA_BOT_RESPAWN 5.0f

bool hta_bot_load(hta_bot *b, const hta_cache *c,
                  const hta_resource_map *bitmaps, uint32_t bipd_tag_id,
                  char *err, size_t errlen)
{
    if (!b || !c) {
        if (err) snprintf(err, errlen, "bad arguments");
        return false;
    }
    memset(b, 0, sizeof(*b));
    b->rng = 0x1234567u;
    if (!hta_actor_load(&b->actor, c, bitmaps, bipd_tag_id, err, errlen))
        return false;

    /* The same numbers the player runs on. A target that took a different
     * amount of killing than you do would make every weapon feel wrong. */
    if (!hta_vitals_load(&b->vitals, c)) {
        b->vitals.max_health = 75.0f;
        b->vitals.max_shield = 75.0f;
    }
    hta_vitals_reset(&b->vitals);

    /* Its shape is the biped's own, which is the shape the player is
     * already walking around inside. */
    hta_player_physics phys;
    memset(&phys, 0, sizeof(phys));
    char perr[HTA_ERRLEN];
    b->radius = 0.175f;
    b->height = 0.70f;
    if (hta_player_physics_load(&phys, c, perr, sizeof(perr))) {
        if (phys.radius > 0.0f) b->radius = phys.radius;
        if (phys.coll_stand > 0.0f) b->height = phys.coll_stand;
    }

    b->state = HTA_BOT_ALIVE;
    b->loaded = true;
    if (err && errlen)
        snprintf(err, errlen, "%.0f health, %.0f shield, %.2f wu across, %.2f tall",
                 b->vitals.max_health, b->vitals.max_shield,
                 b->radius * 2.0f, b->height);
    return true;
}

void hta_bot_free(hta_bot *b)
{
    if (!b) return;
    hta_actor_free(&b->actor);
    memset(b, 0, sizeof(*b));
}

void hta_bot_spawn(hta_bot *b, const float pos[3], float yaw)
{
    if (!b || !b->loaded) return;
    if (pos) for (int k = 0; k < 3; k++) b->pos[k] = pos[k];
    b->yaw = yaw;
    b->state = HTA_BOT_ALIVE;
    b->timer = 0.0f;
    b->took_damage = false;
    b->died = false;
    b->have_last_hit = false;
    hta_vitals_reset(&b->vitals);
    /* Standing, not mid-collapse.
     *
     * The name matters: hta_actor_play matches a SUBSTRING, and the cyborg's
     * 254 animations include a dozen seated ones. Plain "idle" finds
     * `B-driver unarmed idle` -- a body sitting in a Banshee, hanging in the
     * air. `stand rifle idle` is a man on his feet holding a rifle. */
    if (!hta_actor_play(&b->actor, "stand rifle idle", false))
        hta_actor_play(&b->actor, "stand unarmed idle", false);
    hta_actor_place(&b->actor, b->pos, b->yaw);
}

void hta_bot_update(hta_bot *b, float dt)
{
    if (!b || !b->loaded) return;
    b->took_damage = false;
    b->died = false;

    hta_vitals_update(&b->vitals, dt);

    if (b->state == HTA_BOT_DYING) {
        b->timer -= dt;
        if (b->timer <= 0.0f) {
            /* The clip has finished and is holding its last frame. Now it
             * lies there a while before it is put back. */
            b->state = HTA_BOT_GONE;
            b->timer = HTA_BOT_RESPAWN;
        }
    } else if (b->state == HTA_BOT_GONE) {
        b->timer -= dt;
        if (b->timer <= 0.0f) hta_bot_spawn(b, b->pos, b->yaw);
    }

    hta_actor_update(&b->actor, dt);
    hta_actor_place(&b->actor, b->pos, b->yaw);
}

void hta_bot_centre(const hta_bot *b, float out[3])
{
    if (!b || !out) return;
    out[0] = b->pos[0];
    out[1] = b->pos[1];
    out[2] = b->pos[2] + b->height * 0.5f;
}

bool hta_bot_near(const hta_bot *b, const float pos[3], float reach)
{
    if (!b || !b->loaded || !pos || b->state != HTA_BOT_ALIVE) return false;
    float dx = b->pos[0] - pos[0];
    float dy = b->pos[1] - pos[1];
    float flat = sqrtf(dx*dx + dy*dy);
    if (flat > reach + b->radius) return false;
    /* Vertically: anywhere alongside the body, not just level with its feet. */
    if (pos[2] < b->pos[2] - reach) return false;
    if (pos[2] > b->pos[2] + b->height + reach) return false;
    return true;
}

bool hta_bot_ray(const hta_bot *b, const float orig[3], const float dir[3],
                 float max_t, float *out_t, float out_hit[3])
{
    if (!b || !b->loaded || !orig || !dir) return false;
    if (b->state != HTA_BOT_ALIVE) return false;   /* a corpse stops nothing */

    /* An upright cylinder: solve in XY, then check the height of the entry
     * point. Halo's own hit detection is per-node spheres; a cylinder is
     * the honest approximation of a standing body and does not pretend to
     * headshots it cannot tell apart. */
    float ox = orig[0] - b->pos[0];
    float oy = orig[1] - b->pos[1];
    float a = dir[0]*dir[0] + dir[1]*dir[1];
    if (a < 1e-9f) {
        /* Straight up or down: inside the circle or nothing. */
        if (ox*ox + oy*oy > b->radius * b->radius) return false;
        float t = (dir[2] > 0.0f) ? (b->pos[2] - orig[2]) / dir[2]
                                  : (b->pos[2] + b->height - orig[2]) / dir[2];
        if (t < 0.0f || t > max_t) return false;
        if (out_t) *out_t = t;
        if (out_hit) for (int k = 0; k < 3; k++) out_hit[k] = orig[k] + dir[k] * t;
        return true;
    }
    float bq = 2.0f * (ox*dir[0] + oy*dir[1]);
    float cq = ox*ox + oy*oy - b->radius * b->radius;
    float disc = bq*bq - 4.0f*a*cq;
    if (disc < 0.0f) return false;
    float sq = sqrtf(disc);
    float t0 = (-bq - sq) / (2.0f*a);
    float t1 = (-bq + sq) / (2.0f*a);
    float t = (t0 >= 0.0f) ? t0 : t1;
    if (t < 0.0f || t > max_t) return false;

    float z = orig[2] + dir[2] * t;
    if (z < b->pos[2] || z > b->pos[2] + b->height) {
        /* Missed the body but the line may still pass through it: try the
         * far intersection before giving up, which is what happens shooting
         * upward from below its feet. */
        if (t == t0 && t1 >= 0.0f && t1 <= max_t) {
            float z1 = orig[2] + dir[2] * t1;
            if (z1 >= b->pos[2] && z1 <= b->pos[2] + b->height) {
                t = t1;
                z = z1;
            } else return false;
        } else return false;
    }
    if (out_t) *out_t = t;
    if (out_hit) {
        out_hit[0] = orig[0] + dir[0] * t;
        out_hit[1] = orig[1] + dir[1] * t;
        out_hit[2] = z;
    }
    return true;
}

void hta_bot_damage(hta_bot *b, float amount, const float at[3])
{
    if (!b || !b->loaded || b->state != HTA_BOT_ALIVE) return;
    if (!(amount > 0.0f)) return;

    hta_vitals_damage(&b->vitals, amount);
    b->took_damage = true;
    if (at) {
        for (int k = 0; k < 3; k++) b->last_hit[k] = at[k];
        b->have_last_hit = true;
    }
    if (!b->vitals.died) return;

    b->died = true;
    b->state = HTA_BOT_DYING;
    if (hta_actor_play_death(&b->actor, &b->rng)) {
        const hta_animation *an = &b->actor.graph.anims[b->actor.clip];
        b->timer = (float)an->frame_count / HTA_ANIM_FPS;
    } else {
        b->timer = 1.0f;
    }
}
