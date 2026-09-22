#include "game.h"
#include "../asset/effect.h"
#include "../asset/anim.h"
#include "../asset/items.h"
#include "../asset/strings.h"
#include "../asset/bsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265f

/* Weapon (inherits Item, Object): offsets from Invader's weapon.json, which
 * reconciles at 1288. */
#define WEAP_LABEL          780u
#define WEAP_MELEE_DAMAGE   916u
#define WEAP_AUTOAIM_ANGLE  996u
#define WEAP_AUTOAIM_RANGE 1000u
#define WEAP_MAGNET_ANGLE  1004u
#define WEAP_MAGNET_RANGE  1008u
#define OBJ_MODEL            40u

/* How long a swing and a throw take a bot or a remote player. The cyborg's
 * `stand rifle ar melee` is 36 frames and `throw-grenade` 38 -- about 1.2 s
 * -- but a swing lands early in the clip, and so does the release. Ours. */
#define UNIT_SWING_TIME   0.6f
#define UNIT_THROW_TIME   0.45f
/* The overshield: the platform's HTA_OVERSHIELD_MULT, which is invented. */
#define GAME_OVERSHIELD   3.0f

static void emit(hta_game *g, const hta_game_event *e)
{
    if (g->event_count >= HTA_GAME_MAX_EVENTS) {
        /* Drop the oldest rather than the newest: a kill that just
         * happened matters more than a shot from a moment ago. */
        memmove(&g->events[0], &g->events[1],
                (HTA_GAME_MAX_EVENTS - 1u) * sizeof(g->events[0]));
        g->event_count--;
    }
    g->events[g->event_count++] = *e;
}

static uint32_t rnd(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static float frand(uint32_t *s) { return (float)(rnd(s) & 0xFFFFu) / 65535.0f; }

static const char *text(const hta_game *g, uint32_t index, char *buf, size_t n,
                        const char *fallback)
{
    if (!g->text_tag || !hta_ustr_get(g->cache, g->text_tag, index, buf, n) || !buf[0])
        snprintf(buf, n, "%s", fallback);
    return buf;
}

/* ---------------------------------------------------------------- load */

static void read_label(const hta_cache *c, uint32_t base, char out[8])
{
    char lab[33];
    memset(lab, 0, sizeof(lab));
    hta_rd_bytes(c, base + WEAP_LABEL, lab, 32);
    lab[7] = 0;
    memcpy(out, lab, 8);
}

/* Which stance word the cyborg's graph files this weapon's label under:
 * "stand rifle ar fire-1" says the AR is a rifle. The flamethrower and the
 * plasma cannon have no clips of their own and borrow the z-stances. */
static void anim_class(const hta_anim_graph *gr, const char *label,
                       char out[12], bool *z)
{
    *z = false;
    snprintf(out, 12, "rifle");
    if (!label[0]) return;
    char needle[16];
    snprintf(needle, sizeof(needle), " %s ", label);
    for (uint32_t i = 0; gr && i < gr->anim_count; i++) {
        const char *n = gr->anims[i].name;
        if (strncmp(n, "stand ", 6) != 0 || !strstr(n + 5, needle)) continue;
        const char *w = n + 6;
        const char *sp = strchr(w, ' ');
        if (!sp || sp - w >= 12) continue;
        memcpy(out, w, (size_t)(sp - w));
        out[sp - w] = 0;
        return;
    }
    if (!strcmp(label, "ft")) { snprintf(out, 12, "flame"); *z = true; }
    else if (!strcmp(label, "pc")) { snprintf(out, 12, "cannon"); *z = true; }
    else if (!strcmp(label, "hp") || !strcmp(label, "pp") ||
             !strcmp(label, "ne") || !strcmp(label, "b"))
        snprintf(out, 12, "pistol");
    else if (!strcmp(label, "rl")) snprintf(out, 12, "missile");
}

static int32_t pool_for(hta_game *g, uint32_t proj, const hta_resource_map *bm)
{
    for (uint32_t i = 0; i < g->pool_count; i++)
        if (g->pools[i].proj_tag_id == proj) return (int32_t)i;
    if (g->pool_count >= HTA_GAME_MAX_POOLS) return -1;
    hta_projectiles *p = &g->pools[g->pool_count];
    hta_projectiles_init(p);
    char err[HTA_ERRLEN];
    if (!hta_projectiles_equip_projectile(p, g->cache, bm, proj, err, sizeof(err)))
        return -1;
    g->pool_weapon[g->pool_count] = -1;
    for (int k = 0; k < HTA_PROJ_MAX; k++) g->pool_owner[g->pool_count][k] = -1;
    return (int32_t)g->pool_count++;
}

bool hta_game_load(hta_game *g, const hta_cache *c, const hta_resource_map *bitmaps,
                   const hta_collision *col, char *err, size_t errlen)
{
    if (!g || !c || !col) {
        if (err) snprintf(err, errlen, "bad arguments");
        return false;
    }
    memset(g, 0, sizeof(*g));
    g->cache = c;
    g->col = col;
    g->local = HTA_GAME_NONE;
    g->winner = HTA_GAME_NONE;
    g->leader = HTA_GAME_NONE;
    g->grenade_pool = -1;
    g->simulate_vehicles = true;
    for (uint32_t t = 0; t < HTA_VEHICLE_TYPES; t++) g->vweapon[t][0] = g->vweapon[t][1] = -1;
    g->score_limit = HTA_SLAYER_SCORE_LIMIT;
    g->respawn_time = HTA_SLAYER_RESPAWN;
    g->rng = 0xC0FFEEu;

    char perr[HTA_ERRLEN];
    hta_player_physics_defaults(&g->phys);
    hta_player_physics_load(&g->phys, c, perr, sizeof(perr));
    {
        hta_player probe;
        hta_player_init(&probe);
        hta_player_apply_physics(&probe, &g->phys);
        g->gravity = probe.gravity > 0.0f ? probe.gravity : 3.57f;
    }
    if (!hta_vitals_load(&g->vitals_template, c)) {
        g->vitals_template.max_health = 75.0f;
        g->vitals_template.max_shield = 75.0f;
    }
    hta_vitals_reset(&g->vitals_template);
    g->spawn_count = hta_scenario_spawns(c, g->spawns, 64);

    /* The cyborg's graph, only to learn which stance each weapon takes. */
    hta_anim_graph graph;
    memset(&graph, 0, sizeof(graph));
    bool have_graph = false;
    for (uint32_t i = 0; i < c->tag_count && !have_graph; i++) {
        hta_tag_entry t;
        char path[256];
        if (!hta_cache_tag(c, i, &t) || t.primary_class != HTA_FOURCC('b','i','p','d')) continue;
        if (!hta_cache_tag_path(c, &t, path, sizeof(path)) || !strstr(path, "cyborg_mp")) continue;
        uint32_t base, antr = 0;
        if (hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base) &&
            hta_rd_u32(c, base + 56u + 12u, &antr) && antr && antr != 0xFFFFFFFFu)
            have_graph = hta_anim_load(&graph, c, antr, perr, sizeof(perr));
    }

    uint32_t tags[HTA_GAME_MAX_WEAPONS];
    uint32_t n = hta_weapon_list_playable(c, tags, HTA_GAME_MAX_WEAPONS);
    for (uint32_t i = 0; i < n && g->weapon_count < HTA_GAME_MAX_WEAPONS; i++) {
        hta_game_weapon *w = &g->weapons[g->weapon_count];
        memset(w, 0, sizeof(*w));
        if (!hta_weapon_load_id(c, NULL, tags[i], &w->def, NULL, perr, sizeof(perr)))
            continue;
        w->tag = tags[i];
        w->pool = -1;
        int32_t ti = hta_cache_find_tag_by_id(c, tags[i]);
        hta_tag_entry t;
        uint32_t base = 0;
        if (ti >= 0 && hta_cache_tag(c, (uint32_t)ti, &t) &&
            hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) {
            read_label(c, base, w->label);
            hta_rd_u32(c, base + WEAP_MELEE_DAMAGE + 12u, &w->melee_jpt);
            hta_rd_f32(c, base + WEAP_AUTOAIM_ANGLE, &w->autoaim_angle);
            hta_rd_f32(c, base + WEAP_AUTOAIM_RANGE, &w->autoaim_range);
            hta_rd_f32(c, base + WEAP_MAGNET_ANGLE, &w->magnet_angle);
            hta_rd_f32(c, base + WEAP_MAGNET_RANGE, &w->magnet_range);
            uint32_t m = 0;
            hta_rd_u32(c, base + OBJ_MODEL + 12u, &m);
            w->model = (m && m != 0xFFFFFFFFu) ? m : 0u;
        }
        if (w->melee_jpt == 0xFFFFFFFFu) w->melee_jpt = 0;
        w->melee_damage = hta_damage_vs(c, w->melee_jpt, HTA_MATERIAL_CYBORG_ARMOR);
        w->impact_jpt = hta_projectile_impact_damage(c, w->def.projectile_id);
        anim_class(have_graph ? &graph : NULL, w->label, w->anim_class, &w->z_prefix);

        /* Rounds that are objects fly; everything else is hitscan, as it
         * is for the local player. */
        int32_t pool = w->def.projectile_id ? pool_for(g, w->def.projectile_id, bitmaps) : -1;
        if (pool >= 0) {
            w->travels = true;
            w->pool = pool;
            g->pool_weapon[pool] = (int32_t)g->weapon_count;
            const hta_projectiles *p = &g->pools[pool];
            w->speed = p->speed_initial > 0.0f ? p->speed_initial : p->speed_final;
            w->blast_damage = p->blast_damage;
            w->blast_radius = p->blast_damage_radius;
            w->blast_core = p->blast_core;
        }
        g->weapon_count++;
    }
    if (have_graph) hta_anim_free(&graph);

    /* What everybody spawns with. */
    g->start_weapon[0] = g->start_weapon[1] = -1;
    uint32_t start[2];
    uint32_t ns = hta_scenario_starting_weapons(c, start, 2);
    for (uint32_t i = 0; i < ns; i++)
        g->start_weapon[i] = hta_game_weapon_index(g, start[i]);
    if (g->start_weapon[0] < 0 && g->weapon_count) g->start_weapon[0] = 0;

    /* The frag grenade, from `globals` +296 like the platform's own. */
    g->start_grenades = 2;
    g->max_grenades = 4;
    int32_t gi = hta_cache_find_tag_by_class(c, HTA_TAG_MATG);
    hta_tag_entry gt;
    uint32_t gb;
    if (gi >= 0 && hta_cache_tag(c, (uint32_t)gi, &gt) &&
        hta_cache_ptr_to_offset(c, gt.tag_data_ptr, &gb)) {
        uint32_t cnt = 0, p2 = 0, off = 0;
        if (hta_read_reflexive(c, gb + 296u, &cnt, &p2) && cnt &&
            hta_cache_ptr_to_offset(c, p2, &off)) {
            uint16_t mx = 0, sp = 0;
            uint32_t proj = 0;
            hta_rd_u16(c, off + 0u, &mx);
            hta_rd_u16(c, off + 2u, &sp);
            hta_rd_u32(c, off + 52u + 12u, &proj);
            if ((int16_t)mx > 0) g->max_grenades = (int16_t)mx;
            if ((int16_t)sp > 0) g->start_grenades = (int16_t)sp;
            if (proj && proj != 0xFFFFFFFFu) g->grenade_pool = pool_for(g, proj, bitmaps);
        }
    }

    g->text_tag = hta_ustr_find(c, "ui\\multiplayer_game_text");
    g->names_tag = hta_ustr_find(c, "ui\\random_player_names");
    g->loaded = g->weapon_count > 0 && g->spawn_count > 0;
    if (err && errlen)
        snprintf(err, errlen, "%u weapons, %u projectile pools, %u spawns, "
                 "grenades %d of %d%s",
                 g->weapon_count, g->pool_count, g->spawn_count,
                 g->start_grenades, g->max_grenades,
                 g->loaded ? "" : " -- NOT playable");
    return g->loaded;
}

