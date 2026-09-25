#include "brain.h"
#include "game.h"
#include "../asset/items.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
#define PUSH_STOP      5.0f    /* wu: closer than this, an objective waits */
#define RIDE_REACH     8.0f    /* wu: a teammate's vehicle this close is worth a seat */
#define RIDE_ALONE     2.0f    /* s a gunner waits for a driver before it gets out */
#define GUN_RANGE     40.0f    /* wu a gunner opens fire at */
#define BOARD_REACH   18.0f    /* wu: an empty vehicle this close is worth the walk */
#define BOARD_GIVE_UP  8.0f    /* s walking to a wheel before giving up on it */
#define BOARD_SKIP    10.0f    /* s before a car given up on is tried again */
#define BOARD_FIGHT   15.0f    /* wu: an enemy this close is fought on foot */
#define BOARD_OBJECTIVE 30.0f  /* wu: a flag this near is walked to */
#define DRIVE_REACH    2.5f    /* wu: a path corner this close is passed */
#define DRIVE_ARRIVE   6.0f    /* wu: a roam goal this close is reached */
#define DRIVE_FROM     6.0f    /* wu a car looks round itself for open ground */
#define DRIVE_TO      15.0f    /* wu round a goal it looks for open ground */
#define DISMOUNT_NEAR 10.0f    /* wu from a flag: out and on foot */
#define DISMOUNT_HULL  0.25f   /* hull fraction a bot bails out below */
#define WAIT_GUNNER    3.0f    /* s a bot driver holds for a teammate on the gun */
#define TANK_RANGE    25.0f    /* wu a tank holds off at and shells from */
#define GHOST_RANGE   12.0f    /* wu a Ghost closes to before it circles */
#define ORBIT         14.0f    /* wu a Warthog circles at while its gunner works */
#define STUCK_LIMIT    3u      /* jams in a row before a bot walks */
#define AVOID_AHEAD    8.0f    /* wu: a car nearer than this on the way is steered round */
#define AVOID_GAP      0.8f    /* wu kept between the bodies passing it */
#define STRAFE_LOOK    3.0f    /* wu: a Ghost strafes only toward open ground this far aside */
#define JAM_HELD       0.3f    /* s of a second the physics refused a move: a jam */
#define JAM_SKIP      (HTA_VEHICLE_RESPAWN + 5.0f)  /* s: a car it jammed stays left until it goes home */
#define ROAM_MIN      30.0f    /* wu: a drive with nothing to do goes this far... */
#define ROAM_MAX      70.0f    /* ...and no further, so the path search finds it */
#define BEHIND         1.9f    /* rad: a goal this far round, a Warthog backs round */

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
    b->item_skip = -1;
    b->item_skip_time = b->item_time = b->item_there = 0.0f;
    b->goal_game = false;
    b->replan = 0.0f;
    b->react = 0.0f;
    b->aim_err[0] = b->aim_err[1] = 0.0f;
    b->strafe = 0.0f;
    b->strafe_timer = 0.0f;
    b->grenade_timer = GRENADE_WAIT * 0.5f;
    b->stuck_timer = 0.0f;
    b->look_timer = 0.0f;
    b->board = b->board_skip = b->jam_car = -1;
    b->board_time = b->skip_time = 0.0f;
    b->dismount = b->reversing = b->roaming = false;
    b->reverse_timer = b->wait_gunner = 0.0f;
    b->stuck_count = 0;
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
    if(w && w->melee_only) return 1.0f;
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
    if(w && w->melee_only) return 2.2f;
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

/* Where a bot looks from. Seated, the eye is the vehicle's camera --
 * behind and above a Warthog, sometimes in a hillside -- so a rider looks
 * from its own body in the seat instead. */
static void eye_of(const hta_game *g, int32_t me, float out[3])
{
    const hta_unit *u = &g->units[me];
    if (u->vehicle >= 0) {
        hta_game_centre(g, me, out);
        out[2] += 0.25f;
        return;
    }
    for (int k = 0; k < 3; k++) out[k] = u->eye.pos[k];
}

static bool sees(const hta_game *g, int32_t me, int32_t them, float range, float fov_cos,
                 float *out_dist)
{
    const hta_unit *u = &g->units[me], *t = &g->units[them];
    float c[3], e[3];
    hta_game_centre(g, them, c);
    eye_of(g, me, e);
    float d[3] = { c[0]-e[0], c[1]-e[1], c[2]-e[2] };
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
    /* Not through its own ride, nor the one the target sits in. */
    hta_collision_instance *own = NULL, *theirs = NULL;
    bool own_was = false, theirs_was = false;
    if (g->vehicles && u->vehicle >= 0) {
        own = &g->vehicles->inst[u->vehicle]; own_was = own->active; own->active = false;
    }
    if (g->vehicles && t->vehicle >= 0 && t->vehicle != u->vehicle) {
        theirs = &g->vehicles->inst[t->vehicle]; theirs_was = theirs->active; theirs->active = false;
    }
    bool blocked = g->col && hta_collision_ray(g->col, e, d, dist - 0.1f, &hit, NULL, NULL);
    if (own) own->active = own_was;
    if (theirs) theirs->active = theirs_was;
    if (blocked) return false;
    if (out_dist) *out_dist = dist;
    return true;
}

/* A round that explodes is aimed at the feet: a near miss into the ground
 * still catches him, where one past his chest goes off far behind. How
 * people use rockets and the tank. */
static void feet_for_blast(const hta_game *g, int32_t them, const hta_game_weapon *w, float aim[3])
{
    if (!w || !(w->blast_damage > 0.0f)) return;
    const hta_unit *t = &g->units[them];
    if (t->vehicle >= 0 || !t->body.on_ground) return;
    aim[2] = t->body.pos[2] + 0.1f;
}

/* The same reach on a finer grid needs as many more expansions as it has
 * nodes per area. */
static uint32_t path_budget(const hta_nav *n)
{
    float k = n->cell > 0.05f ? HTA_NAV_CELL / n->cell : 1.0f;
    return (uint32_t)((float)PATH_BUDGET * k * k);
}

