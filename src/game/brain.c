#include "brain.h"
#include "game.h"
#include "../asset/items.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265f

/* Everything below is ours: Halo CE has no multiplayer AI to take numbers
 * from. Per skill, easy .. legendary. */
static const float SIGHT_RANGE[4]  = { 35.0f, 45.0f, 60.0f, 80.0f };  /* wu */
static const float FOV_COS[4]      = { 0.50f, 0.34f, 0.17f, 0.0f };   /* half-angle */
static const float TURN_RATE[4]    = { 2.5f, 3.5f, 5.0f, 7.0f };      /* rad/s */
static const float REACT_TIME[4]   = { 0.70f, 0.45f, 0.30f, 0.18f };  /* s */
static const float AIM_ERROR[4]    = { 0.14f, 0.09f, 0.05f, 0.025f }; /* rad, settles */
static const float AIM_SETTLE[4]   = { 0.8f, 1.2f, 1.8f, 2.6f };      /* /s */
#define THINK_LOST     3.0f    /* s a lost target is chased to its last sighting */
#define REPLAN         1.5f    /* s between path refreshes while chasing */
#define WAYPOINT_REACH 0.30f   /* wu */
#define GRENADE_WAIT   5.0f    /* s between throws */
#define PATH_BUDGET    60000u  /* A* expansions */

static uint32_t rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }
static float frand(uint32_t *s) { return (float)(rnd(s) & 0xFFFFu) / 65535.0f; }