void hta_game_free(hta_game *g)
{
    if (!g) return;
    for (uint32_t i = 0; i < g->pool_count; i++) hta_projectiles_free(&g->pools[i]);
    memset(g, 0, sizeof(*g));
    g->local = HTA_GAME_NONE;
}

int32_t hta_game_weapon_index(const hta_game *g, uint32_t tag)
{
    if (!g || !tag) return -1;
    for (uint32_t i = 0; i < g->weapon_count; i++)
        if (g->weapons[i].tag == tag) return (int32_t)i;
    /* `mp_needler` and `needler` share everything but the path. */
    return -1;
}

const hta_game_weapon *hta_game_held(const hta_game *g, int32_t u)
{
    if (!g || u < 0 || u >= (int32_t)g->unit_count) return NULL;
    const hta_unit *un = &g->units[u];
    int32_t w = un->carry[un->slot & 1u].weapon;
    return (w >= 0 && (uint32_t)w < g->weapon_count) ? &g->weapons[w] : NULL;
}

/* ---------------------------------------------------------------- units */

static void arm(hta_game *g, hta_unit *u)
{
    for (int s = 0; s < 2; s++) {
        u->carry[s].weapon = g->start_weapon[s];
        if (u->carry[s].weapon >= 0)
            hta_ammo_init(&u->carry[s].ammo, &g->weapons[u->carry[s].weapon].def);
        else
            memset(&u->carry[s].ammo, 0, sizeof(u->carry[s].ammo));
    }
    u->slot = 0;
    u->grenades = g->start_grenades;
    u->cooldown = u->error = 0.0f;
    u->swing = u->throwing = 0.0f;
}

int32_t hta_game_add(hta_game *g, hta_unit_kind kind, const char *name, uint8_t team)
{
    if (!g || kind == HTA_UNIT_NONE) return -1;
    int32_t idx = -1;
    for (uint32_t i=0;i<g->unit_count;i++)
        if (g->units[i].kind==HTA_UNIT_NONE) { idx=(int32_t)i; break; }
    if (idx<0) {
        if (g->unit_count >= HTA_GAME_MAX_UNITS) return -1;
        idx = (int32_t)g->unit_count++;
    }
    hta_unit *u = &g->units[idx];
    memset(u, 0, sizeof(*u));
    u->kind = kind;
    u->team = team;
    u->vehicle = -1;
    u->seat = -1;
    u->rng = 0x9E3779B9u * (uint32_t)(idx + 1);
    u->last_attacker = HTA_GAME_NONE;
    for (int k = 0; k < HTA_GAME_MAX_UNITS; k++) u->attackers[k] = 0;
    if (name && name[0]) {
        snprintf(u->name, sizeof(u->name), "%s", name);
    } else {
        /* The Trial's own names for players who did not choose one. */
        uint32_t count = hta_ustr_count(g->cache, g->names_tag);
        bool named = false;
        for (int tries = 0; count && tries < 16 && !named; tries++) {
            char nm[HTA_GAME_NAME];
            if (!hta_ustr_get(g->cache, g->names_tag, rnd(&g->rng) % count, nm, sizeof(nm)))
                continue;
            bool taken = false;
            for (uint32_t k = 0; k < g->unit_count; k++)
                if ((int32_t)k != idx && !strcmp(g->units[k].name, nm)) taken = true;
            if (!taken) { snprintf(u->name, sizeof(u->name), "%s", nm); named = true; }
        }
        if (!named) snprintf(u->name, sizeof(u->name), "Player %d", idx + 1);
    }
    hta_player_init(&u->body);
    hta_player_apply_physics(&u->body, &g->phys);
    hta_camera_init(&u->eye);
    u->vitals = g->vitals_template;
    arm(g, u);
    if (kind == HTA_UNIT_LOCAL) g->local = idx;
    if (kind == HTA_UNIT_BOT) hta_brain_init(&g->brains[idx], 1, u->rng);
    u->alive = false;
    u->respawn = 0.0f;
    return idx;
}

void hta_game_remove(hta_game *g, int32_t idx)
{
    if (!g || idx < 0 || idx >= (int32_t)g->unit_count) return;
    /* Leave the slot, empty: indices are how kills are credited. */
    hta_game_unseat(g, idx);
    g->units[idx].kind = HTA_UNIT_NONE;
    g->units[idx].alive = false;
    if (g->local == idx) g->local = HTA_GAME_NONE;
}

void hta_game_set_skill(hta_game *g, uint8_t skill)
{
    if (!g) return;
    if (skill > 3) skill = 3;
    for (uint32_t i = 0; i < g->unit_count; i++)
        if (g->units[i].kind == HTA_UNIT_BOT) g->brains[i].skill = skill;
}

/* The game ends: `winner` takes it, and everyone hears. */
static void finish(hta_game *g, int32_t winner)
{
    char buf[96];
    g->over = true;
    g->winner = winner;
    hta_game_event o = { .kind = HTA_EV_GAME_OVER, .a = winner, .b = -1,
                         .line = HTA_LINE_GAME_OVER, .for_local = true };
    snprintf(o.text, sizeof(o.text), "%s",
             text(g, winner == g->local ? 59 : 57, buf, sizeof(buf),
                  winner == g->local ? "You won" : "You lost"));
    emit(g, &o);
}

static bool enemies(const hta_game *g, int32_t a, int32_t b)
{
    if (a == b) return false;
    if (!g->teams) return true;
    return g->units[a].team != g->units[b].team;
}

void hta_game_pick_spawn(hta_game *g, int32_t idx, float out_pos[3], float *out_facing)
{
    if (!g->spawn_count) {
        out_pos[0] = out_pos[1] = out_pos[2] = 0.0f;
        if (out_facing) *out_facing = 0.0f;
        return;
    }
    /* Halo spawns you away from the people who want you dead. Score each
     * start by its nearest living enemy, and take one of the best few so
     * the same corner does not come up every time. */
    float score[64];
    for (uint32_t s = 0; s < g->spawn_count; s++) {
        float nearest = 1e9f;
        for (uint32_t k = 0; k < g->unit_count; k++) {
            const hta_unit *o = &g->units[k];
            if (!o->alive || o->kind == HTA_UNIT_NONE || (int32_t)k == idx) continue;
            if (!enemies(g, idx, (int32_t)k)) continue;
            float d = hypotf(o->body.pos[0] - g->spawns[s].position[0],
                             o->body.pos[1] - g->spawns[s].position[1]);
            if (d < nearest) nearest = d;
        }
        score[s] = nearest + frand(&g->rng) * 12.0f;
    }
    uint32_t best = 0;
    for (uint32_t s = 1; s < g->spawn_count; s++) if (score[s] > score[best]) best = s;
    const hta_spawn_point *sp = &g->spawns[best];
    out_pos[0] = sp->position[0];
    out_pos[1] = sp->position[1];
    out_pos[2] = sp->position[2];
    float gz;
    if (g->col && hta_collision_ground(g->col, out_pos[0], out_pos[1], out_pos[2] + 1.0f, &gz))
        out_pos[2] = gz;
    if (out_facing) *out_facing = sp->facing;
}

static void spawn_unit(hta_game *g, int32_t idx)
{
    hta_unit *u = &g->units[idx];
    hta_game_unseat(g, idx);
    float pos[3], facing;
    hta_game_pick_spawn(g, idx, pos, &facing);
    hta_player_init(&u->body);
    hta_player_apply_physics(&u->body, &g->phys);
    for (int k = 0; k < 3; k++) u->body.pos[k] = pos[k];
    u->body.on_ground = true;
    hta_camera_init(&u->eye);
    u->eye.yaw = facing;
    u->eye.pitch = 0.0f;
    u->eye.pos[0] = pos[0]; u->eye.pos[1] = pos[1];
    u->eye.pos[2] = pos[2] + u->body.eye_height;
    u->vitals = g->vitals_template;
    hta_vitals_reset(&u->vitals);
    arm(g, u);
    u->alive = true;
    u->spree = 0;
    u->last_attacker = HTA_GAME_NONE;
    u->powerup = HTA_ITEM_NONE;
    u->powerup_timer = 0.0f;
    memset(u->attackers, 0, sizeof(u->attackers));
    memset(&u->in, 0, sizeof(u->in));
    if (u->kind == HTA_UNIT_BOT) hta_brain_reset(&g->brains[idx]);
    hta_game_event e = { .kind = HTA_EV_SPAWN, .a = idx, .b = -1 };
    for (int k = 0; k < 3; k++) e.pos[k] = pos[k];
    e.dir[0] = cosf(facing); e.dir[1] = sinf(facing);
    emit(g, &e);
}

void hta_game_spawn(hta_game *g, int32_t idx)
{
    if (!g || idx<0 || idx>=(int32_t)g->unit_count ||
        g->units[idx].kind==HTA_UNIT_NONE) return;
    spawn_unit(g,idx);
}

void hta_game_revive(hta_game *g, int32_t idx)
{
    if (!g || idx < 0 || idx >= (int32_t)g->unit_count) return;
    hta_unit *u = &g->units[idx];
    hta_game_unseat(g, idx);
    u->alive = true;
    u->spree = 0;
    u->last_attacker = HTA_GAME_NONE;
    memset(u->attackers, 0, sizeof(u->attackers));
    arm(g, u);
}