static void plan_to(hta_game *g, int32_t me, hta_brain *b, const float to[3])
{
    /* A search that fails has spent its whole budget -- a goal down a
     * one-way drop shares the region but not a way back. Once a second is
     * plenty; every frame was most of a match's CPU on de_dust2. Ours. */
    b->path_len = b->path_i = 0;
    if (!g->nav || !g->nav->built) return;
    const hta_unit *u = &g->units[me];
    uint32_t from = hta_nav_nearest(g->nav, u->body.pos, 1.5f);
    uint32_t goal = hta_nav_nearest(g->nav, to, 2.0f);
    if (from == HTA_NAV_NONE || goal == HTA_NAV_NONE) return;
    if (b->plan_wait > 0.0f && goal == b->plan_fail) return;
    b->goal = goal;
    uint32_t n = hta_nav_path(g->nav, from, goal, b->path, HTA_BRAIN_PATH, path_budget(g->nav));
    if (!n) { b->goal = HTA_NAV_NONE; b->plan_wait = 1.0f; b->plan_fail = goal; return; }
    b->path_len = hta_nav_smooth(g->nav, b->path, n);
    b->path_i = b->path_len > 1 ? 1 : 0;
}

/* The same, down a flag stand's field: no search, however far. */
static void plan_field(hta_game *g, int32_t me, hta_brain *b, int stand)
{
    b->path_len = b->path_i = 0;
    if (!g->nav || !g->nav->built) return;
    uint32_t from = hta_nav_nearest(g->nav, g->units[me].body.pos, 1.5f);
    if (from == HTA_NAV_NONE) return;
    const uint32_t *next = g->stand_field[stand];
    if (from != g->stand_node[stand] && next[from] == HTA_NAV_NONE) return;
    b->goal = g->stand_node[stand];
    uint32_t n = hta_nav_field_path(g->nav, next, from, b->path, HTA_BRAIN_PATH);
    b->path_len = hta_nav_smooth(g->nav, b->path, n);
    b->path_i = b->path_len > 1 ? 1 : 0;
}

/* Is the straight line from `a` to `b` open ground for a car? */
static bool open_line(const hta_game *g, const float a[3], const float b[3], uint8_t clear)
{
    if (!g->nav || !g->nav->built) return true;
    uint32_t na = hta_nav_nearest_wide(g->nav, a, 1.0f, clear);
    uint32_t nb = hta_nav_nearest_wide(g->nav, b, 1.0f, clear);
    if (na == HTA_NAV_NONE || nb == HTA_NAV_NONE) return false;
    return hta_nav_straight_wide(g->nav, na, nb, clear);
}

/* Is there open ground for this car STRAFE_LOOK wu to one side? */
static bool side_open(const hta_game *g, const float here[3], const float right[2],
                      float sign, uint8_t clear)
{
    if (!g->nav || !g->nav->built) return true;
    float at[3] = { here[0] + right[0] * sign * STRAFE_LOOK,
                    here[1] + right[1] * sign * STRAFE_LOOK, here[2] + 0.5f };
    uint32_t na = hta_nav_nearest_wide(g->nav, here, 1.0f, clear);
    uint32_t nb = hta_nav_nearest_wide(g->nav, at, 0.7f, clear);
    return na != HTA_NAV_NONE && nb != HTA_NAV_NONE && hta_nav_straight_wide(g->nav, na, nb, clear);
}

/* At the wheel: a path over ground wide enough for this car, from where
 * it is to the open ground nearest `to` -- a flag's stand indoors becomes
 * the ground outside its door. */