static float wrap(float a)
{
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

void hta_brain_reset(hta_brain *b)
{
    b->target = -1;
    b->visible = false;
    b->seen_ago = 1e9f;
    b->path_len = b->path_i = 0;
    b->goal = HTA_NAV_NONE;
    b->goal_item = -1;
    b->replan = 0.0f;
    b->react = 0.0f;
    b->aim_err[0] = b->aim_err[1] = 0.0f;
    b->strafe = 0.0f;
    b->strafe_timer = 0.0f;
    b->grenade_timer = GRENADE_WAIT * 0.5f;
    b->stuck_timer = 0.0f;
    b->look_timer = 0.0f;
}

void hta_brain_init(hta_brain *b, uint8_t skill, uint32_t seed)
{
    memset(b, 0, sizeof(*b));
    b->skill = skill > 3 ? 3 : skill;
    b->rng = seed ? seed : 1u;
    hta_brain_reset(b);
}

/* How much a bot wants a weapon. Ours, and roughly how Blood Gulch is
 * actually played: the sniper and the rocket are what people run for. */
static int want(const hta_game_weapon *w)
{
    if (!w) return 0;
    const char *l = w->label;
    if (!strcmp(l, "rl")) return 9;
    if (!strcmp(l, "sr")) return 8;
    if (!strcmp(l, "sg")) return 6;
    if (!strcmp(l, "hp")) return 6;
    if (!strcmp(l, "pc")) return 5;
    if (!strcmp(l, "pr")) return 5;
    if (!strcmp(l, "ar")) return 4;
    if (!strcmp(l, "ne")) return 4;
    if (!strcmp(l, "ft")) return 3;
    if (!strcmp(l, "pp")) return 3;
    return 1;
}

/* Where a weapon is worth firing from, in wu. */
static float best_range(const hta_game_weapon *w)
{
    if (!w) return 5.0f;
    const char *l = w->label;
    if (!strcmp(l, "sg") || !strcmp(l, "ft")) return 2.0f;
    if (!strcmp(l, "sr")) return 30.0f;
    if (!strcmp(l, "rl")) return 12.0f;
    if (!strcmp(l, "hp")) return 15.0f;
    return 8.0f;
}

static float max_range(const hta_game_weapon *w)
{
    if (!w) return 10.0f;
    const char *l = w->label;
    if (!strcmp(l, "sg")) return 7.0f;
    if (!strcmp(l, "ft")) return 5.0f;
    if (!strcmp(l, "sr")) return 90.0f;
    if (!strcmp(l, "hp")) return 45.0f;
    if (!strcmp(l, "rl")) return 40.0f;
    if (!strcmp(l, "ne") || !strcmp(l, "pp")) return 18.0f;
    return 28.0f;
}

static bool sees(const hta_game *g, int32_t me, int32_t them, float range, float fov_cos,
                 float *out_dist)
{
    const hta_unit *u = &g->units[me], *t = &g->units[them];
    float c[3];
    hta_game_centre(g, them, c);
    float d[3] = { c[0]-u->eye.pos[0], c[1]-u->eye.pos[1], c[2]-u->eye.pos[2] };
    float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (dist > range || dist < 1e-3f) return false;
    /* Camouflage: only close, or when it has just fired. */
    if (t->powerup == HTA_ITEM_CAMOUFLAGE && dist > 4.0f && !t->fired) return false;
    float f[2] = { cosf(u->eye.yaw), sinf(u->eye.yaw) };
    float flat = hypotf(d[0], d[1]);
    if (flat > 1e-3f && (f[0]*d[0] + f[1]*d[1]) / flat < fov_cos &&
        u->last_attacker != them) return false;
    for (int k = 0; k < 3; k++) d[k] /= dist;
    float hit;
    if (g->col && hta_collision_ray(g->col, u->eye.pos, d, dist - 0.1f, &hit, NULL, NULL))
        return false;
    if (out_dist) *out_dist = dist;
    return true;
}

static void plan_to(hta_game *g, int32_t me, hta_brain *b, const float to[3])
{
    b->path_len = b->path_i = 0;
    if (!g->nav || !g->nav->built) return;
    const hta_unit *u = &g->units[me];
    uint32_t from = hta_nav_nearest(g->nav, u->body.pos, 1.5f);
    uint32_t goal = hta_nav_nearest(g->nav, to, 2.0f);
    if (from == HTA_NAV_NONE || goal == HTA_NAV_NONE) return;
    b->goal = goal;
    uint32_t n = hta_nav_path(g->nav, from, goal, b->path, HTA_BRAIN_PATH, PATH_BUDGET);
    if (!n) { b->goal = HTA_NAV_NONE; return; }
    b->path_len = hta_nav_smooth(g->nav, b->path, n);
    b->path_i = b->path_len > 1 ? 1 : 0;
}

/* The world direction the path wants, or false when there is no path. */
static bool follow(hta_game *g, int32_t me, hta_brain *b, float out[2])
{
    const hta_unit *u = &g->units[me];
    while (b->path_i < b->path_len) {
        float w[3];
        hta_nav_pos(g->nav, b->path[b->path_i], w);
        float dx = w[0] - u->body.pos[0], dy = w[1] - u->body.pos[1];
        float d = hypotf(dx, dy);
        if (d < WAYPOINT_REACH) { b->path_i++; continue; }
        out[0] = dx / d;
        out[1] = dy / d;
        return true;
    }
    return false;
}

/* Something worth walking to: a better weapon, health when hurt, a
 * powerup. The nearest worthwhile item that is on the ground now. */
static int32_t pick_item(hta_game *g, int32_t me)
{
    hta_pickups *it = g->items;
    if (!it || !it->loaded) return -1;
    const hta_unit *u = &g->units[me];
    const hta_game_weapon *held = hta_game_held(g, me);
    int have = want(held);
    int other = u->carry[u->slot ^ 1u].weapon >= 0
              ? want(&g->weapons[u->carry[u->slot ^ 1u].weapon]) : 0;
    if (other > have) have = other;
    float hurt = u->vitals.max_health > 0.0f ? u->vitals.health / u->vitals.max_health : 1.0f;
    int32_t best = -1;
    float best_score = 0.0f;
    for (uint32_t i = 0; i < it->count; i++) {
        if (!it->slot[i].present) continue;
        const hta_item_choice *c = hta_pickups_item(it, (int32_t)i);
        if (!c) continue;
        float value = 0.0f;
        switch (c->kind) {
        case HTA_ITEM_WEAPON: {
            int32_t wi = hta_game_weapon_index(g, c->tag_id);
            int v = wi >= 0 ? want(&g->weapons[wi]) : 0;
            if (v > have) value = (float)(v - have) * 3.0f;
            break;
        }
        case HTA_ITEM_HEALTH: value = hurt < 0.6f ? 10.0f * (1.0f - hurt) : 0.0f; break;
        case HTA_ITEM_OVERSHIELD: case HTA_ITEM_CAMOUFLAGE: value = 12.0f; break;
        case HTA_ITEM_GRENADE: value = u->grenades < g->max_grenades ? 1.5f : 0.0f; break;
        default: break;
        }
        if (value <= 0.0f) continue;
        float d = hypotf(it->spawn[i].position[0] - u->body.pos[0],
                         it->spawn[i].position[1] - u->body.pos[1]);
        float score = value / (1.0f + d * 0.08f);
        if (score > best_score) { best_score = score; best = (int32_t)i; }
    }
    return best;
}

void hta_brain_think(struct hta_game *g, int32_t me, hta_brain *b, float dt)
{
    hta_unit *u = &g->units[me];
    hta_unit_input *in = &u->in;
    memset(&in->move, 0, sizeof(in->move));
    in->pickup = false;
    const uint8_t sk = b->skill;
    const hta_game_weapon *w = hta_game_held(g, me);
    hta_carried *held = &u->carry[u->slot & 1u];

    /* ---- eyes ---- */
    int32_t seen = -1;
    float seen_dist = 1e9f;
    for (uint32_t i = 0; i < g->unit_count; i++) {
        const hta_unit *o = &g->units[i];
        if ((int32_t)i == me || !o->alive || o->kind == HTA_UNIT_NONE) continue;
        if (g->teams && o->team == u->team) continue;
        float d;
        /* Keep the one being fought unless another is much closer. */
        if (!sees(g, me, (int32_t)i, SIGHT_RANGE[sk], FOV_COS[sk], &d)) continue;
        if ((int32_t)i == b->target) d *= 0.6f;
        if (d < seen_dist) { seen_dist = d; seen = (int32_t)i; }
    }
    if (seen >= 0) {
        if (seen != b->target || !b->visible) {
            b->react = REACT_TIME[sk] * (0.7f + 0.6f * frand(&b->rng));
            b->aim_err[0] = (frand(&b->rng) - 0.5f) * AIM_ERROR[sk] * 6.0f;
            b->aim_err[1] = (frand(&b->rng) - 0.5f) * AIM_ERROR[sk] * 3.0f;
        }
        b->target = seen;
        b->visible = true;
        b->seen_ago = 0.0f;
        hta_game_centre(g, seen, b->seen_pos);
    } else {
        b->visible = false;
        b->seen_ago += dt;
        if (b->target >= 0 && (!g->units[b->target].alive || b->seen_ago > THINK_LOST))
            b->target = -1;
    }
    /* Shot at by somebody unseen: turn to face them. */
    if (!b->visible && u->last_attacker >= 0 && u->last_attacker != me &&
        u->since_attacked < 0.3f && g->units[u->last_attacker].alive) {
        b->target = u->last_attacker;
        hta_game_centre(g, b->target, b->seen_pos);
        b->seen_ago = 0.5f;
    }

    /* ---- where to look ---- */
    float want_yaw = u->eye.yaw, want_pitch = 0.0f;
    float move[2] = { 0.0f, 0.0f };
    bool shoot = false;
    if (b->target >= 0) {
        const hta_unit *t = &g->units[b->target];
        float aim[3];
        if (b->visible) {
            hta_game_centre(g, b->target, aim);
            /* Lead a moving target with a round that flies. */
            if (w && w->travels && w->speed > 1.0f) {
                float tof = seen_dist / w->speed;
                for (int k = 0; k < 2; k++) aim[k] += t->body.velocity[k] * tof;
            }
        } else {
            for (int k = 0; k < 3; k++) aim[k] = b->seen_pos[k];
        }
        float d[3] = { aim[0]-u->eye.pos[0], aim[1]-u->eye.pos[1], aim[2]-u->eye.pos[2] };
        float flat = hypotf(d[0], d[1]);
        want_yaw = atan2f(d[1], d[0]);
        want_pitch = atan2f(d[2], flat);
        if (b->visible) {
            /* The error settles while it tracks, and jumps when the target
             * does something sudden -- here, simply decays. */
            float settle = expf(-AIM_SETTLE[sk] * dt);
            b->aim_err[0] *= settle;
            b->aim_err[1] *= settle;
            want_yaw += b->aim_err[0];
            want_pitch += b->aim_err[1];
            if (b->react > 0.0f) b->react -= dt;
            float off = fabsf(wrap(want_yaw - u->eye.yaw)) + fabsf(want_pitch - u->eye.pitch);
            float tol = 0.05f + (flat > 1e-3f ? 0.15f / flat : 0.0f);
            if (b->react <= 0.0f && off < tol && seen_dist < max_range(w))
                shoot = true;
        }
    } else {
        /* Nobody about: look where it is going, with an occasional glance. */
        b->look_timer -= dt;
        if (b->look_timer <= 0.0f) {
            b->wander_yaw = (frand(&b->rng) - 0.5f) * 1.2f;
            b->look_timer = 1.5f + frand(&b->rng) * 2.5f;
        }
    }

    /* ---- where to go ---- */
    b->replan -= dt;
    bool fighting = b->target >= 0 && b->visible;
    if (fighting) {
        /* Hold a useful distance and strafe. */
        b->strafe_timer -= dt;
        if (b->strafe_timer <= 0.0f) {
            b->strafe = frand(&b->rng) < 0.5f ? -1.0f : 1.0f;
            if (frand(&b->rng) < 0.2f) b->strafe = 0.0f;
            b->strafe_timer = 0.4f + frand(&b->rng) * 1.1f;
        }
        float to[2] = { cosf(want_yaw), sinf(want_yaw) };
        float side[2] = { to[1], -to[0] };
        float close = seen_dist - best_range(w);
        float fwd = close > 1.5f ? 1.0f : (close < -1.5f ? -0.7f : 0.0f);
        move[0] = to[0] * fwd + side[0] * b->strafe;
        move[1] = to[1] * fwd + side[1] * b->strafe;
        /* A jump now and then, the way people dodge. */
        if (sk >= 1 && u->body.on_ground && frand(&b->rng) < 0.25f * dt * (float)sk)
            in->move.jump = true;
        b->path_len = 0;
        /* Too close for a gun: hit him. */
        if (seen_dist < HTA_GAME_MELEE_REACH + 0.25f && u->swing <= 0.0f) in->melee = true;
        /* A grenade at the middle distance, when the throw can reach. */
        b->grenade_timer -= dt;
        if (b->grenade_timer <= 0.0f && u->grenades > 0 && seen_dist > 5.0f &&
            seen_dist < 18.0f && b->react <= 0.0f) {
            in->grenade = true;
            b->grenade_timer = GRENADE_WAIT * (0.7f + frand(&b->rng));
        }
    } else {
        if (b->target >= 0) {
            /* Lost sight: go to where he was. */
            if (b->replan <= 0.0f || !b->path_len) {
                plan_to(g, me, b, b->seen_pos);
                b->replan = REPLAN;
            }
        } else {
            int32_t item = pick_item(g, me);
            if (item != b->goal_item || b->replan <= 0.0f ||
                b->path_i >= b->path_len) {
                b->goal_item = item;
                if (item >= 0) {
                    plan_to(g, me, b, g->items->spawn[item].position);
                } else if (g->nav && b->path_i >= b->path_len) {
                    uint32_t r = hta_nav_random(g->nav, &b->rng);
                    if (r != HTA_NAV_NONE) {
                        float p[3];
                        hta_nav_pos(g->nav, r, p);
                        plan_to(g, me, b, p);
                    }
                }
                b->replan = 4.0f;
            }
            if (b->goal_item >= 0) in->pickup = true;
        }
        float dir[2];
        if (g->nav && follow(g, me, b, dir)) {
            move[0] = dir[0];
            move[1] = dir[1];
            if (b->target < 0) want_yaw = atan2f(dir[1], dir[0]) + b->wander_yaw * 0.4f;
            want_pitch = 0.0f;
        }
    }
    /* Standing on a weapon it wants more than what it holds: take it --
     * the map's, or one somebody dropped. */
    {
        int32_t dr = hta_game_drop_near(g, u->body.pos, HTA_DROP_REACH);
        if (dr >= 0 && want(&g->weapons[g->drops[dr].weapon]) > want(w)) in->pickup = true;
    }
    if (g->items) {
        int32_t ws = hta_pickups_at_kind(g->items, u->body.pos, HTA_ITEM_WEAPON);
        const hta_item_choice *c = hta_pickups_item(g->items, ws);
        if (c) {
            int32_t wi = hta_game_weapon_index(g, c->tag_id);
            if (wi >= 0 && want(&g->weapons[wi]) > want(w)) in->pickup = true;
        }
    }

    /* ---- stuck? ---- */
    b->stuck_timer += dt;
    if (b->stuck_timer > 1.0f) {
        float moved = hypotf(u->body.pos[0] - b->last_pos[0], u->body.pos[1] - b->last_pos[1]);
        bool trying = hypotf(move[0], move[1]) > 0.5f;
        if (trying && moved < 0.25f) {
            in->move.jump = true;
            if (moved < 0.05f) { b->path_len = 0; b->replan = 0.0f; b->goal_item = -1; }
        }
        b->stuck_timer = 0.0f;
        for (int k = 0; k < 3; k++) b->last_pos[k] = u->body.pos[k];
    }

    /* ---- the thumbs ---- */
    float dyaw = wrap(want_yaw - u->eye.yaw);
    float dpitch = want_pitch - u->eye.pitch;
    float turn = TURN_RATE[sk] * dt;
    if (dyaw > turn) dyaw = turn;
    if (dyaw < -turn) dyaw = -turn;
    if (dpitch > turn) dpitch = turn;
    if (dpitch < -turn) dpitch = -turn;
    in->move.look_yaw = dyaw;
    in->move.look_pitch = dpitch;
    /* The move is a world direction; the controls are relative to the
     * body's facing after this update's turn. */
    float yaw = u->eye.yaw + dyaw;
    float f[2] = { cosf(yaw), sinf(yaw) };
    float r[2] = { f[1], -f[0] };
    float ml = hypotf(move[0], move[1]);
    if (ml > 1.0f) { move[0] /= ml; move[1] /= ml; }
    in->move.move_forward = move[0]*f[0] + move[1]*f[1];
    in->move.move_right = move[0]*r[0] + move[1]*r[1];
    in->move.fire = shoot;

    /* ---- the weapon ---- */
    if (held->weapon >= 0 && !fighting) {
        if (held->ammo.loaded < held->ammo.mag_max / 2 && held->ammo.reserve > 0 &&
            held->ammo.phase == HTA_AMMO_READY)
            in->reload = true;
    }
    int32_t other = u->carry[u->slot ^ 1u].weapon;
    if (other >= 0) {
        const hta_game_weapon *ow = &g->weapons[other];
        const hta_ammo *oa = &u->carry[u->slot ^ 1u].ammo;
        bool empty = held->ammo.loaded == 0 && held->ammo.reserve == 0;
        bool other_has = oa->loaded + oa->reserve > 0;
        bool better = fighting &&
            fabsf(seen_dist - best_range(ow)) + 2.0f < fabsf(seen_dist - best_range(w));
        if (other_has && (empty || (better && u->cooldown <= 0.0f && frand(&b->rng) < dt)))
            in->swap = true;
    }
}