void hta_game_start(hta_game *g)
{
    if (!g) return;
    g->over = false;
    g->winner = HTA_GAME_NONE;
    g->leader = HTA_GAME_NONE;
    g->time = 0.0f;
    g->event_count = 0;
    for (uint32_t i = 0; i < g->unit_count; i++) {
        hta_unit *u = &g->units[i];
        if (u->kind == HTA_UNIT_NONE) continue;
        u->kills = u->deaths = u->suicides = u->betrayals = u->assists = 0;
        u->score = u->spree = u->multi = 0;
        if (u->kind == HTA_UNIT_LOCAL) { hta_game_revive(g, (int32_t)i); continue; }
        spawn_unit(g, (int32_t)i);
    }
    for (uint32_t p = 0; p < g->pool_count; p++)
        for (uint32_t k = 0; k < HTA_PROJ_MAX; k++) g->pools[p].live[k].alive = false;
    if (g->vehicles && g->simulate_vehicles) {
        for (uint32_t i = 0; i < g->vehicles->count; i++) hta_vehicles_reset(g->vehicles, i);
        hta_vehicles_sync(g->vehicles);
    }
    memset(g->vgun, 0, sizeof(g->vgun));
    for (uint32_t i = 0; i < HTA_VEHICLE_MAX; i++) g->vgun[i].last_driver = -1;
    char buf[96];
    hta_game_event e = { .kind = HTA_EV_ANNOUNCE, .a = -1, .b = -1,
                         .line = g->teams ? HTA_LINE_NONE : HTA_LINE_SLAYER,
                         .for_local = true };
    snprintf(e.text, sizeof(e.text), "%s", text(g, 4, buf, sizeof(buf), "Slayer"));
    emit(g, &e);
}

void hta_game_sync_local(hta_game *g, const hta_player *body, const hta_camera *eye,
                         int32_t weapon)
{
    if (!g || g->local < 0) return;
    hta_unit *u = &g->units[g->local];
    u->body = *body;
    u->eye = *eye;
    if (weapon >= 0 && (uint32_t)weapon < g->weapon_count) {
        u->carry[u->slot & 1u].weapon = weapon;
    }
}

void hta_game_give(hta_game *g, int32_t idx, int32_t weapon, const hta_ammo *ammo)
{
    if (!g || idx < 0 || idx >= (int32_t)g->unit_count ||
        weapon < 0 || (uint32_t)weapon >= g->weapon_count) return;
    hta_unit *u = &g->units[idx];
    uint32_t s = u->slot & 1u;
    if (u->carry[s].weapon >= 0 && u->carry[s ^ 1u].weapon < 0) s ^= 1u;
    u->carry[s].weapon = weapon;
    if (ammo) u->carry[s].ammo = *ammo;
    else hta_ammo_init(&u->carry[s].ammo, &g->weapons[weapon].def);
    u->slot = s;
    u->cooldown = g->weapons[weapon].def.cooldown * 0.5f;
    hta_game_event e = { .kind = HTA_EV_SWAP, .a = idx, .b = -1, .weapon = weapon };
    emit(g, &e);
}

/* ---------------------------------------------------------------- damage */

void hta_game_centre(const hta_game *g, int32_t idx, float out[3])
{
    const hta_unit *u = &g->units[idx];
    float h = u->body.phys.coll_stand > 0.0f ? u->body.phys.coll_stand : 0.7f;
    out[0] = u->body.pos[0];
    out[1] = u->body.pos[1];
    out[2] = u->body.pos[2] + h * (0.6f - 0.2f * u->body.crouch_t);
}

static float unit_height(const hta_unit *u)
{
    float s = u->body.phys.coll_stand > 0.0f ? u->body.phys.coll_stand : 0.7f;
    float c = u->body.phys.coll_crouch > 0.0f ? u->body.phys.coll_crouch : 0.5f;
    /* Sitting is about as tall as crouching. */
    if (u->vehicle >= 0) return c;
    return s + (c - s) * u->body.crouch_t;
}

int32_t hta_game_ray(const hta_game *g, const float o[3], const float d[3],
                     float max_t, int32_t ignore, float *out_t, float out_hit[3])
{
    if (!g) return -1;
    int32_t best = -1;
    float best_t = max_t;
    for (uint32_t i = 0; i < g->unit_count; i++) {
        const hta_unit *u = &g->units[i];
        if ((int32_t)i == ignore || !u->alive || u->kind == HTA_UNIT_NONE) continue;
        if (hta_game_enclosed(g, (int32_t)i)) continue;
        /* Nobody shoots their own ride's other seats by accident. */
        if (ignore >= 0 && ignore < (int32_t)g->unit_count && u->vehicle >= 0 &&
            u->vehicle == g->units[ignore].vehicle) continue;
        float r = u->body.phys.radius > 0.0f ? u->body.phys.radius : 0.175f;
        float h = unit_height(u);
        /* Vertical cylinder: solve in XY, then check the height. */
        float ox = o[0] - u->body.pos[0], oy = o[1] - u->body.pos[1];
        float a = d[0]*d[0] + d[1]*d[1];
        float b = 2.0f * (ox*d[0] + oy*d[1]);
        float cc = ox*ox + oy*oy - r*r;
        float t = -1.0f;
        if (a > 1e-8f) {
            float disc = b*b - 4.0f*a*cc;
            if (disc < 0.0f) continue;
            float sq = sqrtf(disc);
            float t0 = (-b - sq) / (2.0f*a), t1 = (-b + sq) / (2.0f*a);
            t = t0 >= 0.0f ? t0 : t1;
            if (t < 0.0f) continue;
        } else {
            if (cc > 0.0f) continue;
            t = 0.0f;
        }
        float z = o[2] + d[2] * t - u->body.pos[2];
        if (z < 0.0f || z > h) {
            /* Through the top or bottom cap instead? */
            float capz = z > h ? h : 0.0f;
            if (fabsf(d[2]) < 1e-6f) continue;
            float tc = (u->body.pos[2] + capz - o[2]) / d[2];
            if (tc < 0.0f) continue;
            float cx = ox + d[0]*tc, cy = oy + d[1]*tc;
            if (cx*cx + cy*cy > r*r) continue;
            t = tc;
        }
        if (t < best_t) { best_t = t; best = (int32_t)i; }
    }
    if (best >= 0) {
        if (out_t) *out_t = best_t;
        if (out_hit) for (int k = 0; k < 3; k++) out_hit[k] = o[k] + d[k] * best_t;
    }
    return best;
}

int32_t hta_game_near(const hta_game *g, const float pos[3], float reach, int32_t ignore)
{
    if (!g) return -1;
    int32_t best = -1;
    float best_d = 1e9f;
    for (uint32_t i = 0; i < g->unit_count; i++) {
        const hta_unit *u = &g->units[i];
        if ((int32_t)i == ignore || !u->alive || u->kind == HTA_UNIT_NONE) continue;
        if (hta_game_enclosed(g, (int32_t)i)) continue;
        float r = u->body.phys.radius > 0.0f ? u->body.phys.radius : 0.175f;
        float dz = 0.0f, h = unit_height(u);
        if (pos[2] < u->body.pos[2]) dz = u->body.pos[2] - pos[2];
        else if (pos[2] > u->body.pos[2] + h) dz = pos[2] - (u->body.pos[2] + h);
        float dxy = hypotf(pos[0] - u->body.pos[0], pos[1] - u->body.pos[1]) - r;
        if (dxy < 0.0f) dxy = 0.0f;
        float d = sqrtf(dxy*dxy + dz*dz);
        if (d <= reach && d < best_d) { best_d = d; best = (int32_t)i; }
    }
    return best;
}

void hta_game_hurt(hta_game *g, int32_t victim, int32_t attacker, float amount,
                   const float at[3])
{
    if (!g || victim < 0 || victim >= (int32_t)g->unit_count || amount <= 0.0f) return;
    hta_unit *v = &g->units[victim];
    /* Nobody gets hurt in the postgame. */
    if (!v->alive || g->over) return;
    hta_vitals_damage(&v->vitals, amount);
    v->hurt = true;
    if (attacker >= 0 && attacker < (int32_t)g->unit_count) {
        v->last_attacker = attacker;
        v->since_attacked = 0.0f;
        if (attacker != victim) v->attackers[attacker] = 1;
    }
    hta_game_event e = { .kind = HTA_EV_HIT_UNIT, .a = victim, .b = attacker,
                         .amount = amount };
    if (at) for (int k = 0; k < 3; k++) e.pos[k] = at[k];
    else hta_game_centre(g, victim, e.pos);
    emit(g, &e);
}

void hta_game_hurt_jpt(hta_game *g, int32_t victim, int32_t attacker,
                       uint32_t jpt, int count, const float at[3])
{
    if (!g || victim < 0 || victim >= (int32_t)g->unit_count || !jpt) return;
    if (count < 1) count = 1;
    if (count > 32) count = 32;
    hta_unit *v = &g->units[victim];
    float total = 0.0f;
    /* What a round does depends on what it hits, pellet by pellet: the
     * shield takes the shield multiplier until it is gone. */
    float shield = v->vitals.shield;
    for (int i = 0; i < count; i++) {
        uint8_t mat = shield > 0.0f ? HTA_MATERIAL_CYBORG_SHIELD : HTA_MATERIAL_CYBORG_ARMOR;
        float d = hta_damage_vs(g->cache, jpt, mat);
        shield -= d;
        total += d;
    }
    hta_game_hurt(g, victim, attacker, total, at);
}

void hta_game_blast(hta_game *g, int32_t attacker, const float centre[3],
                    float damage, float core, float radius)
{
    if (!g || damage <= 0.0f || radius <= 0.0f) return;
    for (uint32_t i = 0; i < g->unit_count; i++) {
        hta_unit *u = &g->units[i];
        if (!u->alive || u->kind == HTA_UNIT_NONE) continue;
        float c[3];
        hta_game_centre(g, (int32_t)i, c);
        float dx = c[0]-centre[0], dy = c[1]-centre[1], dz = c[2]-centre[2];
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
        if (dist >= radius) continue;
        float f = 1.0f;
        if (dist > core && radius > core) f = 1.0f - (dist - core) / (radius - core);
        if (f <= 0.0f) continue;
        /* A wall between you and the blast keeps it off you. */
        if (g->col && dist > 0.05f) {
            float dir[3] = { dx/dist, dy/dist, dz/dist };
            float t;
            if (hta_collision_ray(g->col, centre, dir, dist - 0.05f, &t, NULL, NULL) &&
                t > 0.1f)
                continue;
        }
        /* Inside a hull, a rider takes the vehicle's tagged share. */
        if (hta_game_enclosed(g, (int32_t)i) && g->vehicles) {
            const hta_vehicle *car = &g->vehicles->cars[u->vehicle];
            f *= g->vehicles->types[car->type].rider_damage;
        }
        hta_game_hurt(g, (int32_t)i, attacker, damage * f, c);
    }
}

/* ---------------------------------------------------------------- scoring */

static void announce(hta_game *g, hta_line line, const char *msg, bool for_local)
{
    hta_game_event e = { .kind = HTA_EV_ANNOUNCE, .a = -1, .b = -1, .line = line,
                         .for_local = for_local };
    snprintf(e.text, sizeof(e.text), "%s", msg);
    emit(g, &e);
}