static void plan_drive(hta_game *g, int32_t me, hta_brain *b, const float to[3], uint8_t clear)
{
    b->path_len = b->path_i = 0;
    if (!g->nav || !g->nav->built) return;
    const float *here = g->vehicles->cars[g->units[me].vehicle].pos;
    uint32_t from = hta_nav_nearest(g->nav, here, 1.5f);
    if (from == HTA_NAV_NONE) from = hta_nav_nearest_wide(g->nav, here, DRIVE_FROM, clear);
    uint32_t goal = hta_nav_nearest_wide(g->nav, to, DRIVE_TO, clear);
    if (from == HTA_NAV_NONE || goal == HTA_NAV_NONE) return;
    b->goal = goal;
    uint32_t n = hta_nav_path_wide(g->nav, from, goal, b->path, HTA_BRAIN_PATH, path_budget(g->nav), clear);
    if (!n) { b->goal = HTA_NAV_NONE; return; }
    b->path_len = hta_nav_smooth_wide(g->nav, b->path, n, clear);
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
static int32_t pick_item(hta_game *g, int32_t me, int32_t skip)
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
        if (!it->slot[i].present || (int32_t)i == skip) continue;
        const hta_item_choice *c = hta_pickups_item(it, (int32_t)i);
        if (!c) continue;
        float value = 0.0f;
        switch (c->kind) {
        case HTA_ITEM_WEAPON: {
            int32_t wi = hta_game_weapon_index(g, c->tag_id);
            int v = hta_game_can_equip(g, me, wi) ? want(&g->weapons[wi]) : 0;
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

/* A teammate driving a vehicle with its gun free and near enough to climb
 * on. Returns the car, -1 for none. */
static int32_t ride_offer(const hta_game *g, int32_t me, int32_t *out_seat, float door[3])
{
    const hta_unit *u = &g->units[me];
    if (!g->teams || !g->vehicles || u->flag >= 0 || u->vehicle >= 0) return -1;
    const hta_vehicles *v = g->vehicles;
    int32_t best = -1;
    float best_d = RIDE_REACH;
    for (uint32_t c = 0; c < v->count; c++) {
        const hta_vehicle *car = &v->cars[c];
        if (!car->active) continue;
        int32_t ds = hta_vehicles_driver_seat(v, c), gs = hta_vehicles_gunner_seat(v, c);
        if (ds < 0 || gs < 0 || ds == gs || car->occupant[gs] >= 0) continue;
        int32_t driver = car->occupant[ds];
        if (driver < 0 || driver >= (int32_t)g->unit_count) continue;
        const hta_unit *d = &g->units[driver];
        if (d->team != u->team) continue;
        const hta_vehicle_seat *st = hta_vehicles_seat(v, c, (uint32_t)gs);
        hta_transform w;
        hta_vehicles_world(v, c, &w);
        float at[3];
        hta_xf_point(at, &w, st->enter);
        float dist = hypotf(at[0] - u->body.pos[0], at[1] - u->body.pos[1]);
        if (dist < best_d) {
            best_d = dist; best = (int32_t)c; *out_seat = gs;
            for (int k = 0; k < 3; k++) door[k] = at[k];
        }
    }
    return best;
}

/* Kinds a bot can drive: on the ground. The Banshee is left to people. */
static bool drivable(uint16_t kind)
{
    return kind == HTA_VK_JEEP || kind == HTA_VK_TANK || kind == HTA_VK_SCOUT;
}

/* The driver's door of `car`, in the world. */
static bool driver_door(const hta_game *g, uint32_t car, float door[3])
{
    const hta_vehicles *v = g->vehicles;
    int32_t ds = hta_vehicles_driver_seat(v, car);
    if (ds < 0) return false;
    const hta_vehicle_seat *st = hta_vehicles_seat(v, car, (uint32_t)ds);
    if (!st) return false;
    hta_transform w;
    hta_vehicles_world(v, car, &w);
    hta_xf_point(door, &w, st->enter);
    return true;
}

/* Is the wheel of `car` free for `me` to take: parked, whole enough, no
 * enemy aboard, nobody else walking to it. */
static bool wheel_free(const hta_game *g, int32_t me, uint32_t car)
{
    const hta_vehicles *v = g->vehicles;
    const hta_vehicle *c = &v->cars[car];
    if (!c->active || !drivable(c->kind)) return false;
    int32_t ds = hta_vehicles_driver_seat(v, car);
    if (ds < 0 || c->occupant[ds] >= 0) return false;
    if (hta_vehicles_speed(v, car) > HTA_VEHICLE_EXIT_SPEED) return false;
    if (hta_game_hull(g, (int32_t)car) < 0.5f) return false;
    for (uint32_t s = 0; s < HTA_VEHICLE_SEATS; s++) {
        int32_t o = c->occupant[s];
        if (o >= 0 && o < (int32_t)g->unit_count && g->teams &&
            g->units[o].team != g->units[me].team) return false;
    }
    for (uint32_t i = 0; i < g->unit_count; i++) {
        if ((int32_t)i == me || g->units[i].kind != HTA_UNIT_BOT || !g->units[i].alive) continue;
        if (g->brains[i].board == (int16_t)car) return false;
        /* Somebody got out of it jammed: nobody takes it until it has gone
         * home, which an empty car does after HTA_VEHICLE_RESPAWN. */
        if (g->brains[i].board_skip == (int16_t)car && g->brains[i].skip_time > BOARD_SKIP) return false;
    }
    return true;
}

/* The nearest empty vehicle a bot could drive. Ours, like all of this:
 * Halo CE's bots are the campaign's, and they never drove in multiplayer. */
static int32_t board_offer(const hta_game *g, int32_t me, const hta_brain *b)
{
    const hta_unit *u = &g->units[me];
    if (!g->vehicles || u->flag >= 0 || u->vehicle >= 0) return -1;
    int32_t best = -1;
    float best_d = BOARD_REACH;
    for (uint32_t c = 0; c < g->vehicles->count; c++) {
        if ((int32_t)c == b->board_skip && b->skip_time > 0.0f) continue;
        if (!wheel_free(g, me, c)) continue;
        float door[3];
        if (!driver_door(g, c, door)) continue;
        float d = hypotf(door[0] - u->body.pos[0], door[1] - u->body.pos[1]);
        if (fabsf(door[2] - u->body.pos[2]) > 3.0f) continue;
        if (d < best_d) { best_d = d; best = (int32_t)c; }
    }
    return best;
}

/* The nearest enemy in sight, all the way round; target, react and aim
 * error kept the way the gunner keeps them. */
static int32_t spot(hta_game *g, int32_t me, hta_brain *b, float dt, float *out_dist)
{
    const hta_unit *u = &g->units[me];
    const uint8_t sk = b->skill;
    int32_t seen = -1;
    float seen_dist = 1e9f;
    for (uint32_t i = 0; i < g->unit_count; i++) {
        const hta_unit *o = &g->units[i];
        if ((int32_t)i == me || !o->alive || o->kind == HTA_UNIT_NONE) continue;
        if (g->teams && o->team == u->team) continue;
        if (o->vehicle >= 0 && o->vehicle == u->vehicle) continue;
        float d;
        if (!sees(g, me, (int32_t)i, SIGHT_RANGE[sk], -1.0f, &d)) continue;
        if ((int32_t)i == b->target) d *= 0.6f;
        if (d < seen_dist) { seen_dist = d; seen = (int32_t)i; }
    }
    if (seen != b->target && seen >= 0) {
        b->react = REACT_TIME[sk] * (0.7f + 0.6f * frand(&b->rng));
        b->aim_err[0] = (frand(&b->rng) - 0.5f) * AIM_ERROR[sk] * 4.0f;
        b->aim_err[1] = (frand(&b->rng) - 0.5f) * AIM_ERROR[sk] * 2.0f;
    }
    if (seen >= 0) {
        b->seen_ago = 0.0f;
        hta_game_centre(g, seen, b->seen_pos);
    } else {
        b->seen_ago += dt;
    }
    b->target = seen;
    b->visible = seen >= 0;
    *out_dist = seen_dist;
    return seen;
}

/* On a gun: pick an enemy, swing onto it, fire; get off when the driver
 * does. */
static void ride(struct hta_game *g, int32_t me, hta_brain *b, float dt)
{
    hta_unit *u = &g->units[me];
    hta_unit_input *in = &u->in;
    memset(&in->move, 0, sizeof(in->move));
    in->fire2 = false;
    const uint8_t sk = b->skill;
    const hta_vehicles *v = g->vehicles;
    const hta_vehicle *car = &v->cars[u->vehicle];
    const hta_vehicle_seat *st = hta_vehicles_seat(v, (uint32_t)u->vehicle, (uint32_t)u->seat);
    int32_t ds = hta_vehicles_driver_seat(v, (uint32_t)u->vehicle);
    bool gunner = st && (st->flags & HTA_SEAT_GUNNER) && !(st->flags & HTA_SEAT_DRIVER);
    /* Alone on it, or not on the gun: off. */
    if (!gunner || ds < 0 || car->occupant[ds] < 0) {
        b->ride_alone += dt;
        if (!gunner || b->ride_alone > RIDE_ALONE) in->action = true;
        return;
    }
    b->ride_alone = 0.0f;
    int32_t seen = -1;
    float seen_dist = 1e9f;
    for (uint32_t i = 0; i < g->unit_count; i++) {
        const hta_unit *o = &g->units[i];
        if ((int32_t)i == me || !o->alive || o->kind == HTA_UNIT_NONE) continue;
        if (g->teams && o->team == u->team) continue;
        float d;
        /* The gun turns all the way round: no field of view. */
        if (!sees(g, me, (int32_t)i, SIGHT_RANGE[sk], -1.0f, &d)) continue;
        if ((int32_t)i == b->target) d *= 0.6f;
        if (d < seen_dist) { seen_dist = d; seen = (int32_t)i; }
    }
    if (seen != b->target && seen >= 0) {
        b->react = REACT_TIME[sk] * (0.7f + 0.6f * frand(&b->rng));
        b->aim_err[0] = (frand(&b->rng) - 0.5f) * AIM_ERROR[sk] * 4.0f;
        b->aim_err[1] = (frand(&b->rng) - 0.5f) * AIM_ERROR[sk] * 2.0f;
    }
    b->target = seen;
    b->visible = seen >= 0;
    float want_yaw = car->yaw, want_pitch = 0.0f;
    bool shoot = false;
    if (seen >= 0) {
        float aim[3];
        hta_game_centre(g, seen, aim);
        int32_t wi = car->type < HTA_VEHICLE_TYPES ? g->vweapon[car->type][0] : -1;
        const hta_game_weapon *w = wi >= 0 ? &g->weapons[wi] : NULL;
        if (w && w->travels && w->speed > 1.0f)
            for (int k = 0; k < 2; k++) aim[k] += g->units[seen].body.velocity[k] * seen_dist / w->speed;
        feet_for_blast(g, seen, w, aim);
        /* Aim from the camera: a vehicle gun fires along its line. */
        float d[3] = { aim[0]-u->eye.pos[0], aim[1]-u->eye.pos[1], aim[2]-u->eye.pos[2] };
        float flat = hypotf(d[0], d[1]);
        float settle = expf(-AIM_SETTLE[sk] * dt);
        b->aim_err[0] *= settle; b->aim_err[1] *= settle;
        want_yaw = atan2f(d[1], d[0]) + b->aim_err[0];
        want_pitch = atan2f(d[2], flat) + b->aim_err[1];
        if (b->react > 0.0f) b->react -= dt;
        float off = fabsf(wrap(want_yaw - u->eye.yaw)) + fabsf(want_pitch - u->eye.pitch);
        if (b->react <= 0.0f && off < 0.12f && seen_dist < GUN_RANGE) shoot = true;
    }
    float dyaw = wrap(want_yaw - u->eye.yaw), dpitch = want_pitch - u->eye.pitch;
    float turn = TURN_RATE[sk] * dt;
    if (dyaw > turn) dyaw = turn;
    if (dyaw < -turn) dyaw = -turn;
    if (dpitch > turn) dpitch = turn;
    if (dpitch < -turn) dpitch = -turn;
    in->move.look_yaw = dyaw;
    in->move.look_pitch = dpitch;
    in->move.fire = shoot;
}

/* Turn the thumbs toward a world yaw/pitch at the skill's rate. */
static void look_toward(hta_unit *u, const hta_brain *b, float want_yaw, float want_pitch, float dt)
{
    float dyaw = wrap(want_yaw - u->eye.yaw), dpitch = want_pitch - u->eye.pitch;
    float turn = TURN_RATE[b->skill] * dt;
    if (dyaw > turn) dyaw = turn;
    if (dyaw < -turn) dyaw = -turn;
    if (dpitch > turn) dpitch = turn;
    if (dpitch < -turn) dpitch = -turn;
    u->in.move.look_yaw = dyaw;
    u->in.move.look_pitch = dpitch;
}

/* The next point to steer at: along the nav path when there is one (a
 * corner is passed a car's length early), else straight at `goal`. */
static void steer_point(hta_game *g, int32_t me, hta_brain *b, const float goal[3], float out[3])
{
    const hta_unit *u = &g->units[me];
    while (g->nav && b->path_i < b->path_len) {
        float w[3];
        hta_nav_pos(g->nav, b->path[b->path_i], w);
        if (hypotf(w[0] - u->body.pos[0], w[1] - u->body.pos[1]) < DRIVE_REACH &&
            b->path_i + 1 < b->path_len) { b->path_i++; continue; }
        for (int k = 0; k < 3; k++) out[k] = w[k];
        return;
    }
    for (int k = 0; k < 3; k++) out[k] = goal[k];
}

/* Steer round another vehicle in the way: the grid is built without
 * them, so a path runs straight through the cars parked at a base. The
 * nearest car ahead, short of the goal and within reach of the bodies,
 * is passed on its nearer side. A car the target sits in is rammed. */
static void around_cars(const hta_game *g, uint32_t ci, int32_t target,
                        float dx, float dy, float *out_x, float *out_y)
{
    *out_x = dx; *out_y = dy;
    const hta_vehicles *v = g->vehicles;
    const hta_vehicle *me = &v->cars[ci];
    float dist = hypotf(dx, dy);
    if (dist < 1e-3f) return;
    float fx = dx / dist, fy = dy / dist;
    int32_t block = -1;
    float block_along = 0.0f, block_side = 0.0f, block_need = 0.0f;
    for (uint32_t j = 0; j < v->count; j++) {
        const hta_vehicle *o = &v->cars[j];
        if (j == ci || !o->active) continue;
        if (target >= 0 && g->units[target].vehicle == (int32_t)j) continue;
        float rx = o->pos[0] - me->pos[0], ry = o->pos[1] - me->pos[1];
        if (fabsf(o->pos[2] - me->pos[2]) > 3.0f) continue;
        float along = rx * fx + ry * fy;
        if (along <= 0.0f || along > AVOID_AHEAD || along > dist) continue;
        float side = fx * ry - fy * rx;     /* > 0: it is to the left */
        float need = me->body_radius + o->body_radius + AVOID_GAP;
        if (fabsf(side) >= need) continue;
        if (block < 0 || along < block_along) {
            block = (int32_t)j; block_along = along; block_side = side; block_need = need;
        }
    }
    if (block < 0) return;
    /* A point beside it, away from the side it is on. */
    float away = block_side > 0.0f ? -1.0f : 1.0f;
    const hta_vehicle *o = &v->cars[block];
    *out_x = o->pos[0] + (-fy) * away * block_need - me->pos[0];
    *out_y = o->pos[1] + fx * away * block_need - me->pos[1];
}

/* At the wheel. Ours: a Warthog runs people down, or circles while a
 * gunner works; a Ghost closes and strafes; a Scorpion shells from range.
 * Anywhere else it drives the nav path to the flag, the last sighting, or
 * somewhere far off. It backs out of jams, and walks when the car is
 * nearly gone, when it keeps jamming, or near a flag. */
static void drive(struct hta_game *g, int32_t me, hta_brain *b, float dt)
{
    hta_unit *u = &g->units[me];
    hta_unit_input *in = &u->in;
    memset(&in->move, 0, sizeof(in->move));
    in->fire2 = false;
    const uint8_t sk = b->skill;
    hta_vehicles *v = g->vehicles;
    uint32_t ci = (uint32_t)u->vehicle;
    const hta_vehicle *car = &v->cars[ci];
    float speed = hta_vehicles_speed(v, ci);
    int32_t gs = hta_vehicles_gunner_seat(v, ci);
    const hta_vehicle_seat *st = hta_vehicles_seat(v, ci, (uint32_t)u->seat);
    bool shoots = st && (st->flags & HTA_SEAT_GUNNER);
    b->board = -1;
    const uint8_t clear = hta_nav_car_clear(v->types[car->type].coll_radius);

    float objective[3];
    bool has_objective = hta_game_ctf_goal(g, me, objective, NULL);
    if (!drivable(car->kind) || hta_game_hull(g, (int32_t)ci) < DISMOUNT_HULL ||
        b->stuck_count >= STUCK_LIMIT ||
        (has_objective && hypotf(objective[0] - u->body.pos[0],
                                 objective[1] - u->body.pos[1]) < DISMOUNT_NEAR))
        b->dismount = true;
    if (b->dismount) {
        if (b->stuck_count >= STUCK_LIMIT) { b->board_skip = (int16_t)ci; b->skip_time = JAM_SKIP; }
        in->move.jump = true;              /* the brake */
        if (speed <= HTA_VEHICLE_EXIT_SPEED) in->action = true;
        return;
    }

    float seen_dist;
    int32_t seen = spot(g, me, b, dt, &seen_dist);

    /* A Warthog in a team game waits a moment for a teammate on the gun. */
    if (car->kind == HTA_VK_JEEP && g->teams && gs >= 0 && car->occupant[gs] < 0 &&
        b->wait_gunner < WAIT_GUNNER && (seen < 0 || seen_dist > BOARD_FIGHT)) {
        bool mate = false;
        for (uint32_t i = 0; i < g->unit_count && !mate; i++) {
            const hta_unit *o = &g->units[i];
            if ((int32_t)i == me || o->kind != HTA_UNIT_BOT || !o->alive || o->vehicle >= 0 ||
                o->team != u->team || o->flag >= 0) continue;
            if (hypotf(o->body.pos[0] - u->body.pos[0], o->body.pos[1] - u->body.pos[1]) < RIDE_REACH * 1.5f)
                mate = true;
        }
        if (mate) {
            b->wait_gunner += dt;
            in->move.jump = true;
            return;
        }
    }
    if (gs >= 0 && car->occupant[gs] >= 0) b->wait_gunner = WAIT_GUNNER;

    /* ---- where to ---- */
    float goal[3] = { car->pos[0], car->pos[1], car->pos[2] };
    bool go = false, hold = false;
    const float *here = car->pos;
    if (seen >= 0) {
        float at[3];
        hta_game_centre(g, seen, at);
        const hta_unit *t = &g->units[seen];
        bool manned_gun = gs >= 0 && gs != u->seat && car->occupant[gs] >= 0;
        if (car->kind == HTA_VK_JEEP && manned_gun) {
            /* Circle him: the gunner does the work. */
            float d[2] = { here[0] - at[0], here[1] - at[1] };
            float dl = hypotf(d[0], d[1]);
            if (dl < 1e-3f) { d[0] = 1; d[1] = 0; dl = 1; }
            d[0] /= dl; d[1] /= dl;
            if (b->strafe == 0.0f) b->strafe = 1.0f;
            float side[2] = { -d[1] * b->strafe, d[0] * b->strafe };
            goal[0] = at[0] + d[0] * ORBIT + side[0] * 10.0f;
            goal[1] = at[1] + d[1] * ORBIT + side[1] * 10.0f;
            goal[2] = at[2];
        } else if (car->kind == HTA_VK_JEEP) {
            /* Run him down, leading him. */
            float tof = speed > 1.0f ? seen_dist / speed : 0.0f;
            if (tof > 1.5f) tof = 1.5f;
            goal[0] = at[0] + t->body.velocity[0] * tof;
            goal[1] = at[1] + t->body.velocity[1] * tof;
            goal[2] = at[2];
        } else {
            float range = car->kind == HTA_VK_TANK ? TANK_RANGE : GHOST_RANGE;
            for (int k = 0; k < 3; k++) goal[k] = at[k];
            hold = seen_dist < range;
        }
        b->roaming = false;
    }
    {
        const float *to = NULL;
        float fight[3];
        if (seen >= 0) {
            for (int k = 0; k < 3; k++) fight[k] = goal[k];
            to = fight;
        } else if (has_objective) to = objective;
        else if (b->target >= 0 && b->seen_ago < THINK_LOST) to = b->seen_pos;
        else {
            if (!b->roaming || hypotf(b->roam[0] - here[0], b->roam[1] - here[1]) < DRIVE_ARRIVE) {
                b->roaming = false;
                if (g->nav && g->nav->built) {
                    for (int tries = 0; tries < 24 && !b->roaming; tries++) {
                        uint32_t r = hta_nav_random_wide(g->nav, &b->rng, clear);
                        if (r == HTA_NAV_NONE) break;
                        hta_nav_pos(g->nav, r, b->roam);
                        /* Far enough to be going somewhere, near enough
                         * for the path search to find. */
                        float far = hypotf(b->roam[0] - here[0], b->roam[1] - here[1]);
                        b->roaming = far > ROAM_MIN && far < ROAM_MAX;
                    }
                    if (b->roaming) b->replan = 0.0f;
                }
            }
            if (b->roaming) to = b->roam;
        }
        /* Straight at a fight across open ground; round whatever is in
         * the way. */
        if (to == fight && open_line(g, here, fight, clear)) {
            b->path_len = 0;
            b->replan = 0.0f;       /* the moment it is not, a path */
            go = true;
        } else if (to) {
            float moved = hypotf(to[0] - b->goal_pos[0], to[1] - b->goal_pos[1]);
            b->replan -= dt;
            if (b->replan <= 0.0f || moved > 3.0f) {
                plan_drive(g, me, b, to, clear);
                for (int k = 0; k < 3; k++) b->goal_pos[k] = to[k];
                /* No way there: straight at it, and ask again soon. */
                b->replan = b->path_len ? REPLAN * 2.0f : REPLAN * 0.3f;
            }
            float p[3];
            steer_point(g, me, b, to, p);
            for (int k = 0; k < 3; k++) goal[k] = p[k];
            go = true;
        }
    }

    /* ---- the wheel ---- */
    float dx = goal[0] - here[0], dy = goal[1] - here[1];
    if (go) around_cars(g, ci, seen, dx, dy, &dx, &dy);
    float heading = hypotf(dx, dy) > 1e-3f ? atan2f(dy, dx) : car->yaw;
    float err = wrap(heading - car->yaw);
    float gas = 0.0f, steer = 0.0f;
    if (go && !hold) {
        switch (car->kind) {
        case HTA_VK_JEEP:
            /* Stick right turns right (clockwise, yaw falling). */
            steer = -err * 2.5f;
            gas = fabsf(err) < 1.2f ? 1.0f : 0.6f;
            /* Behind it: back round, the wheel the other way. */
            if (fabsf(err) > BEHIND) { gas = -0.8f; steer = err > 0.0f ? 1.0f : -1.0f; }
            break;
        case HTA_VK_TANK:
            steer = -err * 2.0f;
            gas = fabsf(err) < 0.5f ? 1.0f : (fabsf(err) < 1.2f ? 0.4f : 0.0f);
            break;
        default:
            gas = 1.0f;
            break;
        }
    } else if (hold && car->kind == HTA_VK_TANK) {
        steer = -err * 2.0f;   /* square the hull up, the turret does the rest */
    }
    if (steer > 1.0f) steer = 1.0f;
    if (steer < -1.0f) steer = -1.0f;

    /* Jammed: back out, wheel the other way. */
    if (b->jam_car != (int16_t)ci) {
        b->jam_car = (int16_t)ci;
        b->last_blocked = car->blocked;
    }
    b->stuck_timer += dt;
    if (b->stuck_timer > 1.0f) {
        float moved = hypotf(here[0] - b->last_pos[0], here[1] - b->last_pos[1]);
        /* Held by the physics for a good part of it, not just slow: a car
         * backing round, or still rolling back out of the last jam, moves
         * little and is not stuck. A tank turning on the spot against a
         * post is. */
        bool held = car->blocked - b->last_blocked > JAM_HELD;
        if (!b->reversing && (fabsf(gas) > 0.3f || fabsf(steer) > 0.3f) && moved < 0.5f && held) {
            b->reversing = true;
            b->reverse_timer = 1.2f;
            b->stuck_count++;
            b->path_len = 0;
            b->replan = 0.0f;
        } else if (moved > 3.0f) b->stuck_count = 0;
        b->stuck_timer = 0.0f;
        b->last_blocked = car->blocked;
        for (int k = 0; k < 3; k++) b->last_pos[k] = here[k];
    }
    if (b->reversing) {
        b->reverse_timer -= dt;
        if (b->reverse_timer <= 0.0f) {
            /* The next second is judged from here: one that began while
             * backing out sees the car return to where it was and calls
             * that a jam, which backs it out again, and again. */
            b->reversing = false;
            b->stuck_timer = 0.0f;
            b->last_blocked = car->blocked;
            for (int k = 0; k < 3; k++) b->last_pos[k] = here[k];
        }
        /* Whichever way it was pushing, the other. */
        gas = gas < 0.0f ? 1.0f : -1.0f;
        steer = car->kind == HTA_VK_TANK ? steer : -steer;
        if (fabsf(steer) < 0.3f) steer = 1.0f;
    }

    /* ---- the eyes and the gun ---- */
    float want_yaw = heading, want_pitch = 0.0f;
    bool shoot = false;
    if (seen >= 0 && shoots) {
        float aim[3];
        hta_game_centre(g, seen, aim);
        int32_t wi = car->type < HTA_VEHICLE_TYPES ? g->vweapon[car->type][0] : -1;
        const hta_game_weapon *w = wi >= 0 ? &g->weapons[wi] : NULL;
        if (w && w->travels && w->speed > 1.0f)
            for (int k = 0; k < 2; k++) aim[k] += g->units[seen].body.velocity[k] * seen_dist / w->speed;
        feet_for_blast(g, seen, w, aim);
        /* Aim from the camera: a vehicle gun fires along its line. */
        float d[3] = { aim[0]-u->eye.pos[0], aim[1]-u->eye.pos[1], aim[2]-u->eye.pos[2] };
        float settle = expf(-AIM_SETTLE[sk] * dt);
        b->aim_err[0] *= settle; b->aim_err[1] *= settle;
        want_yaw = atan2f(d[1], d[0]) + b->aim_err[0];
        want_pitch = atan2f(d[2], hypotf(d[0], d[1])) + b->aim_err[1];
        if (b->react > 0.0f) b->react -= dt;
        float off = fabsf(wrap(want_yaw - u->eye.yaw)) + fabsf(want_pitch - u->eye.pitch);
        float range = car->kind == HTA_VK_TANK ? GUN_RANGE * 1.5f : GUN_RANGE;
        /* A shell is one shot every few seconds: lay it properly. */
        float tol = w && w->def.single_shot ? 0.03f + 0.3f / fmaxf(seen_dist, 1.0f) : 0.12f;
        if (b->react <= 0.0f && off < tol && seen_dist < range) shoot = true;
        /* A cannon fires once a pull. */
        if (w && w->def.single_shot && b->trigger) shoot = false;
    }
    b->trigger = shoot;
    look_toward(u, b, want_yaw, want_pitch, dt);

    if (car->kind == HTA_VK_SCOUT) {
        /* The Ghost turns to the look; the stick moves it relative to that.
         * Toward the goal, or across the target's front when in range. */
        float yaw = u->eye.yaw + in->move.look_yaw;
        float f[2] = { cosf(yaw), sinf(yaw) }, r[2] = { f[1], -f[0] };
        float mv[2] = { 0.0f, 0.0f };
        if (go && !hold) { mv[0] = cosf(heading); mv[1] = sinf(heading); }
        if (hold || (seen >= 0 && seen_dist < GHOST_RANGE * 1.5f)) {
            b->strafe_timer -= dt;
            if (b->strafe_timer <= 0.0f) {
                b->strafe = frand(&b->rng) < 0.5f ? -1.0f : 1.0f;
                b->strafe_timer = 0.8f + frand(&b->rng) * 1.2f;
            }
            /* Only toward open ground: the other way if that side is a
             * rock, and not at all if both are. */
            if (!side_open(g, here, r, b->strafe, clear)) {
                b->strafe = -b->strafe;
                if (!side_open(g, here, r, b->strafe, clear)) b->strafe_timer = 0.0f;
            }
            if (side_open(g, here, r, b->strafe, clear)) {
                mv[0] += r[0] * b->strafe; mv[1] += r[1] * b->strafe;
            }
        }
        gas = mv[0]*f[0] + mv[1]*f[1];
        steer = mv[0]*r[0] + mv[1]*r[1];
        if (b->reversing) { gas = -1.0f; steer = 0.0f; }
    }
    in->move.move_forward = gas;
    in->move.move_right = steer;
    in->move.fire = shoot;
}

void hta_brain_think(struct hta_game *g, int32_t me, hta_brain *b, float dt)
{
    if (b->skip_time > 0.0f) b->skip_time -= dt;
    if (b->plan_wait > 0.0f) b->plan_wait -= dt;
    if (g->units[me].vehicle >= 0 && g->vehicles) {
        const hta_vehicle_seat *st = hta_vehicles_seat(g->vehicles, (uint32_t)g->units[me].vehicle,
                                                       (uint32_t)g->units[me].seat);
        if (st && (st->flags & HTA_SEAT_DRIVER)) drive(g, me, b, dt);
        else ride(g, me, b, dt);
        return;
    }
    b->dismount = b->reversing = false;
    b->stuck_count = 0;
    b->wait_gunner = 0.0f;
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
            feet_for_blast(g, b->target, w, aim);
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
    /* The game's objective, if it has one: a flag to take, carry or win
     * back. A carrier does not stop to duel; it runs. */
    float objective[3];
    int stand = -1;
    bool has_objective = hta_game_ctf_goal(g, me, objective, &stand);
    bool fighting = b->target >= 0 && b->visible && u->flag < 0;
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
        float margin=w && w->melee_only ? .2f : 1.5f;
        float fwd = close > margin ? 1.0f : (close < -margin ? -0.7f : 0.0f);
        move[0] = to[0] * fwd + side[0] * b->strafe;
        move[1] = to[1] * fwd + side[1] * b->strafe;
        /* With a flag to take or take back, keep going and shoot on the
         * way; only a fight at arm's length is worth stopping for. Ours. */
        float dir[2];
        if (has_objective && seen_dist > PUSH_STOP) {
            if (!b->goal_game || b->replan <= 0.0f || b->path_i >= b->path_len) {
                if (stand >= 0 && g->stand_field[stand]) plan_field(g, me, b, stand);
                else plan_to(g, me, b, objective);
                for (int k = 0; k < 3; k++) b->goal_pos[k] = objective[k];
                b->goal_game = true;
                b->replan = REPLAN;
            }
            if (g->nav && follow(g, me, b, dir)) {
                move[0] = dir[0] + side[0] * b->strafe * 0.4f;
                move[1] = dir[1] + side[1] * b->strafe * 0.4f;
            }
        }
        /* A jump now and then, the way people dodge. */
        if (sk >= 1 && u->body.on_ground && frand(&b->rng) < 0.25f * dt * (float)sk)
            in->move.jump = true;
        if (!has_objective || seen_dist <= PUSH_STOP) b->path_len = 0;
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
        if (has_objective && (b->target < 0 || u->flag >= 0)) {
            /* Replan when the objective has moved -- a carrier running --
             * or the old path is spent. */
            float moved = hypotf(objective[0] - b->goal_pos[0], objective[1] - b->goal_pos[1]);
            if (!b->goal_game || b->replan <= 0.0f || moved > 1.0f ||
                (b->path_i >= b->path_len && moved > 0.2f)) {
                if (stand >= 0 && g->stand_field[stand]) plan_field(g, me, b, stand);
                else plan_to(g, me, b, objective);
                for (int k = 0; k < 3; k++) b->goal_pos[k] = objective[k];
                b->goal_game = true;
                b->goal_item = -1;
                b->replan = REPLAN;
            }
            /* The last step onto a flag is not always on the grid. */
            if (b->path_i >= b->path_len) {
                float dx = objective[0] - u->body.pos[0], dy = objective[1] - u->body.pos[1];
                float dl = hypotf(dx, dy);
                if (dl > 0.05f && dl < 2.0f) { move[0] = dx / dl; move[1] = dy / dl; }
            }
        } else if (b->target >= 0) {
            b->goal_game = false;
            /* Lost sight: go to where he was. */
            if (b->replan <= 0.0f || !b->path_len) {
                plan_to(g, me, b, b->seen_pos);
                b->replan = REPLAN;
            }
        } else {
            b->goal_game = false;
            /* An item the grid reaches but the body cannot -- on a ledge an
             * imported map's clip brushes would have kept it off, say -- is
             * given up for a while instead of stood under forever. Ours:
             * 1.5 s at the end of the path, 25 s in all, 30 s off. */
            if (b->item_skip_time > 0.0f && (b->item_skip_time -= dt) <= 0.0f) b->item_skip = -1;
            if (b->goal_item >= 0) {
                b->item_time += dt;
                b->item_there = b->path_i >= b->path_len ? b->item_there + dt : 0.0f;
                if (b->item_time >= 25.0f || b->item_there >= 1.5f) {
                    b->item_skip = b->goal_item;
                    b->item_skip_time = 30.0f;
                    b->goal_item = -1;
                    b->path_len = b->path_i = 0;
                }
            }
            int32_t item = pick_item(g, me, b->item_skip);
            if (item != b->goal_item) b->item_time = b->item_there = 0.0f;
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
            /* A carrier under fire cannot shoot back: it weaves and jumps
             * the way a person running a flag does. */
            if (u->flag >= 0 && b->target >= 0 && b->visible) {
                b->strafe_timer -= dt;
                if (b->strafe_timer <= 0.0f) {
                    b->strafe = frand(&b->rng) < 0.5f ? -1.0f : 1.0f;
                    b->strafe_timer = 0.3f + frand(&b->rng) * 0.6f;
                }
                move[0] += dir[1] * b->strafe * 0.7f;
                move[1] -= dir[0] * b->strafe * 0.7f;
                if (u->body.on_ground && frand(&b->rng) < 0.6f * dt) in->move.jump = true;
            }
        }
    }
    /* Standing on a weapon it wants more than what it holds: take it --
     * the map's, or one somebody dropped. */
    {
        int32_t dr = hta_game_drop_near(g, u->body.pos, HTA_DROP_REACH);
        if (dr >= 0 && hta_game_can_equip(g, me, g->drops[dr].weapon) &&
            want(&g->weapons[g->drops[dr].weapon]) > want(w)) in->pickup = true;
    }
    if (g->items) {
        int32_t ws = hta_pickups_at_kind(g->items, u->body.pos, HTA_ITEM_WEAPON);
        const hta_item_choice *c = hta_pickups_item(g->items, ws);
        if (c) {
            int32_t wi = hta_game_weapon_index(g, c->tag_id);
            if (hta_game_can_equip(g, me, wi) && want(&g->weapons[wi]) > want(w)) in->pickup = true;
        }
    }

    /* A teammate at the wheel with the gun free: climb on. Ours -- Halo CE
     * has no multiplayer bots. */
    bool riding = false;
    {
        int32_t seat = -1;
        float door[3];
        int32_t car = ride_offer(g, me, &seat, door);
        riding = car >= 0;
        if (car >= 0 && !(fighting && seen_dist < PUSH_STOP)) {
            float dx = door[0] - u->body.pos[0], dy = door[1] - u->body.pos[1];
            float dl = hypotf(dx, dy);
            if (dl > 0.05f) { move[0] = dx / dl; move[1] = dy / dl; }
            if (!fighting) want_yaw = atan2f(dy, dx);
            int32_t near_seat = -1;
            if (hta_game_seat_near(g, me, &near_seat) == car && near_seat == seat)
                in->action = true;
        }
    }

    /* An empty vehicle near by, nobody close enough to fight on foot, and
     * no flag just ahead: take the wheel. */
    if (!riding && !in->action && g->vehicles && u->flag < 0 && !(fighting && seen_dist < BOARD_FIGHT)) {
        bool near_goal = has_objective &&
            hypotf(objective[0] - u->body.pos[0], objective[1] - u->body.pos[1]) < BOARD_OBJECTIVE;
        if (b->board >= 0 && (!wheel_free(g, me, (uint32_t)b->board) || near_goal)) b->board = -1;
        if (b->board >= 0) {
            b->board_time += dt;
            if (b->board_time > BOARD_GIVE_UP) {
                b->board_skip = b->board;
                b->skip_time = BOARD_SKIP;
                b->board = -1;
            }
        } else if (!near_goal) {
            int32_t car = board_offer(g, me, b);
            if (car >= 0) { b->board = (int16_t)car; b->board_time = 0.0f; }
        }
        float door[3];
        if (b->board >= 0 && driver_door(g, (uint32_t)b->board, door) &&
            hypotf(door[0] - u->body.pos[0], door[1] - u->body.pos[1]) > BOARD_REACH * 1.5f)
            b->board = -1;      /* driven off, or it was carried away */
        if (b->board >= 0 && driver_door(g, (uint32_t)b->board, door)) {
            float dx = door[0] - u->body.pos[0], dy = door[1] - u->body.pos[1];
            float dl = hypotf(dx, dy);
            if (dl > 0.05f) { move[0] = dx / dl; move[1] = dy / dl; }
            if (!fighting) { want_yaw = atan2f(dy, dx); want_pitch = 0.0f; }
            int32_t near_seat = -1;
            if (hta_game_seat_near(g, me, &near_seat) == b->board &&
                near_seat == hta_vehicles_driver_seat(g->vehicles, (uint32_t)b->board))
                in->action = true;
        }
    } else b->board = -1;

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