static void die(hta_game *g, int32_t idx)
{
    hta_unit *v = &g->units[idx];
    bool by_vehicle = v->splattered;
    v->splattered = false;
    hta_game_unseat(g, idx);
    v->alive = false;
    v->deaths++;
    v->respawn = g->respawn_time;
    v->dead_for = 0.0f;
    v->death_yaw = v->eye.yaw;
    int32_t killer = (v->last_attacker >= 0 && v->since_attacked <= HTA_CREDIT_WINDOW)
                   ? v->last_attacker : HTA_GAME_NONE;
    char buf[96], fmt[96];
    hta_game_event e = { .kind = HTA_EV_KILL, .a = idx, .b = killer, .weapon = -1 };
    hta_game_centre(g, idx, e.pos);
    if (killer >= 0 && killer != idx && g->units[killer].kind != HTA_UNIT_NONE) {
        hta_unit *k = &g->units[killer];
        e.weapon = k->carry[k->slot & 1u].weapon;
        if (k->vehicle >= 0 && g->vehicles) {
            uint16_t type = g->vehicles->cars[k->vehicle].type;
            if (type < HTA_VEHICLE_TYPES && g->vweapon[type][0] >= 0) e.weapon = g->vweapon[type][0];
        }
        bool betrayal = g->teams && k->team == v->team;
        if (betrayal) {
            k->betrayals++;
            k->score--;
            text(g, 79, fmt, sizeof(fmt), "%s was betrayed by %s");
        } else {
            k->kills++;
            k->score++;
            k->spree++;
            k->multi = k->multi_timer > 0.0f ? k->multi + 1 : 1;
            k->multi_timer = HTA_MULTIKILL_WINDOW;
            text(g, 78, fmt, sizeof(fmt), "%s was killed by %s");
        }
        snprintf(e.text, sizeof(e.text), fmt, v->name, k->name);
        emit(g, &e);
        /* Everyone else who hurt him this life helped. */
        for (uint32_t a = 0; a < g->unit_count; a++)
            if (v->attackers[a] && (int32_t)a != killer && (int32_t)a != idx)
                g->units[a].assists++;

        if (!betrayal) {
            bool mine = killer == g->local;
            /* The multikill and the spree, in the Trial's words and voice. */
            hta_line line = HTA_LINE_NONE;
            uint32_t tx = 0;
            const char *fb = NULL;
            if (k->multi == 2) { line = HTA_LINE_DOUBLE_KILL; tx = 85; fb = "Double Kill!"; }
            else if (k->multi == 3) { line = HTA_LINE_TRIPLE_KILL; tx = 84; fb = "Triple Kill!"; }
            else if (k->multi >= 4) { line = HTA_LINE_KILLTACULAR; tx = 83; fb = "Killtacular!"; }
            if (line != HTA_LINE_NONE)
                announce(g, line, text(g, tx, buf, sizeof(buf), fb), mine);
            if (k->spree == HTA_SPREE_KILLS)
                announce(g, HTA_LINE_KILLING_SPREE,
                         text(g, 87, buf, sizeof(buf), "You are on a killing spree!"), mine);
            else if (k->spree == HTA_RIOT_KILLS)
                announce(g, HTA_LINE_RUNNING_RIOT,
                         text(g, 86, buf, sizeof(buf), "Running Riot!"), mine);
        }
    } else {
        /* Nobody to blame: a suicide if it was your own doing, else a
         * plain death. Halo's Slayer takes a point for both. */
        v->suicides++;
        v->score--;
        if (killer == idx) text(g, 81, fmt, sizeof(fmt), "%s committed suicide");
        else if (by_vehicle) text(g, 77, fmt, sizeof(fmt), "%s was killed by a vehicle");
        else text(g, 75, fmt, sizeof(fmt), "%s died");
        snprintf(e.text, sizeof(e.text), fmt, v->name);
        emit(g, &e);
    }
    v->spree = 0;
    v->multi = 0;

    /* The game is over when somebody reaches the limit. */
    if (!g->over && g->score_limit > 0 && killer >= 0 &&
        g->units[killer].score >= g->score_limit)
        finish(g, killer);
}

/* ---------------------------------------------------------------- firing */

static void aim_dir(const hta_camera *eye, float out[3])
{
    float cp = cosf(eye->pitch);
    out[0] = cosf(eye->yaw) * cp;
    out[1] = sinf(eye->yaw) * cp;
    out[2] = sinf(eye->pitch);
}

/* A direction inside a cone of `half` about `aim`, uniform in the disc. */
static void cone(uint32_t *rng, const float aim[3], float half, float out[3])
{
    float up[3] = { 0, 0, 1 };
    if (fabsf(aim[2]) > 0.95f) { up[0] = 1; up[2] = 0; }
    float r[3] = { aim[1]*up[2] - aim[2]*up[1], aim[2]*up[0] - aim[0]*up[2],
                   aim[0]*up[1] - aim[1]*up[0] };
    float rl = sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
    for (int k = 0; k < 3; k++) r[k] /= rl;
    float u[3] = { r[1]*aim[2] - r[2]*aim[1], r[2]*aim[0] - r[0]*aim[2],
                   r[0]*aim[1] - r[1]*aim[0] };
    float a = frand(rng) * 2.0f * PI;
    float t = tanf(half) * sqrtf(frand(rng));
    for (int k = 0; k < 3; k++) out[k] = aim[k] + (r[k]*cosf(a) + u[k]*sinf(a)) * t;
    float l = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
    for (int k = 0; k < 3; k++) out[k] /= l;
}

static void fire(hta_game *g, int32_t idx, float dt)
{
    hta_unit *u = &g->units[idx];
    hta_carried *c = &u->carry[u->slot & 1u];
    if (c->weapon < 0) return;
    const hta_game_weapon *w = &g->weapons[c->weapon];
    (void)dt;
    if (!hta_ammo_shoot(&c->ammo)) {
        if (c->ammo.dry && hta_ammo_reload(&c->ammo)) {
            u->reloading = true;
            hta_game_event e = { .kind = HTA_EV_RELOAD, .a = idx, .b = -1 };
            emit(g, &e);
        }
        u->cooldown = 0.35f;
        return;
    }
    u->cooldown = w->def.cooldown > 0.0f ? w->def.cooldown : 0.2f;
    u->fired = true;
    u->since_shot = 0.0f;

    float spread = w->def.error_angle[0] +
                   (w->def.error_angle[1] - w->def.error_angle[0]) * u->error;
    if (w->def.error_accel > 0.0f) {
        u->error += u->cooldown / w->def.error_accel;
        if (u->error > 1.0f) u->error = 1.0f;
    }
    float aim[3];
    aim_dir(&u->eye, aim);
    float muzzle[3];
    for (int k = 0; k < 3; k++) muzzle[k] = u->eye.pos[k] + aim[k] * 0.3f;

    hta_game_event fe = { .kind = HTA_EV_FIRE, .a = idx, .b = -1, .weapon = c->weapon };
    for (int k = 0; k < 3; k++) { fe.pos[k] = muzzle[k]; fe.dir[k] = aim[k]; }
    emit(g, &fe);

    int pellets = w->def.projectiles_per_shot > 0 ? w->def.projectiles_per_shot : 1;
    if (pellets > 16) pellets = 16;
    if (w->travels && w->pool >= 0) {
        for (int p = 0; p < pellets; p++) {
            float dir[3];
            cone(&u->rng, aim, spread, dir);
            int slot = hta_projectiles_fire(&g->pools[w->pool], muzzle, dir);
            if (slot >= 0) g->pool_owner[w->pool][slot] = (int8_t)idx;
        }
        return;
    }
    /* Hitscan: every pellet finds its own victim or its own wall. Pellets
     * that land on the same body are dealt together, so the shield
     * multiplier is spent in order. */
    int hits[HTA_GAME_MAX_UNITS];
    float hit_at[HTA_GAME_MAX_UNITS][3];
    memset(hits, 0, sizeof(hits));
    for (int p = 0; p < pellets; p++) {
        float dir[3];
        cone(&u->rng, aim, spread, dir);
        float wt = HTA_GUN_RANGE, wh[3], wn[3];
        uint8_t mat = HTA_MATERIAL_NONE;
        bool wall = g->col && hta_collision_ray_material(g->col, u->eye.pos, dir,
                                  HTA_GUN_RANGE, &wt, wh, wn, &mat);
        float ut, uh[3];
        int32_t who = hta_game_ray(g, u->eye.pos, dir, wall ? wt : HTA_GUN_RANGE,
                                   idx, &ut, uh);
        if (who >= 0) {
            hits[who]++;
            for (int k = 0; k < 3; k++) hit_at[who][k] = uh[k];
        } else if (wall) {
            hta_game_event e = { .kind = HTA_EV_HIT_WORLD, .a = idx, .b = -1,
                                 .weapon = c->weapon, .material = mat };
            for (int k = 0; k < 3; k++) { e.pos[k] = wh[k]; e.dir[k] = wn[k]; }
            emit(g, &e);
        }
    }
    for (uint32_t v = 0; v < g->unit_count; v++)
        if (hits[v]) hta_game_hurt_jpt(g, (int32_t)v, idx, w->impact_jpt, hits[v], hit_at[v]);
}

int32_t hta_game_melee(hta_game *g, int32_t idx)
{
    if (!g || idx < 0 || idx >= (int32_t)g->unit_count) return -1;
    hta_unit *u = &g->units[idx];
    const hta_game_weapon *w = hta_game_held(g, idx);
    u->meleed = true;
    float fwd[3];
    aim_dir(&u->eye, fwd);
    float reach[3];
    for (int k = 0; k < 3; k++)
        reach[k] = u->eye.pos[k] - u->body.eye_height * 0.4f * (k == 2) + fwd[k] * HTA_GAME_MELEE_REACH;
    int32_t who = hta_game_near(g, reach, HTA_GAME_MELEE_REACH, idx);
    hta_game_event e = { .kind = HTA_EV_MELEE, .a = idx, .b = who };
    for (int k = 0; k < 3; k++) e.pos[k] = reach[k];
    emit(g, &e);
    if (who < 0 || !w) return who;
    /* From behind it is a kill: the victim is facing the way the blow is
     * travelling. */
    const hta_unit *v = &g->units[who];
    float vf[2] = { cosf(v->eye.yaw), sinf(v->eye.yaw) };
    float to[2] = { v->body.pos[0] - u->body.pos[0], v->body.pos[1] - u->body.pos[1] };
    float tl = hypotf(to[0], to[1]);
    bool behind = tl > 1e-4f && (vf[0]*to[0] + vf[1]*to[1]) / tl > 0.5f;
    float c[3];
    hta_game_centre(g, who, c);
    if (w->melee_jpt) {
        hta_game_hurt_jpt(g, who, idx, w->melee_jpt, 1, c);
        if (behind) hta_game_hurt(g, who, idx, w->melee_damage * (HTA_BACKSMACK_MULT - 1.0f), c);
    }
    return who;
}

static void throw_grenade(hta_game *g, int32_t idx)
{
    hta_unit *u = &g->units[idx];
    if (g->grenade_pool < 0 || u->grenades <= 0) return;
    float fwd[3];
    aim_dir(&u->eye, fwd);
    float at[3], dir[3];
    for (int k = 0; k < 3; k++) {
        at[k] = u->eye.pos[k] + fwd[k] * 0.4f;
        dir[k] = fwd[k] + (k == 2 ? 0.25f : 0.0f);
    }
    int slot = hta_projectiles_throw(&g->pools[g->grenade_pool], at, dir,
                                     HTA_GAME_GRENADE_THROW);
    if (slot >= 0) g->pool_owner[g->grenade_pool][slot] = (int8_t)idx;
    u->grenades--;
    u->threw = true;
    hta_game_event e = { .kind = HTA_EV_GRENADE, .a = idx, .b = -1 };
    for (int k = 0; k < 3; k++) e.pos[k] = at[k];
    emit(g, &e);
}

/* ---------------------------------------------------------------- items */

static void take_items(hta_game *g, int32_t idx)
{
    hta_unit *u = &g->units[idx];
    hta_pickups *it = g->items;
    if (!it || !it->loaded) return;
    int32_t got = hta_pickups_at(it, u->body.pos);
    const hta_item_choice *item = hta_pickups_item(it, got);
    if (item) {
        bool taken = false;
        switch (item->kind) {
        case HTA_ITEM_GRENADE:
            if (u->grenades < g->max_grenades) { u->grenades++; taken = true; }
            break;
        case HTA_ITEM_HEALTH:
            if (u->vitals.health < u->vitals.max_health) {
                u->vitals.health = u->vitals.max_health; taken = true;
            }
            break;
        case HTA_ITEM_OVERSHIELD:
            u->vitals.shield = u->vitals.max_shield * GAME_OVERSHIELD;
            u->powerup = HTA_ITEM_OVERSHIELD;
            u->powerup_timer = item->powerup_time;
            taken = true;
            break;
        case HTA_ITEM_CAMOUFLAGE:
            u->powerup = HTA_ITEM_CAMOUFLAGE;
            u->powerup_timer = item->powerup_time;
            taken = true;
            break;
        default:
            break;
        }
        if (taken) {
            hta_game_event e = { .kind = HTA_EV_PICKUP, .a = idx, .b = -1, .tag = item->tag_id };
            for (int k = 0; k < 3; k++) e.pos[k] = u->body.pos[k];
            emit(g, &e);
            hta_pickups_take(it, got);
        }
    }
    int32_t ws = hta_pickups_at_kind(it, u->body.pos, HTA_ITEM_WEAPON);
    const hta_item_choice *w = hta_pickups_item(it, ws);
    if (!w) return;
    int32_t wi = hta_game_weapon_index(g, w->tag_id);
    if (wi < 0) return;
    /* Carrying it already: take its ammunition, as Halo does. */
    for (int s = 0; s < 2; s++) {
        if (u->carry[s].weapon != wi) continue;
        hta_ammo *a = &u->carry[s].ammo;
        if (a->reserve >= a->reserve_max) return;
        hta_ammo fresh;
        hta_ammo_init(&fresh, &g->weapons[wi].def);
        a->reserve += fresh.loaded + fresh.reserve;
        if (a->reserve > a->reserve_max) a->reserve = a->reserve_max;
        hta_game_event e = { .kind = HTA_EV_PICKUP, .a = idx, .b = -1, .tag = w->tag_id };
        for (int k = 0; k < 3; k++) e.pos[k] = u->body.pos[k];
        emit(g, &e);
        hta_pickups_take(it, ws);
        return;
    }
    if (!u->in.pickup) return;
    hta_game_give(g, idx, wi, NULL);
    hta_game_event e = { .kind = HTA_EV_PICKUP, .a = idx, .b = -1, .tag = w->tag_id };
    for (int k = 0; k < 3; k++) e.pos[k] = u->body.pos[k];
    emit(g, &e);
    hta_pickups_take(it, ws);
}

/* ---------------------------------------------------------------- vehicles */

static uint32_t find_tag_path(const hta_cache *c, uint32_t cls, const char *path)
{
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        char p[256];
        if (!hta_cache_tag(c, i, &t) || t.primary_class != cls) continue;
        if (hta_cache_tag_path(c, &t, p, sizeof(p)) && !strcmp(p, path)) return t.tag_id;
    }
    return 0;
}

/* A round slower than this flies as an object even with nothing to draw;
 * a faster one is hitscan, as it is on foot. 150 wu/s splits the Trial's
 * vehicle guns cleanly: bullets at 290-320, shells and bolts at 50-100. */
#define VEHICLE_TRAVEL_SPEED 150.0f

static int32_t pool_any(hta_game *g, uint32_t proj, const hta_resource_map *bm)
{
    for (uint32_t i = 0; i < g->pool_count; i++)
        if (g->pools[i].proj_tag_id == proj) return (int32_t)i;
    if (g->pool_count >= HTA_GAME_MAX_POOLS) return -1;
    hta_projectiles *p = &g->pools[g->pool_count];
    hta_projectiles_init(p);
    char err[HTA_ERRLEN];
    if (!hta_projectiles_equip_any(p, g->cache, bm, proj, err, sizeof(err))) return -1;
    float speed = p->speed_initial > 0.0f ? p->speed_initial : p->speed_final;
    if (p->verts_each == 0 && speed >= VEHICLE_TRAVEL_SPEED) {
        hta_projectiles_free(p);
        return -1;
    }
    g->pool_weapon[g->pool_count] = -1;
    for (int k = 0; k < HTA_PROJ_MAX; k++) g->pool_owner[g->pool_count][k] = -1;
    return (int32_t)g->pool_count++;
}

bool hta_game_attach_vehicles(hta_game *g, hta_vehicles *v, const hta_resource_map *bm)
{
    if (!g || !v || !v->loaded) return false;
    g->vehicles = v;
    g->splatter_jpt = find_tag_path(g->cache, HTA_FOURCC('j','p','t','!'),
                                    "globals\\vehicle_collision");
    for (uint32_t t = 0; t < v->type_count && t < HTA_VEHICLE_TYPES; t++) {
        uint32_t tag = v->types[t].weapon_tag;
        g->vweapon[t][0] = g->vweapon[t][1] = -1;
        if (!tag) continue;
        for (uint32_t trig = 0; trig < 2; trig++) {
            if (g->weapon_count >= HTA_GAME_MAX_WEAPONS) break;
            hta_game_weapon *w = &g->weapons[g->weapon_count];
            memset(w, 0, sizeof(*w));
            if (!hta_weapon_load_trigger(g->cache, tag, trig, &w->def)) break;
            if (!w->def.projectile_id) continue;
            w->tag = tag;
            w->vehicle = true;
            w->trigger = (int8_t)trig;
            w->pool = -1;
            int32_t ti = hta_cache_find_tag_by_id(g->cache, tag);
            hta_tag_entry te;
            uint32_t base = 0;
            if (ti >= 0 && hta_cache_tag(g->cache, (uint32_t)ti, &te) &&
                hta_cache_ptr_to_offset(g->cache, te.tag_data_ptr, &base)) {
                read_label(g->cache, base, w->label);
                hta_rd_f32(g->cache, base + WEAP_AUTOAIM_ANGLE, &w->autoaim_angle);
                hta_rd_f32(g->cache, base + WEAP_AUTOAIM_RANGE, &w->autoaim_range);
                hta_rd_f32(g->cache, base + WEAP_MAGNET_ANGLE, &w->magnet_angle);
                hta_rd_f32(g->cache, base + WEAP_MAGNET_RANGE, &w->magnet_range);
            }
            snprintf(w->anim_class, sizeof(w->anim_class), "unarmed");
            w->impact_jpt = hta_projectile_impact_damage(g->cache, w->def.projectile_id);
            int32_t pool = pool_any(g, w->def.projectile_id, bm);
            if (pool >= 0) {
                w->travels = true;
                w->pool = pool;
                if (g->pool_weapon[pool] < 0) g->pool_weapon[pool] = (int32_t)g->weapon_count;
                const hta_projectiles *p = &g->pools[pool];
                w->speed = p->speed_initial > 0.0f ? p->speed_initial : p->speed_final;
                w->blast_damage = p->blast_damage;
                w->blast_radius = p->blast_damage_radius;
                w->blast_core = p->blast_core;
            }
            g->vweapon[t][trig] = (int32_t)g->weapon_count++;
        }
    }
    for (uint32_t i = 0; i < HTA_VEHICLE_MAX; i++) g->vgun[i].last_driver = -1;
    return true;
}

bool hta_game_enclosed(const hta_game *g, int32_t unit)
{
    if (!g || !g->vehicles || unit < 0 || unit >= (int32_t)g->unit_count) return false;
    const hta_unit *u = &g->units[unit];
    if (u->vehicle < 0 || (uint32_t)u->vehicle >= g->vehicles->count) return false;
    const hta_vehicle *car = &g->vehicles->cars[u->vehicle];
    const hta_vehicle_seat *s = hta_vehicles_seat(g->vehicles, (uint32_t)u->vehicle,
                                                  (uint32_t)u->seat);
    /* The Scorpion's driver is under armour, the Banshee's under glass. */
    return s && (s->flags & HTA_SEAT_DRIVER) &&
           (car->kind == HTA_VK_TANK || car->kind == HTA_VK_FIGHTER);
}

bool hta_game_seat_root(const hta_game *g, int32_t unit, hta_transform *out)
{
    if (!g || !g->vehicles || unit < 0 || unit >= (int32_t)g->unit_count) return false;
    const hta_unit *u = &g->units[unit];
    if (u->vehicle < 0) return false;
    return hta_vehicles_seat_transform(g->vehicles, (uint32_t)u->vehicle, (uint32_t)u->seat, out);
}

static bool hostile_car(const hta_game *g, int32_t unit, uint32_t car)
{
    const hta_vehicle *c = &g->vehicles->cars[car];
    for (uint32_t s = 0; s < HTA_VEHICLE_SEATS; s++) {
        int32_t o = c->occupant[s];
        if (o >= 0 && o < (int32_t)g->unit_count && o != unit && enemies(g, unit, o) && g->teams)
            return true;
    }
    return false;
}

int32_t hta_game_seat_near(const hta_game *g, int32_t unit, int32_t *out_seat)
{
    if (out_seat) *out_seat = -1;
    if (!g || !g->vehicles || unit < 0 || unit >= (int32_t)g->unit_count) return -1;
    const hta_unit *u = &g->units[unit];
    if (!u->alive || u->vehicle >= 0) return -1;
    int32_t seat = -1;
    int32_t car = hta_vehicles_near(g->vehicles, g->col, u->body.pos, &seat);
    if (car < 0 || hostile_car(g, unit, (uint32_t)car)) return -1;
    if (out_seat) *out_seat = seat;
    return car;
}

bool hta_game_seat(hta_game *g, int32_t unit, int32_t car, int32_t seat)
{
    if (!g || !g->vehicles || unit < 0 || unit >= (int32_t)g->unit_count ||
        car < 0 || seat < 0) return false;
    hta_unit *u = &g->units[unit];
    if (u->vehicle == car && u->seat == seat) return true;
    hta_game_unseat(g, unit);
    if (!hta_vehicles_enter(g->vehicles, (uint32_t)car, (uint32_t)seat, unit)) return false;
    u->vehicle = (int16_t)car;
    u->seat = (int8_t)seat;
    memset(u->body.velocity, 0, sizeof(u->body.velocity));
    u->swing = u->throwing = 0.0f;
    const hta_vehicle_seat *st = hta_vehicles_seat(g->vehicles, (uint32_t)car, (uint32_t)seat);
    if (st && (st->flags & HTA_SEAT_DRIVER)) {
        g->vgun[car].last_driver = unit;
        g->vgun[car].since_driven = 0.0f;
        g->vehicles->cars[car].ctl.yaw = u->eye.yaw;
    }
    hta_game_event e = { .kind = HTA_EV_ENTER, .a = unit, .b = car, .pool = seat };
    for (int k = 0; k < 3; k++) e.pos[k] = u->body.pos[k];
    emit(g, &e);
    return true;
}

void hta_game_unseat(hta_game *g, int32_t unit)
{
    if (!g || unit < 0 || unit >= (int32_t)g->unit_count) return;
    hta_unit *u = &g->units[unit];
    if (g->vehicles) hta_vehicles_vacate(g->vehicles, unit);
    u->vehicle = -1;
    u->seat = -1;
}

bool hta_game_board(hta_game *g, int32_t unit)
{
    if (!g || !g->vehicles || unit < 0 || unit >= (int32_t)g->unit_count) return false;
    hta_unit *u = &g->units[unit];
    if (!u->alive) return false;
    if (u->vehicle >= 0) {
        int32_t car = u->vehicle, seat = u->seat;
        float feet[3], yaw = u->eye.yaw;
        float r = u->body.phys.radius > 0.0f ? u->body.phys.radius : 0.175f;
        float h = u->body.phys.coll_stand > 0.0f ? u->body.phys.coll_stand : 0.7f;
        if (!hta_vehicles_exit(g->vehicles, g->col, (uint32_t)car, (uint32_t)seat,
                               r, h, feet, &yaw))
            return false;
        const hta_vehicle *c = &g->vehicles->cars[car];
        u->vehicle = -1;
        u->seat = -1;
        for (int k = 0; k < 3; k++) u->body.pos[k] = feet[k];
        u->body.velocity[0] = cosf(c->yaw) * c->speed + c->lateral_vel[0];
        u->body.velocity[1] = sinf(c->yaw) * c->speed + c->lateral_vel[1];
        u->body.velocity[2] = 0.0f;
        u->body.on_ground = !( c->kind == HTA_VK_FIGHTER && !c->grounded);
        u->body.landed = false;
        u->body.crouch_t = 0.0f;
        u->body.eye_height = u->body.phys.cam_stand;
        u->eye.pos[0] = feet[0]; u->eye.pos[1] = feet[1];
        u->eye.pos[2] = feet[2] + u->body.eye_height;
        hta_game_event e = { .kind = HTA_EV_EXIT, .a = unit, .b = car, .pool = seat };
        for (int k = 0; k < 3; k++) e.pos[k] = feet[k];
        emit(g, &e);
        return true;
    }
    int32_t seat = -1;
    int32_t car = hta_game_seat_near(g, unit, &seat);
    if (car < 0) return false;
    return hta_game_seat(g, unit, car, seat);
}

/* What the crosshair is on: along the unit's eye into the world and
 * everyone in it, but not through its own vehicle. */
static void aim_point(hta_game *g, int32_t unit, float out[3])
{
    hta_unit *u = &g->units[unit];
    float dir[3];
    aim_dir(&u->eye, dir);
    float t = HTA_VEHICLE_AIM_RANGE;
    hta_collision_instance *own = NULL;
    bool was = false;
    if (u->vehicle >= 0 && g->vehicles) {
        own = &g->vehicles->inst[u->vehicle];
        was = own->active;
        own->active = false;
    }
    float wt;
    if (g->col && hta_collision_ray(g->col, u->eye.pos, dir, t, &wt, NULL, NULL)) t = wt;
    float ut;
    if (hta_game_ray(g, u->eye.pos, dir, t, unit, &ut, NULL) >= 0) t = ut;
    if (own) own->active = was;
    for (int k = 0; k < 3; k++) out[k] = u->eye.pos[k] + dir[k] * t;
}

static void vfire(hta_game *g, int32_t idx, uint32_t car, uint32_t trig, float dt)
{
    hta_unit *u = &g->units[idx];
    hta_vehicle *c = &g->vehicles->cars[car];
    hta_game_vgun *gun = &g->vgun[car];
    int32_t wi = c->type < HTA_VEHICLE_TYPES ? g->vweapon[c->type][trig] : -1;
    if (wi < 0) return;
    const hta_game_weapon *w = &g->weapons[wi];
    (void)dt;
    /* One round per pull where the tag says so. */
    bool single = w->def.single_shot || (w->def.trigger_flags & HTA_TRIGGER_NO_REPEAT);
    if (single && gun->was_down[trig]) return;
    if (gun->cooldown[trig] > 0.0f || gun->chamber[trig] > 0.0f) return;
    if (gun->loaded[trig] == 0) return;

    float rof = w->def.rof;
    if (w->def.rof_accel > 0.0f && w->def.rof_initial > 0.0f) {
        float f = gun->held[trig] / w->def.rof_accel;
        if (f > 1.0f) f = 1.0f;
        rof = w->def.rof_initial + (w->def.rof - w->def.rof_initial) * f;
    }
    gun->cooldown[trig] = single ? 0.0f : (rof > 0.1f ? 1.0f / rof : 0.25f);
    if (gun->loaded[trig] > 0) {
        int spend = w->def.rounds_per_shot > 0 ? w->def.rounds_per_shot : 0;
        gun->loaded[trig] -= spend;
        if (gun->loaded[trig] < 0) gun->loaded[trig] = 0;
        bool every = (w->def.mag_flags & HTA_MAG_CHAMBER_EACH_ROUND) != 0;
        if (gun->loaded[trig] == 0 || (every && spend)) {
            float wait = fmaxf(w->def.chamber_time, w->def.reload_time);
            gun->chamber[trig] = wait > 0.0f ? wait : 0.5f;
        }
    }
    u->fired = true;
    u->since_shot = 0.0f;

    float muzzle[3], barrel[3];
    hta_vehicles_trigger(g->vehicles, car, trig, muzzle, barrel);
    /* Converge on the crosshair, as long as the barrel is roughly there. */
    float target[3], aim[3];
    aim_point(g, idx, target);
    float d[3] = { target[0]-muzzle[0], target[1]-muzzle[1], target[2]-muzzle[2] };
    float dl = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    float bl = sqrtf(barrel[0]*barrel[0] + barrel[1]*barrel[1] + barrel[2]*barrel[2]);
    if (dl > 0.5f && bl > 1e-4f &&
        (d[0]*barrel[0] + d[1]*barrel[1] + d[2]*barrel[2]) / (dl * bl) > cosf(0.35f)) {
        for (int k = 0; k < 3; k++) aim[k] = d[k] / dl;
    } else {
        for (int k = 0; k < 3; k++) aim[k] = bl > 1e-4f ? barrel[k] / bl : 0.0f;
    }
    float spread = w->def.error_angle[0] +
                   (w->def.error_angle[1] - w->def.error_angle[0]) * gun->error[trig];
    if (w->def.error_accel > 0.0f) {
        gun->error[trig] += (gun->cooldown[trig] > 0.0f ? gun->cooldown[trig] : 0.2f) /
                            w->def.error_accel;
        if (gun->error[trig] > 1.0f) gun->error[trig] = 1.0f;
    }
    if (g->vehicles->types[c->type].barrel_node >= 0)
        c->barrel_speed = HTA_VEHICLE_BARREL_SPIN;

    hta_game_event fe = { .kind = HTA_EV_FIRE, .a = idx, .b = (int32_t)car, .weapon = wi };
    for (int k = 0; k < 3; k++) { fe.pos[k] = muzzle[k]; fe.dir[k] = aim[k]; }
    emit(g, &fe);

    int pellets = w->def.projectiles_per_shot > 0 ? w->def.projectiles_per_shot : 1;
    if (pellets > 16) pellets = 16;
    if (w->travels && w->pool >= 0) {
        for (int p = 0; p < pellets; p++) {
            float dir[3];
            cone(&u->rng, aim, spread, dir);
            int slot = hta_projectiles_fire(&g->pools[w->pool], muzzle, dir);
            if (slot >= 0) {
                g->pool_owner[w->pool][slot] = (int8_t)idx;
                /* Clear of its own hull before it can hit a vehicle. */
                g->pools[w->pool].live[slot].clear = c->body_radius + 0.5f;
            }
        }
        return;
    }
    int hits[HTA_GAME_MAX_UNITS];
    float hit_at[HTA_GAME_MAX_UNITS][3];
    memset(hits, 0, sizeof(hits));
    hta_collision_instance *own = &g->vehicles->inst[car];
    bool was = own->active;
    own->active = false;
    for (int p = 0; p < pellets; p++) {
        float dir[3];
        cone(&u->rng, aim, spread, dir);
        float wt = HTA_GUN_RANGE, wh[3], wn[3];
        uint8_t mat = HTA_MATERIAL_NONE;
        bool wall = g->col && hta_collision_ray_material(g->col, muzzle, dir,
                                  HTA_GUN_RANGE, &wt, wh, wn, &mat);
        float ut, uh[3];
        int32_t who = hta_game_ray(g, muzzle, dir, wall ? wt : HTA_GUN_RANGE, idx, &ut, uh);
        if (who >= 0) {
            hits[who]++;
            for (int k = 0; k < 3; k++) hit_at[who][k] = uh[k];
        } else if (wall) {
            hta_game_event e = { .kind = HTA_EV_HIT_WORLD, .a = idx, .b = -1,
                                 .weapon = wi, .material = mat };
            for (int k = 0; k < 3; k++) { e.pos[k] = wh[k]; e.dir[k] = wn[k]; }
            emit(g, &e);
        }
    }
    own->active = was;
    for (uint32_t v = 0; v < g->unit_count; v++)
        if (hits[v]) hta_game_hurt_jpt(g, (int32_t)v, idx, w->impact_jpt, hits[v], hit_at[v]);
}

/* The guns' own clocks: cooldowns, chambers, bloom, spin-up. */
static void vguns_tick(hta_game *g, float dt)
{
    if (!g->vehicles) return;
    for (uint32_t i = 0; i < g->vehicles->count && i < HTA_VEHICLE_MAX; i++) {
        hta_game_vgun *gun = &g->vgun[i];
        const hta_vehicle *c = &g->vehicles->cars[i];
        gun->since_driven += dt;
        for (int t = 0; t < 2; t++) {
            int32_t wi = c->type < HTA_VEHICLE_TYPES ? g->vweapon[c->type][t] : -1;
            if (wi < 0) continue;
            const hta_game_weapon *w = &g->weapons[wi];
            if (gun->cooldown[t] > 0.0f) gun->cooldown[t] -= dt;
            if (gun->chamber[t] > 0.0f) {
                gun->chamber[t] -= dt;
                if (gun->chamber[t] <= 0.0f) {
                    gun->chamber[t] = 0.0f;
                    gun->loaded[t] = w->def.rounds_loaded_max > 0 ? w->def.rounds_loaded_max : -1;
                }
            }
            if (gun->loaded[t] == 0 && gun->chamber[t] <= 0.0f)
                gun->loaded[t] = (w->def.magazine < 0 || w->def.rounds_loaded_max <= 0)
                               ? -1 : w->def.rounds_loaded_max;
            if (!gun->was_down[t] && w->def.error_decel > 0.0f) {
                gun->error[t] -= dt / w->def.error_decel;
                if (gun->error[t] < 0.0f) gun->error[t] = 0.0f;
            }
        }
    }
}

/* Everything a seated unit does through its vehicle. */
static void seated(hta_game *g, int32_t idx, float dt)
{
    hta_unit *u = &g->units[idx];
    hta_vehicles *v = g->vehicles;
    if (!v || u->vehicle < 0 || (uint32_t)u->vehicle >= v->count ||
        v->cars[u->vehicle].occupant[u->seat] != idx || !v->cars[u->vehicle].active) {
        hta_game_unseat(g, idx);
        return;
    }
    uint32_t car = (uint32_t)u->vehicle;
    hta_vehicle *c = &v->cars[car];
    const hta_vehicle_seat *s = hta_vehicles_seat(v, car, (uint32_t)u->seat);
    hta_unit_input *in = &u->in;
    /* The look still turns: it is where the gun and the camera go. The
     * local player's look is the platform's own camera. */
    if (idx != g->local) {
        hta_camera_look(&u->eye, in->move.look_yaw, in->move.look_pitch);
        in->move.look_yaw = in->move.look_pitch = 0.0f;
    }
    if (s->flags & HTA_SEAT_DRIVER) {
        c->ctl.driven = true;
        c->ctl.throttle = in->move.move_forward;
        c->ctl.strafe = in->move.move_right;
        c->ctl.yaw = u->eye.yaw;
        c->ctl.pitch = u->eye.pitch;
        c->ctl.brake = in->move.jump;
        g->vgun[car].last_driver = idx;
        g->vgun[car].since_driven = 0.0f;
    }
    if (s->flags & HTA_SEAT_GUNNER) {
        hta_vehicles_aim(v, car, u->eye.yaw, u->eye.pitch, dt);
        hta_game_vgun *gun = &g->vgun[car];
        bool down[2] = { in->move.fire, in->fire2 };
        for (uint32_t t = 0; t < 2; t++) {
            if (down[t]) {
                gun->held[t] += dt;
                if (!g->over) vfire(g, idx, car, t, dt);
            } else gun->held[t] = 0.0f;
            gun->was_down[t] = down[t];
        }
    }
}

/* A body in a seat sits where the seat is; its eye goes to the seat's
 * camera. The local player's camera belongs to the platform. */
static void place_seated(hta_game *g, int32_t idx)
{
    hta_unit *u = &g->units[idx];
    hta_transform root;
    if (!hta_game_seat_root(g, idx, &root)) return;
    const hta_vehicle *c = &g->vehicles->cars[u->vehicle];
    for (int k = 0; k < 3; k++) u->body.pos[k] = root.t[k];
    u->body.velocity[0] = cosf(c->yaw) * c->speed + c->lateral_vel[0];
    u->body.velocity[1] = sinf(c->yaw) * c->speed + c->lateral_vel[1];
    u->body.velocity[2] = c->fall_speed;
    u->body.on_ground = true;
    u->body.landed = u->body.footstep = false;
    if (idx == g->local) return;
    hta_camera cam = u->eye;
    hta_vehicles_camera(g->vehicles, g->col, (uint32_t)u->vehicle, (uint32_t)u->seat,
                        u->eye.yaw, u->eye.pitch, &cam);
    for (int k = 0; k < 3; k++) u->eye.pos[k] = cam.pos[k];
}

/* A moving vehicle and somebody on foot in its way. */
static void splatter(hta_game *g)
{
    hta_vehicles *v = g->vehicles;
    if (!v || !g->splatter_jpt) return;
    float full = hta_damage_vs(g->cache, g->splatter_jpt, HTA_MATERIAL_CYBORG_ARMOR);
    for (uint32_t i = 0; i < v->count; i++) {
        const hta_vehicle *c = &v->cars[i];
        if (!c->active) continue;
        float vel[3] = { cosf(c->yaw) * c->speed + c->lateral_vel[0],
                         sinf(c->yaw) * c->speed + c->lateral_vel[1],
                         c->kind == HTA_VK_FIGHTER ? c->fall_speed : 0.0f };
        float speed = sqrtf(vel[0]*vel[0] + vel[1]*vel[1] + vel[2]*vel[2]);
        if (speed < HTA_SPLATTER_MIN_SPEED) continue;
        const hta_game_vgun *gun = &g->vgun[i];
        int32_t driver = gun->last_driver >= 0 && gun->since_driven < HTA_CREDIT_WINDOW
                       ? gun->last_driver : -1;
        for (uint32_t k = 0; k < g->unit_count; k++) {
            hta_unit *u = &g->units[k];
            if (!u->alive || u->kind == HTA_UNIT_NONE || u->vehicle >= 0) continue;
            float r = u->body.phys.radius > 0.0f ? u->body.phys.radius : 0.175f;
            float h = unit_height(u);
            if (hypotf(u->body.pos[0]-c->pos[0], u->body.pos[1]-c->pos[1]) >
                c->body_radius + r + 0.2f) continue;
            for (uint32_t p = 0; p < c->point_count; p++) {
                hta_transform world;
                hta_vehicles_world(v, i, &world);
                float at[3];
                hta_xf_point(at, &world, c->points[p].pos);
                float pr = c->points[p].radius;
                float dz = 0.0f;
                if (at[2] < u->body.pos[2]) dz = u->body.pos[2] - at[2];
                else if (at[2] > u->body.pos[2] + h) dz = at[2] - (u->body.pos[2] + h);
                float dx = u->body.pos[0] - at[0], dy = u->body.pos[1] - at[1];
                float dxy = hypotf(dx, dy);
                if (dxy - r > pr || dz > pr) continue;
                /* Closing speed: how fast the vehicle comes at them. */
                float n[3] = { dxy > 1e-4f ? dx / dxy : 1.0f, dxy > 1e-4f ? dy / dxy : 0.0f, 0.0f };
                float closing = vel[0]*n[0] + vel[1]*n[1] - (u->body.velocity[0]*n[0] +
                                u->body.velocity[1]*n[1]);
                if (closing < HTA_SPLATTER_MIN_SPEED) break;
                float f = (closing - HTA_SPLATTER_MIN_SPEED) /
                          (HTA_SPLATTER_FULL_SPEED - HTA_SPLATTER_MIN_SPEED);
                if (f > 1.0f) f = 1.0f;
                float at_c[3];
                hta_game_centre(g, (int32_t)k, at_c);
                u->splattered = driver < 0;
                hta_game_hurt(g, (int32_t)k, driver, full * f, at_c);
                /* And out of the way. */
                if ((int32_t)k != g->local) {
                    u->body.velocity[0] += vel[0];
                    u->body.velocity[1] += vel[1];
                }
                break;
            }
        }
    }
}

/* ---------------------------------------------------------------- update */

static void simulate(hta_game *g, int32_t idx, float dt)
{
    hta_unit *u = &g->units[idx];
    hta_unit_input *in = &u->in;
    u->fired = u->meleed = u->threw = u->hurt = false;

    hta_player_update(&u->body, &u->eye, g->col, &in->move, dt);
    if (u->body.landed) {
        float cost = hta_vitals_land(&u->vitals, u->body.land_speed);
        if (cost > 0.0f && u->last_attacker < 0) {
            /* A fall with nobody to blame is your own. */
            u->last_attacker = idx;
            u->since_attacked = 0.0f;
        }
    }
    hta_vitals_update(&u->vitals, dt);

    hta_carried *c = &u->carry[u->slot & 1u];
    hta_ammo_update(&c->ammo, dt);
    u->reloading = c->ammo.phase == HTA_AMMO_RELOADING;
    if (u->cooldown > 0.0f) u->cooldown -= dt;
    if (u->swing > 0.0f) u->swing -= dt;
    u->since_shot += dt;
    if (u->since_shot > u->cooldown + 0.05f && c->weapon >= 0) {
        const hta_game_weapon *w = &g->weapons[c->weapon];
        if (w->def.error_decel > 0.0f) {
            u->error -= dt / w->def.error_decel;
            if (u->error < 0.0f) u->error = 0.0f;
        }
    }

    if (in->swap) {
        in->swap = false;
        if (u->carry[u->slot ^ 1u].weapon >= 0) {
            u->slot ^= 1u;
            u->cooldown = 0.5f;
            hta_game_event e = { .kind = HTA_EV_SWAP, .a = idx, .b = -1,
                                 .weapon = u->carry[u->slot].weapon };
            emit(g, &e);
        }
    }
    if (in->reload) {
        in->reload = false;
        if (u->swing <= 0.0f && hta_ammo_reload(&c->ammo)) {
            hta_game_event e = { .kind = HTA_EV_RELOAD, .a = idx, .b = -1 };
            emit(g, &e);
        }
    }
    if (in->melee) {
        in->melee = false;
        if (u->swing <= 0.0f && u->throwing <= 0.0f) {
            u->swing = UNIT_SWING_TIME;
            hta_game_melee(g, idx);
        }
    }
    if (in->grenade) {
        in->grenade = false;
        if (u->throwing <= 0.0f && u->swing <= 0.0f && u->grenades > 0)
            u->throwing = UNIT_THROW_TIME;
    }
    if (u->throwing > 0.0f) {
        u->throwing -= dt;
        if (u->throwing <= 0.0f) throw_grenade(g, idx);
    }
    if (in->move.fire && u->cooldown <= 0.0f && u->swing <= 0.0f && u->throwing <= 0.0f &&
        c->ammo.phase == HTA_AMMO_READY)
        fire(g, idx, dt);
    take_items(g, idx);
    in->pickup = false;
}

static void fly(hta_game *g, float dt)
{
    for (uint32_t p = 0; p < g->pool_count; p++) {
        hta_projectiles *pool = &g->pools[p];
        hta_projectiles_update(pool, g->col, dt);
        const hta_game_weapon *w = g->pool_weapon[p] >= 0
                                 ? &g->weapons[g->pool_weapon[p]] : NULL;
        uint32_t jpt = w ? w->impact_jpt : 0;
        /* Through someone: it stops there and goes off. */
        for (uint32_t k = 0; k < HTA_PROJ_MAX; k++) {
            hta_projectile *q = &pool->live[k];
            if (!q->alive) continue;
            int32_t owner = g->pool_owner[p][k];
            /* A thrown grenade bounces off people; it waits for its fuse. */
            if ((int32_t)p == g->grenade_pool) continue;
            int32_t who = hta_game_near(g, q->pos, 0.02f, q->age < 0.1f ? owner : -1);
            if (who < 0) continue;
            if (jpt) hta_game_hurt_jpt(g, who, owner, jpt, 1, q->pos);
            if (pool->blast_damage > 0.0f)
                hta_game_blast(g, owner, q->pos, pool->blast_damage,
                               pool->blast_core, pool->blast_damage_radius);
            hta_game_event e = { .kind = HTA_EV_DETONATE, .a = owner, .b = who,
                                 .pool = (int32_t)p, .material = HTA_MATERIAL_NONE };
            for (int m = 0; m < 3; m++) e.pos[m] = q->pos[m];
            e.dir[2] = 1.0f;
            emit(g, &e);
            q->alive = false;
        }
        for (uint32_t b = 0; b < pool->blast_count; b++) {
            int32_t owner = g->pool_owner[p][pool->blasts[b].slot];
            if (pool->blast_damage > 0.0f)
                hta_game_blast(g, owner, pool->blasts[b].pos, pool->blast_damage,
                               pool->blast_core, pool->blast_damage_radius);
            hta_game_event e = { .kind = HTA_EV_DETONATE, .a = owner, .b = -1,
                                 .pool = (int32_t)p, .material = pool->blasts[b].material };
            for (int m = 0; m < 3; m++) {
                e.pos[m] = pool->blasts[b].pos[m];
                e.dir[m] = pool->blasts[b].normal[m];
            }
            emit(g, &e);
        }
    }
}

void hta_game_update(hta_game *g, float dt)
{
    if (!g || !g->loaded || dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;
    if (!g->over) g->time += dt;
    /* Out of time: whoever is ahead wins. */
    if (!g->over && g->time_limit > 0.0f && g->time >= g->time_limit) {
        int32_t order[HTA_GAME_MAX_UNITS];
        finish(g, hta_game_standings(g, order, HTA_GAME_MAX_UNITS) ? order[0] : HTA_GAME_NONE);
    }

    vguns_tick(g, dt);
    for (uint32_t i = 0; i < g->unit_count; i++) {
        hta_unit *u = &g->units[i];
        if (u->kind == HTA_UNIT_NONE) continue;
        u->since_attacked += dt;
        if (u->multi_timer > 0.0f) u->multi_timer -= dt;
        if (!u->alive) {
            u->dead_for += dt;
            if (u->kind != HTA_UNIT_LOCAL && !g->over) {
                u->respawn -= dt;
                if (u->respawn <= 0.0f) spawn_unit(g, (int32_t)i);
            }
            continue;
        }
        if (u->kind == HTA_UNIT_BOT && g->nav && !g->over)
            hta_brain_think(g, (int32_t)i, &g->brains[i], dt);
        if (u->kind == HTA_UNIT_BOT && g->over) memset(&u->in, 0, sizeof(u->in));
        /* In and out of vehicles, for everyone -- the local player too. */
        if (u->in.action) {
            u->in.action = false;
            if (!g->over) hta_game_board(g, (int32_t)i);
        }
        if (u->vehicle >= 0) {
            u->fired = u->meleed = u->threw = u->hurt = false;
            seated(g, (int32_t)i, dt);
            if (u->vehicle >= 0 && u->kind != HTA_UNIT_LOCAL) {
                hta_vitals_update(&u->vitals, dt);
                const hta_vehicle_seat *st = hta_vehicles_seat(g->vehicles,
                    (uint32_t)u->vehicle, (uint32_t)u->seat);
                /* A passenger who may shoot does, with what they carry. */
                if (st && (st->flags & HTA_SEAT_ALLOWS_WEAPONS)) {
                    hta_carried *cw = &u->carry[u->slot & 1u];
                    hta_ammo_update(&cw->ammo, dt);
                    if (u->cooldown > 0.0f) u->cooldown -= dt;
                    u->since_shot += dt;
                    if (u->in.reload) { u->in.reload = false; hta_ammo_reload(&cw->ammo); }
                    if (u->in.move.fire && u->cooldown <= 0.0f &&
                        cw->ammo.phase == HTA_AMMO_READY && !g->over)
                        fire(g, (int32_t)i, dt);
                }
                u->in.melee = u->in.grenade = u->in.swap = u->in.pickup = false;
            }
            if (u->vehicle >= 0) continue;
        }
        if (u->kind != HTA_UNIT_LOCAL) simulate(g, (int32_t)i, dt);

        /* Powerups run down for everyone; the overshield bleeds. */
        if (u->powerup_timer > 0.0f && u->kind != HTA_UNIT_LOCAL) {
            float was = u->powerup_timer;
            u->powerup_timer -= dt;
            if (u->powerup == HTA_ITEM_OVERSHIELD && u->vitals.shield > u->vitals.max_shield) {
                float extra = u->vitals.max_shield * (GAME_OVERSHIELD - 1.0f);
                u->vitals.shield -= extra * (dt / was);
                if (u->vitals.shield < u->vitals.max_shield) u->vitals.shield = u->vitals.max_shield;
            }
            if (u->powerup_timer <= 0.0f) { u->powerup_timer = 0.0f; u->powerup = HTA_ITEM_NONE; }
        }
    }
    /* Empty driver's seats let go of the controls. */
    if (g->vehicles) {
        for (uint32_t i = 0; i < g->vehicles->count; i++) {
            hta_vehicle *c = &g->vehicles->cars[i];
            int32_t d = hta_vehicles_driver_seat(g->vehicles, i);
            if (d < 0 || c->occupant[d] < 0) {
                float yaw = c->ctl.yaw;
                memset(&c->ctl, 0, sizeof(c->ctl));
                c->ctl.yaw = yaw;
            }
        }
        if (g->simulate_vehicles) hta_vehicles_update(g->vehicles, g->col, g->gravity, dt);
        for (uint32_t i = 0; i < g->unit_count; i++)
            if (g->units[i].alive && g->units[i].vehicle >= 0) place_seated(g, (int32_t)i);
        if (g->simulate_vehicles && !g->over) splatter(g);
    }
    fly(g, dt);

    /* Deaths are noticed here, whatever caused them. */
    for (uint32_t i = 0; i < g->unit_count; i++) {
        hta_unit *u = &g->units[i];
        if (u->kind == HTA_UNIT_NONE || !u->alive) continue;
        if (u->vitals.health <= 0.0f) die(g, (int32_t)i);
    }
}

bool hta_game_pop(hta_game *g, hta_game_event *out)
{
    if (!g || !g->event_count) return false;
    *out = g->events[0];
    g->event_count--;
    memmove(&g->events[0], &g->events[1], g->event_count * sizeof(g->events[0]));
    return true;
}

/* ---------------------------------------------------------------- scoreboard */

uint32_t hta_game_standings(const hta_game *g, int32_t *out, uint32_t max)
{
    uint32_t n = 0;
    for (uint32_t i = 0; g && i < g->unit_count && n < max; i++)
        if (g->units[i].kind != HTA_UNIT_NONE) out[n++] = (int32_t)i;
    for (uint32_t i = 1; i < n; i++)
        for (uint32_t j = i; j > 0; j--) {
            const hta_unit *a = &g->units[out[j - 1]], *b = &g->units[out[j]];
            bool swap = b->score > a->score ||
                        (b->score == a->score && b->deaths < a->deaths);
            if (!swap) break;
            int32_t t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    return n;
}

void hta_game_place_text(const hta_game *g, int32_t unit, char *out, size_t outlen)
{
    if (!out || !outlen) return;
    out[0] = 0;
    if (!g || unit < 0) return;
    int32_t order[HTA_GAME_MAX_UNITS];
    uint32_t n = hta_game_standings(g, order, HTA_GAME_MAX_UNITS);
    uint32_t place = 0;
    bool tied = false;
    for (uint32_t i = 0; i < n; i++) {
        if (g->units[order[i]].score > g->units[unit].score) place++;
        else if (order[i] != unit && g->units[order[i]].score == g->units[unit].score) tied = true;
    }
    char fmt[96], ord[16], score[16], unitw[24];
    text(g, tied ? 63 : 64, fmt, sizeof(fmt), tied ? "Tied for %s place with %s %s"
                                                  : "In %s place with %s %s");
    text(g, 36 + (place < 16 ? place : 15), ord, sizeof(ord), "1st");
    snprintf(score, sizeof(score), "%d", g->units[unit].score);
    text(g, 24, unitw, sizeof(unitw), "Frags");
    snprintf(out, outlen, fmt, ord, score, unitw);
}

void hta_game_anim(const hta_game *g, int32_t idx, char *base, size_t baselen,
                   char *action, size_t actlen)
{
    if (action && actlen) action[0] = 0;
    if (!g || idx < 0 || !base) return;
    const hta_unit *u = &g->units[idx];
    const hta_game_weapon *w = hta_game_held(g, idx);
    const char *cls = w ? w->anim_class : "unarmed";
    bool z = w && w->z_prefix;
    bool crouch = u->body.crouch_t > 0.5f;
    const char *stance = crouch ? (z ? "zcrouch" : "crouch") : (z ? "zstand" : "stand");
    /* Walking is judged against where the body faces: Halo has a clip for
     * each of the four directions. */
    float vx = u->body.velocity[0], vy = u->body.velocity[1];
    float speed = hypotf(vx, vy);
    const char *what = "idle";
    if (!u->body.on_ground) what = "airborne";
    else if (speed > 0.25f) {
        float fx = cosf(u->eye.yaw), fy = sinf(u->eye.yaw);
        float f = (vx*fx + vy*fy) / speed, r = (vx*fy - vy*fx) / speed;
        if (fabsf(f) >= fabsf(r)) what = f >= 0.0f ? "move-front" : "move-back";
        else what = r >= 0.0f ? "move-right" : "move-left";
    }
    snprintf(base, baselen, "%s %s %s", stance, cls, what);
    if (!action || !actlen || !w) return;
    const char *label = w->label;
    if (u->fired) snprintf(action, actlen, "%s %s %s fire-1", stance, cls, label);
    else if (u->meleed) snprintf(action, actlen, "%s %s %s melee", stance, cls, label);
    else if (u->threw) snprintf(action, actlen, "%s %s throw-grenade", stance, cls);
}
