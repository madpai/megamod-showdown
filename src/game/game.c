#include "../engine/gore.h"
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
#include <strings.h>

/* How long after a blast a death still counts as the blast's (ours). */
#define HTA_BLAST_KILL_WINDOW 0.25f
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

/* Scenario (1456): netgame flags at +888, 148 bytes each -- the block just
 * before the netgame equipment at +900, so it reconciles. Type 0 is a CTF
 * flag's stand; its `usage id` is the team. */
#define SCN_NETGAME_FLAGS   888u
#define NETFLAG_SIZE        148u
#define NETFLAG_FACING       12u
#define NETFLAG_TYPE         16u
#define NETFLAG_USAGE        18u
#define NETFLAG_CTF           0u

static uint32_t find_tag_path(const hta_cache *c, uint32_t cls, const char *path);
static void drop_flag(hta_game *g, int32_t idx, bool thrown);
static void hulls_reset(hta_game *g);

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
    g->flag_weapon = -1;
    g->winner_team = -1;
    g->simulate_vehicles = true;
    g->simulate_drops = true;
    for (uint32_t t = 0; t < HTA_VEHICLE_TYPES; t++) g->vweapon[t][0] = g->vweapon[t][1] = -1;
    g->score_limit = HTA_SLAYER_SCORE_LIMIT;
    g->respawn_time = HTA_SLAYER_RESPAWN;
    g->spawn_lift = 1.0f;
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
    /* The flag: a weapon Halo puts in your hands, never one you pick off a
     * rack. It has no trigger, so the playable list leaves it out; it
     * joins the roster here for its models and its swing. */
    uint32_t flag_tag = find_tag_path(c, HTA_TAG_WEAP, "weapons\\flag\\flag");
    if (flag_tag && g->weapon_count < HTA_GAME_MAX_WEAPONS) {
        hta_game_weapon *w = &g->weapons[g->weapon_count];
        memset(w, 0, sizeof(*w));
        if (hta_weapon_load_id(c, NULL, flag_tag, &w->def, NULL, perr, sizeof(perr))) {
            w->tag = flag_tag;
            w->pool = -1;
            int32_t ti = hta_cache_find_tag_by_id(c, flag_tag);
            hta_tag_entry t;
            uint32_t base = 0;
            if (ti >= 0 && hta_cache_tag(c, (uint32_t)ti, &t) &&
                hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) {
                read_label(c, base, w->label);
                hta_rd_u32(c, base + WEAP_MELEE_DAMAGE + 12u, &w->melee_jpt);
                uint32_t m = 0;
                hta_rd_u32(c, base + OBJ_MODEL + 12u, &m);
                w->model = (m && m != 0xFFFFFFFFu) ? m : 0u;
            }
            if (w->melee_jpt == 0xFFFFFFFFu) w->melee_jpt = 0;
            w->melee_damage = hta_damage_vs(c, w->melee_jpt, HTA_MATERIAL_CYBORG_ARMOR);
            anim_class(have_graph ? &graph : NULL, w->label, w->anim_class, &w->z_prefix);
            g->flag_weapon = (int32_t)g->weapon_count++;
        }
    }
    if (have_graph) hta_anim_free(&graph);

    /* The flags' stands. */
    {
        int32_t si = hta_cache_find_tag_by_id(c, c->scenario_tag_id);
        hta_tag_entry st;
        uint32_t base = 0, count = 0, ptr = 0, off = 0;
        if (si >= 0 && hta_cache_tag(c, (uint32_t)si, &st) &&
            hta_cache_ptr_to_offset(c, st.tag_data_ptr, &base) &&
            hta_read_reflexive(c, base + SCN_NETGAME_FLAGS, &count, &ptr) && count &&
            hta_cache_ptr_to_offset(c, ptr, &off)) {
            for (uint32_t i = 0; i < count && i < 1024u; i++) {
                uint32_t e = off + i * NETFLAG_SIZE;
                uint16_t type = 0xFFFFu, usage = 0xFFFFu;
                hta_rd_u16(c, e + NETFLAG_TYPE, &type);
                hta_rd_u16(c, e + NETFLAG_USAGE, &usage);
                if (type != NETFLAG_CTF || usage > 1u || g->flags[usage].present) continue;
                hta_game_flag *f = &g->flags[usage];
                for (int k = 0; k < 3; k++) hta_rd_f32(c, e + 4u * (uint32_t)k, &f->home[k]);
                hta_rd_f32(c, e + NETFLAG_FACING, &f->home_yaw);
                f->present = true;
                f->carrier = HTA_GAME_NONE;
            }
        }
    }

    /* What everybody spawns with. */
    for (uint32_t w = 0; w < g->weapon_count; w++) {
        g->weapons[w].asset = NULL;
        g->weapons[w].base = -1;
        g->weapons[w].damage_scale = 1.0f;
        /* The tag's own name: "weapons\\sniper rifle\\sniper rifle". */
        const char *leaf = strrchr(g->weapons[w].def.path, '\\');
        leaf = leaf ? leaf + 1 : g->weapons[w].def.path;
        memset(g->weapons[w].display, 0, sizeof(g->weapons[w].display));
        for (size_t k = 0; leaf[k] && k + 1 < sizeof(g->weapons[w].display); k++)
            g->weapons[w].display[k] = leaf[k] == '_' ? ' ' : leaf[k];
    }
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
                 "grenades %d of %d, flags %d%d%s",
                 g->weapon_count, g->pool_count, g->spawn_count,
                 g->start_grenades, g->max_grenades,
                 g->flags[0].present, g->flags[1].present,
                 g->loaded ? "" : " -- NOT playable");
    return g->loaded;
}

void hta_game_free(hta_game *g)
{
    if (!g) return;
    for (uint32_t i = 0; i < g->pool_count; i++) hta_projectiles_free(&g->pools[i]);
    free(g->stand_field[0]);
    free(g->stand_field[1]);
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
    if (un->flag >= 0 && g->flag_weapon >= 0) return &g->weapons[g->flag_weapon];
    int32_t w = un->carry[un->slot & 1u].weapon;
    return (w >= 0 && (uint32_t)w < g->weapon_count) ? &g->weapons[w] : NULL;
}

/* ---------------------------------------------------------------- units */

bool hta_game_class_weapon(const hta_game *g, int32_t w)
{
    return g && w >= 0 && (uint32_t)w < g->weapon_count && !g->weapons[w].vehicle && w != g->flag_weapon &&
           !g->weapons[w].hidden;
}

void hta_game_set_loadout(hta_game *g, int32_t unit, int32_t a, int32_t b)
{
    if (!g || unit < 0 || unit >= (int32_t)g->unit_count) return;
    g->units[unit].loadout[0] = hta_game_class_weapon(g, a) ? a : -1;
    g->units[unit].loadout[1] = hta_game_class_weapon(g, b) && b != a ? b : -1;
}

static void arm(hta_game *g, hta_unit *u)
{
    int32_t pick[2] = { g->start_weapon[0], g->start_weapon[1] };
    if (g->classes) {
        if(u->character>=0 && (uint32_t)u->character<g->character_count) {
            const hta_oal_asset *hero=g->characters[u->character];
            for(int slot=0;slot<2;slot++) {
                u->loadout[slot]=-1;
                for(uint32_t w=0;w<g->weapon_count;w++)
                    if(hta_game_class_weapon(g,(int32_t)w) && !strcmp(g->weapons[w].display,hero->loadout[slot]))
                        u->loadout[slot]=(int32_t)w;
            }
        }
        if (u->loadout[0] >= 0 || u->loadout[1] >= 0) {
            /* The player's class; an empty slot keeps the map's weapon. */
            for (int s = 0; s < 2; s++) if (u->loadout[s] >= 0) pick[s] = u->loadout[s];
            if (pick[0] == pick[1]) pick[1] = -1;
        } else if (u->kind == HTA_UNIT_BOT) {
            /* A bot makes up a class each life, from everything allowed. */
            int32_t allowed[HTA_GAME_MAX_WEAPONS];
            uint32_t n = 0;
            for (uint32_t w = 0; w < g->weapon_count; w++)
                /* Not a bat yet: a bot would fire it from across the map. */
                if (hta_game_class_weapon(g, (int32_t)w) && !g->weapons[w].melee_only && !g->weapons[w].mount)
                    allowed[n++] = (int32_t)w;
            if (n) {
                u->rng = u->rng * 1664525u + 1013904223u;
                pick[0] = allowed[(u->rng >> 8) % n];
                u->rng = u->rng * 1664525u + 1013904223u;
                pick[1] = n > 1 ? allowed[(u->rng >> 8) % n] : -1;
                if (pick[1] == pick[0]) pick[1] = allowed[((u->rng >> 8) + 1) % n];
                if (n == 1) pick[1] = -1;
            }
        }
    }
    for (int s = 0; s < 2; s++) {
        u->carry[s].weapon = pick[s];
        if (u->carry[s].weapon >= 0)
            hta_ammo_init(&u->carry[s].ammo, &g->weapons[u->carry[s].weapon].def);
        else
            memset(&u->carry[s].ammo, 0, sizeof(u->carry[s].ammo));
    }
    u->slot = 0;
    u->grenades = g->start_grenades;
    u->cooldown = u->error = 0.0f;
    u->swing = u->throwing = 0.0f;
    u->ability_active = 0.0f;
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
    if (team == HTA_TEAM_AUTO) {
        /* The smaller side; red when they are even. */
        int n[2] = { 0, 0 };
        for (uint32_t i = 0; i < g->unit_count; i++)
            if ((int32_t)i != idx && g->units[i].kind != HTA_UNIT_NONE)
                n[g->units[i].team & 1u]++;
        team = g->teams ? (n[1] < n[0] ? HTA_TEAM_BLUE : HTA_TEAM_RED) : HTA_TEAM_RED;
    }
    u->kind = kind;
    u->team = team;
    u->character = -1;
    u->loadout[0] = u->loadout[1] = -1;
    u->flag = -1;
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
    drop_flag(g, idx, false);
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

/* A team game ends: `team` takes it, or nobody (-1) in a draw. */
static void finish_team(hta_game *g, int32_t team)
{
    char buf[96];
    g->over = true;
    g->winner_team = team;
    g->winner = HTA_GAME_NONE;
    /* The team's best player, for anyone who asks who won. */
    for (uint32_t i = 0; team >= 0 && i < g->unit_count; i++) {
        const hta_unit *u = &g->units[i];
        if (u->kind == HTA_UNIT_NONE || u->team != team) continue;
        if (g->winner < 0 || u->score > g->units[g->winner].score) g->winner = (int32_t)i;
    }
    hta_game_event o = { .kind = HTA_EV_GAME_OVER, .a = g->winner, .b = team,
                         .line = HTA_LINE_GAME_OVER, .for_local = true };
    bool mine = g->local >= 0 && g->units[g->local].team == team;
    if (team < 0) text(g, 55, buf, sizeof(buf), "Game ends in a draw");
    else text(g, mine ? 58 : 56, buf, sizeof(buf), mine ? "Your team won" : "Your team lost");
    snprintf(o.text, sizeof(o.text), "%s", buf);
    emit(g, &o);
}

/* A team's score has moved: is that the game? */
static void team_scored(hta_game *g, int team)
{
    if (!g->over && g->score_limit > 0 && g->team_score[team & 1] >= g->score_limit)
        finish_team(g, team & 1);
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
    /* A team plays from its own end: the scenario gives every start a
     * team. Free for all uses the lot. */
    bool own_only = false;
    if (g->teams) {
        for (uint32_t s = 0; s < g->spawn_count; s++)
            if (g->spawns[s].team_index == g->units[idx].team) own_only = true;
    }
    for (uint32_t s = 0; s < g->spawn_count; s++) {
        if (own_only && g->spawns[s].team_index != g->units[idx].team) {
            score[s] = -1e9f;
            continue;
        }
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
    if (g->col && hta_collision_ground(g->col, out_pos[0], out_pos[1], out_pos[2] + g->spawn_lift, &gz))
        out_pos[2] = gz;
    if (out_facing) *out_facing = sp->facing;
}

hta_body_attr hta_game_body(const hta_game *g, int32_t idx)
{
    hta_body_attr b = { 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, 1.0f };
    if (!g || idx < 0 || idx >= (int32_t)g->unit_count) return b;
    const hta_unit *u = &g->units[idx];
    if (u->character < 0 || (uint32_t)u->character >= g->character_count || !g->characters[u->character]) return b;
    const hta_oal_asset *a = g->characters[u->character];
    if (a->body_health > 0.0f) b.health = a->body_health;
    if (a->body_shield >= 0.0f) b.shield = a->body_shield;
    if (a->body_damage > 0.0f) b.damage = a->body_damage;
    if (a->body_speed > 0.0f) b.speed = a->body_speed;
    b.can_fly = a->can_fly;
    b.fly_speed = a->fly_speed > 0.0f ? a->fly_speed : 3.5f;
    if (a->fly_damage > 0.0f) b.fly_damage = a->fly_damage;
    return b;
}

bool hta_game_assign_character(hta_game *g, int32_t idx, int32_t character)
{
    if (!g || idx < 0 || idx >= (int32_t)g->unit_count || character < -1 ||
        character >= (int32_t)g->character_count) return false;
    if (character >= 0 && !g->allow_duplicate_heroes &&
        g->characters[character] && g->characters[character]->unique_limit == 1) {
        for (uint32_t i = 0; i < g->unit_count; i++) {
            if ((int32_t)i == idx || g->units[i].kind == HTA_UNIT_NONE ||
                g->units[i].character != character) continue;
            if (g->units[i].kind != HTA_UNIT_BOT || g->units[idx].kind == HTA_UNIT_BOT)
                return false;
        }
        /* Humans take priority over bot picks; those bots use the Spartan. */
        for (uint32_t i = 0; i < g->unit_count; i++)
            if ((int32_t)i != idx && g->units[i].kind == HTA_UNIT_BOT &&
                g->units[i].character == character) {
                g->units[i].character = -1;
                g->units[i].flying = false;
                if (g->units[i].alive) {
                    hta_game_apply_body(g, (int32_t)i);
                    hta_vitals_reset(&g->units[i].vitals);
                }
            }
    }
    g->units[idx].character = (int8_t)character;
    return true;
}

void hta_game_body_physics(const hta_game *g, float speed, hta_player_physics *out)
{
    *out = g->phys;
    if (!(speed > 0.0f) || speed == 1.0f) return;
    out->run_forward *= speed; out->run_back *= speed; out->run_side *= speed;
    out->sneak_forward *= speed; out->sneak_back *= speed; out->sneak_side *= speed;
    out->run_accel *= speed;
}

void hta_game_apply_body(hta_game *g, int32_t idx)
{
    if (!g || idx < 0 || idx >= (int32_t)g->unit_count) return;
    hta_unit *u = &g->units[idx];
    hta_body_attr b = hta_game_body(g, idx);
    u->vitals.max_health = g->vitals_template.max_health * b.health;
    u->vitals.max_shield = g->vitals_template.max_shield * b.shield;
    hta_player_physics ph;
    hta_game_body_physics(g, b.speed, &ph);
    hta_player_apply_physics(&u->body, &ph);
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
    hta_game_apply_body(g, idx);          /* this character's health, shield, speed */
    hta_vitals_reset(&u->vitals);
    arm(g, u);
    u->flag = -1;
    u->alive = true;
    u->gibbed = false;
    u->protect = g->spawn_protect;
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
    u->gibbed = false;
    u->protect = g->spawn_protect;
    u->flag = -1;
    u->spree = 0;
    u->last_attacker = HTA_GAME_NONE;
    memset(u->attackers, 0, sizeof(u->attackers));
    arm(g, u);
}

bool hta_game_set_mode(hta_game *g, hta_game_mode mode)
{
    if (!g) return false;
    bool ok = true;
    if (mode == HTA_MODE_CTF &&
        (!g->flags[0].present || !g->flags[1].present || g->flag_weapon < 0)) {
        mode = HTA_MODE_SLAYER;
        ok = false;
    }
    if (mode >= HTA_MODE_COUNT) { mode = HTA_MODE_SLAYER; ok = false; }
    g->mode = mode;
    g->teams = mode != HTA_MODE_SLAYER;
    /* The mode's own limit; the caller may set another after. */
    g->score_limit = mode == HTA_MODE_CTF ? HTA_CTF_SCORE_LIMIT : HTA_SLAYER_SCORE_LIMIT;
    return ok;
}

static void flags_home(hta_game *g)
{
    for (int t = 0; t < 2; t++) {
        hta_game_flag *f = &g->flags[t];
        for (int k = 0; k < 3; k++) { f->pos[k] = f->home[k]; f->vel[k] = 0.0f; }
        f->yaw = f->home_yaw;
        f->state = HTA_FLAG_HOME;
        f->carrier = HTA_GAME_NONE;
        f->idle = 0.0f;
        f->rest = true;
    }
    for (uint32_t i = 0; i < g->unit_count; i++) g->units[i].flag = -1;
}

void hta_game_start(hta_game *g)
{
    if (!g) return;
    g->over = false;
    g->winner = HTA_GAME_NONE;
    g->winner_team = -1;
    g->leader = HTA_GAME_NONE;
    g->time = 0.0f;
    g->event_count = 0;
    g->team_score[0] = g->team_score[1] = 0;
    flags_home(g);
    if (g->mode == HTA_MODE_CTF && g->nav && g->nav->built) {
        for (int t = 0; t < 2; t++) {
            if (!g->stand_field[t])
                g->stand_field[t] = malloc((size_t)g->nav->node_count * sizeof(uint32_t));
            g->stand_node[t] = hta_nav_nearest(g->nav, g->flags[t].home, 2.0f);
            if (g->stand_field[t] && g->stand_node[t] != HTA_NAV_NONE)
                hta_nav_field(g->nav, g->stand_node[t], g->stand_field[t]);
            else { free(g->stand_field[t]); g->stand_field[t] = NULL; }
        }
    }
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
    memset(g->drops, 0, sizeof(g->drops));
    for (uint32_t i = 0; i < HTA_VEHICLE_MAX; i++) g->vgun[i].last_driver = -1;
    hulls_reset(g);
    char buf[96];
    hta_game_event e = { .kind = HTA_EV_ANNOUNCE, .a = -1, .b = -1,
                         .line = HTA_LINE_SLAYER, .for_local = true };
    if (g->mode == HTA_MODE_CTF) {
        e.line = HTA_LINE_CTF;
        text(g, 3, buf, sizeof(buf), "Capture the Flag");
    } else {
        if (g->mode == HTA_MODE_TEAM_SLAYER) e.line = HTA_LINE_TEAM_SLAYER;
        text(g, 4, buf, sizeof(buf), "Slayer");
    }
    /* "Red Team" or "Blue Team": which side you are on. */
    if (g->teams && g->local >= 0) {
        char side[32];
        text(g, 17u + (g->units[g->local].team & 1u), side, sizeof(side),
             g->units[g->local].team ? "Blue Team" : "Red Team");
        snprintf(e.text, sizeof(e.text), "%.60s - %.30s", buf, side);
    } else {
        snprintf(e.text, sizeof(e.text), "%s", buf);
    }
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
    /* Both hands full: the one in hand goes on the ground. */
    if (u->carry[s].weapon >= 0 && g->simulate_drops && idx != g->local) {
        float at[3] = { u->body.pos[0], u->body.pos[1], u->body.pos[2] + 0.3f };
        float vel[3] = { cosf(u->eye.yaw) * 0.6f, sinf(u->eye.yaw) * 0.6f, 0.8f };
        hta_game_drop_weapon(g, u->carry[s].weapon, &u->carry[s].ammo, at, u->eye.yaw, vel);
    }
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
    /* Nobody gets hurt in the postgame, nor just after coming back. */
    if (!v->alive || g->over || v->protect > 0.0f) return;
    /* The attacker's body: a Saiyan hits harder; anyone in the air, on a
     * broom or by themselves, hits softer. */
    if (attacker >= 0 && attacker < (int32_t)g->unit_count) {
        hta_body_attr b = hta_game_body(g, attacker);
        amount *= b.damage * (g->units[attacker].riding ? b.fly_damage : 1.0f);
    }
    /* Nor by their own side. Ours: a gametype says, and bots do not check
     * their line of fire, so a team game would be a betrayal a minute. */
    if (g->teams && attacker >= 0 && attacker != victim &&
        attacker < (int32_t)g->unit_count && g->units[attacker].team == v->team) return;
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
    hta_game_hurt_jpt_scaled(g, victim, attacker, jpt, count, at, 1.0f);
}

/* An imported weapon hits as its base weapon's round times its own scale. */
void hta_game_hurt_jpt_scaled(hta_game *g, int32_t victim, int32_t attacker,
                              uint32_t jpt, int count, const float at[3], float scale)
{
    if (!(scale > 0.0f)) scale = 1.0f;
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
        float d = hta_damage_vs(g->cache, jpt, mat) * scale;
        shield -= d;
        total += d;
    }
    hta_game_hurt(g, victim, attacker, total, at);
}

/* ---------------------------------------------------------------- hulls */

static float hull_for(uint16_t kind)
{
    switch (kind) {
    case HTA_VK_TANK:    return HTA_HULL_TANK;
    case HTA_VK_SCOUT:   return HTA_HULL_SCOUT;
    case HTA_VK_FIGHTER: return HTA_HULL_FIGHTER;
    case HTA_VK_TURRET:  return HTA_HULL_TURRET;
    default:             return HTA_HULL_JEEP;
    }
}

static void hulls_reset(hta_game *g)
{
    if (!g->vehicles) return;
    for (uint32_t i = 0; i < g->vehicles->count && i < HTA_VEHICLE_MAX; i++) {
        hta_game_vgun *gun = &g->vgun[i];
        gun->hull = gun->hull_max = hull_for(g->vehicles->cars[i].kind);
        gun->last_hit_by = -1;
        gun->since_hit = 1e9f;
        gun->wreck = 0.0f;
    }
}

float hta_game_hull(const hta_game *g, int32_t car)
{
    if (!g || !g->vehicles || car < 0 || (uint32_t)car >= g->vehicles->count) return 1.0f;
    const hta_game_vgun *gun = &g->vgun[car];
    if (!(gun->hull_max > 0.0f)) return 1.0f;
    return gun->hull > 0.0f ? gun->hull / gun->hull_max : 0.0f;
}

int32_t hta_game_car_at(const hta_game *g, const float pos[3], float pad)
{
    if (!g || !g->vehicles || !pos) return -1;
    const hta_vehicles *v = g->vehicles;
    for (uint32_t i = 0; i < v->count && i < HTA_VEHICLE_MAX; i++) {
        const hta_vehicle *c = &v->cars[i];
        if (!c->active || c->type >= v->type_count) continue;
        const hta_vehicle_type *t = &v->types[c->type];
        float dx = pos[0]-c->pos[0], dy = pos[1]-c->pos[1], dz = pos[2]-c->pos[2];
        if (dx*dx + dy*dy + dz*dz > (t->coll_radius + pad) * (t->coll_radius + pad)) continue;
        /* Into the car's own space, and inside its collision box? */
        hta_transform w, inv;
        hta_vehicles_world(v, i, &w);
        hta_xf_inverse(&inv, &w);
        float l[3];
        hta_xf_point(l, &inv, pos);
        bool in = true;
        for (int k = 0; k < 3; k++)
            if (l[k] < t->coll_mesh.bounds_min[k] - pad || l[k] > t->coll_mesh.bounds_max[k] + pad)
                in = false;
        if (in) return (int32_t)i;
    }
    return -1;
}

/* Would this attacker's hit on this car count? Friendly fire is off in a
 * team game: a car carrying the attacker's side is theirs. */
static bool hull_hostile(const hta_game *g, uint32_t car, int32_t attacker)
{
    if (!g->teams || attacker < 0 || attacker >= (int32_t)g->unit_count) return true;
    const hta_vehicle *c = &g->vehicles->cars[car];
    for (uint32_t s = 0; s < HTA_VEHICLE_SEATS; s++) {
        int32_t o = c->occupant[s];
        if (o >= 0 && o != attacker && o < (int32_t)g->unit_count &&
            g->units[o].team == g->units[attacker].team) return false;
    }
    return true;
}

static void wreck(hta_game *g, uint32_t car, int32_t attacker)
{
    hta_vehicles *v = g->vehicles;
    hta_vehicle *c = &v->cars[car];
    hta_game_vgun *gun = &g->vgun[car];
    float at[3] = { c->pos[0], c->pos[1], c->pos[2] + 0.3f };
    /* Whoever last hurt it gets the kills, if it was recent. */
    int32_t by = attacker >= 0 ? attacker
               : (gun->since_hit < HTA_CREDIT_WINDOW ? gun->last_hit_by : -1);
    hta_game_event e = { .kind = HTA_EV_WRECK, .a = by, .b = (int32_t)car };
    for (int k = 0; k < 3; k++) e.pos[k] = at[k];
    e.dir[2] = 1.0f;
    emit(g, &e);
    /* Nobody rides out of that. */
    for (uint32_t s = 0; s < HTA_VEHICLE_SEATS; s++) {
        int32_t o = c->occupant[s];
        if (o < 0 || o >= (int32_t)g->unit_count || !g->units[o].alive) continue;
        hta_unit *u = &g->units[o];
        float ctr[3];
        hta_game_centre(g, o, ctr);
        /* Past friendly fire: the wreck did it, credited to who wrecked it. */
        hta_vitals_damage(&u->vitals, 100000.0f);
        u->hurt = true;
        if (by >= 0 && by < (int32_t)g->unit_count) {
            u->last_attacker = by;
            u->since_attacked = 0.0f;
            if (by != o) u->attackers[by] = 1;
        }
        (void)ctr;
    }
    gun->hull = 0.0f;
    gun->wreck = HTA_VEHICLE_WRECK_TIME;
    c->active = false;
    /* Riders out now, so nothing reads a seat in a car that is gone. */
    for (uint32_t i = 0; i < g->unit_count; i++)
        if (g->units[i].vehicle == (int16_t)car) hta_game_unseat(g, (int32_t)i);
    hta_vehicles_sync(v);
    /* And the fireball hurts everyone around it. */
    if (g->wreck_damage > 0.0f)
        hta_game_blast(g, by, at, g->wreck_damage, g->wreck_core, g->wreck_radius);
}

void hta_game_hurt_car(hta_game *g, int32_t car, int32_t attacker, float damage,
                       const float at[3])
{
    (void)at;
    if (!g || !g->vehicles || g->over || car < 0 || (uint32_t)car >= g->vehicles->count ||
        car >= (int32_t)HTA_VEHICLE_MAX || !(damage > 0.0f)) return;
    if (!g->simulate_vehicles) return;
    hta_vehicle *c = &g->vehicles->cars[car];
    hta_game_vgun *gun = &g->vgun[car];
    if (!c->active || gun->wreck > 0.0f || !hull_hostile(g, (uint32_t)car, attacker)) return;
    if (!(gun->hull_max > 0.0f)) gun->hull = gun->hull_max = hull_for(c->kind);
    gun->hull -= damage;
    if (attacker >= 0) { gun->last_hit_by = attacker; gun->since_hit = 0.0f; }
    if (gun->hull <= 0.0f) wreck(g, (uint32_t)car, attacker);
}

void hta_game_hurt_car_jpt(hta_game *g, int32_t car, int32_t attacker, uint32_t jpt,
                           int count, const float at[3])
{
    if (!g || !jpt || car < 0) return;
    if (count < 1) count = 1;
    hta_game_hurt_car(g, car, attacker,
                      hta_damage_vs(g->cache, jpt, HTA_HULL_MATERIAL) * (float)count, at);
}

/* A destroyed car comes home after a while. */
static void hulls_tick(hta_game *g, float dt)
{
    if (!g->vehicles || !g->simulate_vehicles) return;
    for (uint32_t i = 0; i < g->vehicles->count && i < HTA_VEHICLE_MAX; i++) {
        hta_game_vgun *gun = &g->vgun[i];
        gun->since_hit += dt;
        if (gun->wreck <= 0.0f) continue;
        gun->wreck -= dt;
        if (gun->wreck > 0.0f) continue;
        gun->wreck = 0.0f;
        hta_vehicles_reset(g->vehicles, i);
        g->vehicles->cars[i].active = true;
        gun->hull = gun->hull_max = hull_for(g->vehicles->cars[i].kind);
        gun->last_hit_by = -1;
        hta_vehicles_sync(g->vehicles);
    }
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
        u->blast_hit = damage * f;
        memcpy(u->blast_at, centre, sizeof(u->blast_at));
        u->since_blast = 0.0f;
        hta_game_hurt(g, (int32_t)i, attacker, damage * f, c);
    }
    /* Hulls: hurt, and thrown. Nearest point of the hull counts, roughly
     * its centre less its radius. */
    if (!g->vehicles || !g->simulate_vehicles) return;
    for (uint32_t i = 0; i < g->vehicles->count && i < HTA_VEHICLE_MAX; i++) {
        hta_vehicle *c = &g->vehicles->cars[i];
        if (!c->active || g->vgun[i].wreck > 0.0f) continue;
        float d[3] = { c->pos[0]-centre[0], c->pos[1]-centre[1], c->pos[2]+0.3f-centre[2] };
        float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        float reach = dist - c->body_radius * 0.6f;
        if (reach < 0.0f) reach = 0.0f;
        if (reach >= radius) continue;
        float f = 1.0f;
        if (reach > core && radius > core) f = 1.0f - (reach - core) / (radius - core);
        if (f <= 0.0f) continue;
        /* Thrown away from it and up: the lighter the harder. */
        float mass = c->mass > 1.0f ? c->mass : 5000.0f;
        float k = damage * f * HTA_BLAST_PUSH * 5000.0f / mass;
        float n[3] = { 0, 0, 1 };
        if (dist > 1e-3f) for (int m = 0; m < 3; m++) n[m] = d[m] / dist;
        float kick[3] = { n[0] * k, n[1] * k, (fabsf(n[2]) * 0.5f + 0.5f) * k };
        hta_vehicles_push(g->vehicles, i, kick, k * 0.6f, 0.02f * k);
        hta_game_hurt_car(g, (int32_t)i, attacker, damage * f, centre);
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
    /* A flag goes down where its carrier does. */
    drop_flag(g, idx, false);
    /* The dead drop what they held, as it was. */
    if (g->simulate_drops) {
        hta_carried *c = &v->carry[v->slot & 1u];
        if (c->weapon >= 0) {
            float at[3] = { v->body.pos[0], v->body.pos[1], v->body.pos[2] + 0.4f };
            float vel[3] = { v->body.velocity[0] * 0.5f, v->body.velocity[1] * 0.5f, 0.5f };
            hta_game_drop_weapon(g, c->weapon, &c->ammo, at, v->eye.yaw, vel);
        }
    }
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
    /* Blown up, or shot? A blast this tick or the last few says blown up;
     * how much of a whole body's worth it did says how hard. */
    if (v->since_blast <= HTA_BLAST_KILL_WINDOW && v->blast_hit > 0.0f) {
        float full = v->vitals.max_health + v->vitals.max_shield;
        e.amount = full > 0.0f ? v->blast_hit / full : 1.0f;
        memcpy(e.dir, v->blast_at, sizeof(e.dir));
        v->gibbed = g->gore > 0 && hta_gibs_should(e.amount, NULL);
    }
    v->blast_hit = 0.0f;
    if (killer >= 0 && killer != idx && g->units[killer].kind != HTA_UNIT_NONE) {
        hta_unit *k = &g->units[killer];
        e.weapon = k->carry[k->slot & 1u].weapon;
        if (k->vehicle >= 0 && g->vehicles) {
            uint16_t type = g->vehicles->cars[k->vehicle].type;
            if (type < HTA_VEHICLE_TYPES && g->vweapon[type][0] >= 0) e.weapon = g->vweapon[type][0];
        }
        bool betrayal = g->teams && k->team == v->team;
        /* Kills are the score in Slayer; in CTF only captures are. */
        bool slaying = g->mode != HTA_MODE_CTF;
        if (betrayal) {
            k->betrayals++;
            if (slaying) k->score--;
            if (g->mode == HTA_MODE_TEAM_SLAYER) g->team_score[k->team & 1u]--;
            text(g, 79, fmt, sizeof(fmt), "%s was betrayed by %s");
        } else {
            k->kills++;
            if (slaying) k->score++;
            if (g->mode == HTA_MODE_TEAM_SLAYER) g->team_score[k->team & 1u]++;
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
        if (g->mode != HTA_MODE_CTF) v->score--;
        if (g->mode == HTA_MODE_TEAM_SLAYER) g->team_score[v->team & 1u]--;
        if (killer == idx) text(g, 81, fmt, sizeof(fmt), "%s committed suicide");
        else if (by_vehicle) text(g, 77, fmt, sizeof(fmt), "%s was killed by a vehicle");
        else text(g, 75, fmt, sizeof(fmt), "%s died");
        snprintf(e.text, sizeof(e.text), fmt, v->name);
        emit(g, &e);
    }
    v->spree = 0;
    v->multi = 0;

    /* The game is over when somebody reaches the limit. */
    if (g->mode == HTA_MODE_TEAM_SLAYER && killer >= 0)
        team_scored(g, g->units[killer].team);
    else if (g->mode == HTA_MODE_SLAYER && !g->over && g->score_limit > 0 && killer >= 0 &&
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

/* Where a unit is best aimed at: its chest, or the hull it hides in. */
static void target_point(const hta_game *g, int32_t i, float out[3], float vel[3])
{
    const hta_unit *u = &g->units[i];
    hta_game_centre(g, i, out);
    for (int k = 0; k < 3; k++) vel[k] = u->body.velocity[k];
    if (u->vehicle >= 0 && g->vehicles && (uint32_t)u->vehicle < g->vehicles->count &&
        hta_game_enclosed(g, i)) {
        const hta_vehicle *c = &g->vehicles->cars[u->vehicle];
        out[0] = c->pos[0]; out[1] = c->pos[1]; out[2] = c->pos[2] + 0.3f;
        vel[0] = cosf(c->yaw) * c->speed + c->lateral_vel[0];
        vel[1] = sinf(c->yaw) * c->speed + c->lateral_vel[1];
        vel[2] = c->kind == HTA_VK_FIGHTER ? c->fall_speed : 0.0f;
    }
}

int32_t hta_game_aim_target(const hta_game *g, int32_t unit, int32_t weapon,
                            const float eye[3], const float dir[3], float out_point[3])
{
    if (!g || !eye || !dir) return -1;
    const hta_game_weapon *w = weapon >= 0 && weapon < (int32_t)g->weapon_count
                             ? &g->weapons[weapon] : NULL;
    if (!w || !(w->autoaim_range > 0.0f)) return -1;
    float range = w->autoaim_range, cone_half = w->autoaim_angle;
    float speed = w->travels ? w->speed : 0.0f;
    int32_t own_car = unit >= 0 && unit < (int32_t)g->unit_count ? g->units[unit].vehicle : -1;
    int32_t best = -1;
    float best_off = 1e9f, best_pt[3] = { 0, 0, 0 };
    for (uint32_t i = 0; i < g->unit_count; i++) {
        const hta_unit *u = &g->units[i];
        if ((int32_t)i == unit || !u->alive || u->kind == HTA_UNIT_NONE) continue;
        if (unit >= 0 && unit < (int32_t)g->unit_count && !enemies(g, unit, (int32_t)i)) continue;
        if (u->vehicle >= 0 && u->vehicle == own_car) continue;
        float at[3], vel[3];
        target_point(g, (int32_t)i, at, vel);
        float d[3] = { at[0]-eye[0], at[1]-eye[1], at[2]-eye[2] };
        float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        if (dist > range || dist < 0.2f) continue;
        float c = (d[0]*dir[0] + d[1]*dir[1] + d[2]*dir[2]) / dist;
        float off = acosf(c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c);
        /* The body's own width counts: the crosshair on its shoulder is on it. */
        float r = u->vehicle >= 0 && hta_game_enclosed(g, (int32_t)i) && g->vehicles
                ? g->vehicles->cars[u->vehicle].body_radius * 0.6f
                : (u->body.phys.radius > 0.0f ? u->body.phys.radius : 0.175f);
        off -= atanf(r / dist);
        if (off > cone_half || off >= best_off) continue;
        /* Something solid in the way that is not their ride or ours? */
        if (g->col) {
            float t, hit[3], n[3] = { d[0]/dist, d[1]/dist, d[2]/dist };
            if (hta_collision_ray(g->col, eye, n, dist - 0.3f, &t, hit, NULL)) {
                int32_t struck = hta_game_car_at(g, hit, 0.1f);
                if (struck < 0 || (struck != u->vehicle && struck != own_car)) continue;
            }
        }
        best = (int32_t)i;
        best_off = off;
        /* A round that flies is aimed where they will be. */
        float lead = speed > 1.0f ? dist / speed : 0.0f;
        if (lead > 1.5f) lead = 1.5f;
        for (int k = 0; k < 3; k++) best_pt[k] = at[k] + vel[k] * lead;
    }
    if (best >= 0 && out_point) memcpy(out_point, best_pt, sizeof(best_pt));
    return best;
}

/* A human's round bends toward what the weapon's autoaim has found (bots
 * aim for themselves). */
static void assist(const hta_game *g, int32_t idx, int32_t weapon, const float from[3],
                   float aim[3])
{
    const hta_unit *u = &g->units[idx];
    if (u->kind == HTA_UNIT_BOT) return;
    float pt[3], dir[3];
    aim_dir(&u->eye, dir);
    if (hta_game_aim_target(g, idx, weapon, u->eye.pos, dir, pt) < 0) return;
    float d[3] = { pt[0]-from[0], pt[1]-from[1], pt[2]-from[2] };
    float l = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (l > 1e-3f) for (int k = 0; k < 3; k++) aim[k] = d[k] / l;
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

static void shoot(hta_game *g, int32_t idx, int32_t wi, float spread);

/* A single line of heat vision stops at solid world, but crosses every body
 * along it. One event draws the whole line; individual hurt events give each
 * victim their own damage and kill credit. */
static void beam(hta_game *g, int32_t idx, int32_t wi, float damage)
{
    hta_unit *u = &g->units[idx];
    float dir[3], from[3];
    aim_dir(&u->eye, dir);
    for (int k = 0; k < 3; k++) from[k] = u->eye.pos[k] + dir[k] * 0.18f;
    float reach = 100.0f;
    if (g->col) hta_collision_ray(g->col, from, dir, reach, &reach, NULL, NULL);
    hta_game_event e = { .kind = HTA_EV_FIRE, .a = idx, .b = -1, .weapon = wi, .amount = reach };
    for (int k = 0; k < 3; k++) { e.pos[k] = from[k]; e.dir[k] = dir[k]; }
    emit(g, &e);
    for (uint32_t i = 0; i < g->unit_count; i++) {
        if ((int32_t)i == idx || !g->units[i].alive) continue;
        hta_unit *v = &g->units[i];
        if (g->teams && v->team == u->team) continue;
        float centre[3] = { v->body.pos[0], v->body.pos[1],
                            v->body.pos[2] + v->body.phys.coll_stand * 0.55f };
        float delta[3] = { centre[0]-from[0], centre[1]-from[1], centre[2]-from[2] };
        float along = delta[0]*dir[0] + delta[1]*dir[1] + delta[2]*dir[2];
        if (along < 0.0f || along > reach) continue;
        float side2 = 0.0f;
        for (int k = 0; k < 3; k++) { float d = delta[k]-dir[k]*along; side2 += d*d; }
        float radius = v->body.phys.radius + 0.48f;
        if (side2 > radius*radius) continue;
        hta_game_hurt(g, (int32_t)i, idx, damage, centre);
    }
}

/* A shockwave respects walls and teams; a cone is a directional shout. */
static void hero_pulse(hta_game *g, int32_t idx, int32_t wi, const hta_oal_asset *a)
{
    hta_unit *u = &g->units[idx];
    float from[3], dir[3];
    hta_game_centre(g, idx, from); aim_dir(&u->eye, dir);
    hta_game_event e = { .kind=HTA_EV_FIRE, .a=idx, .b=-1, .weapon=wi, .amount=a->ability_radius };
    memcpy(e.pos, from, sizeof(from)); memcpy(e.dir, dir, sizeof(dir)); emit(g, &e);
    for (uint32_t i=0; i<g->unit_count; i++) {
        if ((int32_t)i==idx || !g->units[i].alive ||
            (g->mode!=HTA_MODE_SLAYER && g->units[i].team==u->team)) continue;
        float at[3], d[3]; hta_game_centre(g, (int32_t)i, at);
        for (int k=0;k<3;k++) d[k]=at[k]-from[k];
        float dist=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if (dist<0.001f || dist>a->ability_radius) continue;
        for (int k=0;k<3;k++) d[k]/=dist;
        if (a->ability_cone>0.0f && d[0]*dir[0]+d[1]*dir[1]+d[2]*dir[2]<a->ability_cone) continue;
        float wall=dist;
        if (g->col && hta_collision_ray(g->col,from,d,dist,&wall,NULL,NULL) && wall<dist-0.08f) continue;
        float power=1.0f-0.5f*dist/a->ability_radius;
        hta_game_hurt(g,(int32_t)i,idx,a->ability_damage*power,at);
        for (int k=0;k<3;k++) g->units[i].knock[k]+=d[k]*a->ability_force*power;
        g->units[i].knock[2]+=a->ability_force*0.3f;
    }
}

static void hero_shot(hta_game *g, int32_t idx)
{
    hta_unit *u=&g->units[idx];
    if (u->character<0 || (uint32_t)u->character>=g->character_count) return;
    int32_t wi=g->char_ability[u->character];
    if (wi<0) return;
    const hta_oal_asset *a=g->characters[u->character];
    if (a->ability_radius>0.0f) hero_pulse(g,idx,wi,a);
    else if (g->weapons[wi].beam) beam(g,idx,wi,a->ability_damage>0.0f?a->ability_damage:250.0f);
    else shoot(g,idx,wi,g->weapons[wi].def.error_angle[0]);
}

bool hta_game_ability(hta_game *g, int32_t idx)
{
    if (!g || idx<0 || idx>=(int32_t)g->unit_count || g->over) return false;
    hta_unit *u=&g->units[idx];
    if (!u->alive || u->ability_cool>0.0f || u->character<0 ||
        (uint32_t)u->character>=g->character_count || g->char_ability[u->character]<0) return false;
    const hta_oal_asset *a=g->characters[u->character];
    u->ability_cool=a->ability_cooldown>0.0f?a->ability_cooldown:6.0f;
    u->ability_active=fminf(a->ability_duration,4.0f);
    u->ability_tick=a->ability_interval>0.0f?fmaxf(a->ability_interval,0.10f):0.15f;
    u->fired=true;
    u->protect=0.0f;
    hero_shot(g,idx);
    return true;
}

float hta_game_ability_charge(const hta_game *g, int32_t idx)
{
    if (!g || idx < 0 || idx >= (int32_t)g->unit_count) return -1.0f;
    const hta_unit *u = &g->units[idx];
    if (u->character < 0 || (uint32_t)u->character >= g->character_count || g->char_ability[u->character] < 0)
        return -1.0f;
    const hta_oal_asset *a = g->characters[u->character];
    float cd = a->ability_cooldown > 0.0f ? a->ability_cooldown : 6.0f;
    float f = 1.0f - u->ability_cool / cd;
    return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
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
    shoot(g, idx, c->weapon, spread);
}

/* One shot of roster weapon `wi` from the unit's eyes along its aim: the
 * event, then projectiles or hitscan. No ammunition, no cooldown: fire()
 * has spent those; an ability pays in its own cooldown. */
static void shoot(hta_game *g, int32_t idx, int32_t wi, float spread)
{
    hta_unit *u = &g->units[idx];
    const hta_game_weapon *w = &g->weapons[wi];
    float aim[3];
    aim_dir(&u->eye, aim);
    float muzzle[3];
    for (int k = 0; k < 3; k++) muzzle[k] = u->eye.pos[k] + aim[k] * 0.3f;
    assist(g, idx, wi, u->eye.pos, aim);

    hta_game_event fe = { .kind = HTA_EV_FIRE, .a = idx, .b = -1, .weapon = wi };
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
                g->pool_scale[w->pool][slot] = w->damage_scale > 0.0f ? w->damage_scale : 1.0f;
            }
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
                                 .weapon = wi, .material = mat };
            for (int k = 0; k < 3; k++) { e.pos[k] = wh[k]; e.dir[k] = wn[k]; }
            emit(g, &e);
            hta_game_hurt_car_jpt(g, hta_game_car_at(g, wh, 0.1f), idx, w->impact_jpt, 1, wh);
        }
    }
    for (uint32_t v = 0; v < g->unit_count; v++)
        if (hits[v]) hta_game_hurt_jpt_scaled(g, (int32_t)v, idx, w->impact_jpt, hits[v], hit_at[v], w->damage_scale);
}

int32_t hta_game_melee(hta_game *g, int32_t idx)
{
    if (!g || idx < 0 || idx >= (int32_t)g->unit_count) return -1;
    hta_unit *u = &g->units[idx];
    const hta_game_weapon *w = hta_game_held(g, idx);
    u->meleed = true;
    float fwd[3];
    aim_dir(&u->eye, fwd);
    float range = w && w->melee_only ? 1.15f : HTA_GAME_MELEE_REACH;
    float reach[3];
    for (int k = 0; k < 3; k++)
        reach[k] = u->eye.pos[k] - u->body.eye_height * 0.4f * (k == 2) + fwd[k] * range;
    int32_t who = hta_game_near(g, reach, range, idx);
    if (who >= 0 && g->col) {
        float at[3], ray[3]; hta_game_centre(g, who, at);
        float len=0.0f;
        for(int k=0;k<3;k++) { ray[k]=at[k]-u->eye.pos[k]; len+=ray[k]*ray[k]; }
        len=sqrtf(len);
        if(len>0.001f) {
            for(int k=0;k<3;k++) ray[k]/=len;
            float hit=len;
            if(hta_collision_ray(g->col,u->eye.pos,ray,len,&hit,NULL,NULL) && hit<len-0.08f) who=-1;
        }
    }
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
        /* A melee weapon's blow is its own damage; a gun's butt is the base's. */
        hta_game_hurt_jpt_scaled(g, who, idx, w->melee_jpt, 1, c, w->melee_only ? w->damage_scale : 1.0f);
        if (w->knockback > 0.0f) {
            /* Fists: the blow throws them back, and a little up. */
            hta_unit *vk = &g->units[who];
            float dx = vk->body.pos[0] - u->body.pos[0], dy = vk->body.pos[1] - u->body.pos[1];
            float dl = hypotf(dx, dy);
            if (dl < 1e-4f) { dx = cosf(u->eye.yaw); dy = sinf(u->eye.yaw); dl = 1.0f; }
            vk->knock[0] += dx / dl * w->knockback;
            vk->knock[1] += dy / dl * w->knockback;
            vk->knock[2] += w->knockback * 0.35f;
        }
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
    if (slot >= 0) {
        g->pool_owner[g->grenade_pool][slot] = (int8_t)idx;
        g->pool_scale[g->grenade_pool][slot] = 1.0f;
    }
    u->grenades--;
    u->threw = true;
    hta_game_event e = { .kind = HTA_EV_GRENADE, .a = idx, .b = -1 };
    for (int k = 0; k < 3; k++) e.pos[k] = at[k];
    emit(g, &e);
}

/* ---------------------------------------------------------------- items */

static int same_gun(const hta_game *g, int32_t a, int32_t b)
{
    if (a == b) return 1;
    if (!g || a < 0 || b < 0 || (uint32_t)a >= g->weapon_count || (uint32_t)b >= g->weapon_count) return 0;
    const char *da = g->weapons[a].display, *db = g->weapons[b].display;
    return da[0] && db[0] && !strcasecmp(da, db);
}

static bool take_drops(hta_game *g, int32_t idx)
{
    hta_unit *u = &g->units[idx];
    /* A dropped weapon: its ammo if you carry one, the gun if you ask. */
    int32_t dr = hta_game_drop_near(g, u->body.pos, HTA_DROP_REACH);
    if (dr >= 0) {
        hta_game_drop *d = &g->drops[dr];
        for (int s = 0; s < 2; s++) {
            if (!same_gun(g, u->carry[s].weapon, d->weapon)) continue;
            hta_ammo *a = &u->carry[s].ammo;
            if (a->reserve >= a->reserve_max) break;
            a->reserve += d->ammo.loaded + d->ammo.reserve;
            if (a->reserve > a->reserve_max) a->reserve = a->reserve_max;
            hta_game_event e = { .kind = HTA_EV_PICKUP, .a = idx, .b = -1,
                                 .tag = g->weapons[d->weapon].tag };
            for (int k = 0; k < 3; k++) e.pos[k] = u->body.pos[k];
            emit(g, &e);
            d->live = false;
            return true;
        }
        if (d->live && u->in.pickup && u->carry[0].weapon != d->weapon &&
            u->carry[1].weapon != d->weapon) {
            int32_t wi;
            hta_ammo am;
            hta_game_take_drop(g, dr, &wi, &am);
            hta_game_give(g, idx, wi, &am);
            hta_game_event e = { .kind = HTA_EV_PICKUP, .a = idx, .b = -1,
                                 .tag = g->weapons[wi].tag };
            for (int k = 0; k < 3; k++) e.pos[k] = u->body.pos[k];
            emit(g, &e);
            return true;
        }
    }
    return false;
}

static void take_items(hta_game *g, int32_t idx)
{
    hta_unit *u = &g->units[idx];
    if (take_drops(g, idx)) return;
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

/* ---------------------------------------------------------------- sensor */

uint32_t hta_game_sensor(const hta_game *g, int32_t viewer, hta_game_contact *out, uint32_t max)
{
    if (!g || viewer < 0 || viewer >= (int32_t)g->unit_count || !out) return 0;
    const hta_unit *me = &g->units[viewer];
    float fx = cosf(me->eye.yaw), fy = sinf(me->eye.yaw);
    uint32_t n = 0;
    bool car_shown[HTA_VEHICLE_MAX] = { false };
    for (uint32_t i = 0; i < g->unit_count && n < max; i++) {
        const hta_unit *u = &g->units[i];
        if ((int32_t)i == viewer || !u->alive || u->kind == HTA_UNIT_NONE) continue;
        float dx = u->body.pos[0] - me->body.pos[0], dy = u->body.pos[1] - me->body.pos[1];
        if (dx * dx + dy * dy > HTA_MOTION_RANGE * HTA_MOTION_RANGE) continue;
        float speed = hypotf(u->body.velocity[0], u->body.velocity[1]);
        bool moving = speed > HTA_MOTION_SPEED && (u->body.crouch_t < 0.5f || u->vehicle >= 0);
        bool shot = u->since_shot < HTA_MOTION_FIRE && u->kind != HTA_UNIT_LOCAL;
        if (!moving && !shot) continue;
        /* A vehicle is one big contact, whoever is in it. */
        if (u->vehicle >= 0 && u->vehicle < (int32_t)HTA_VEHICLE_MAX) {
            if (car_shown[u->vehicle]) continue;
            car_shown[u->vehicle] = true;
        }
        hta_game_contact *c = &out[n++];
        c->x = (dx * fy - dy * fx) / HTA_MOTION_RANGE;
        c->y = (dx * fx + dy * fy) / HTA_MOTION_RANGE;
        c->friendly = g->teams && u->team == me->team;
        c->vehicle = u->vehicle >= 0;
    }
    return n;
}

/* ---------------------------------------------------------------- drops */

int32_t hta_game_drop_weapon(hta_game *g, int32_t weapon, const hta_ammo *ammo,
                             const float pos[3], float yaw, const float vel[3])
{
    if (!g || weapon < 0 || (uint32_t)weapon >= g->weapon_count || !pos ||
        g->weapons[weapon].vehicle) return -1;
    int32_t slot = -1;
    float oldest = -1.0f;
    for (int32_t i = 0; i < HTA_GAME_MAX_DROPS; i++) {
        if (!g->drops[i].live) { slot = i; break; }
        if (g->drops[i].age > oldest) { oldest = g->drops[i].age; slot = i; }
    }
    hta_game_drop *d = &g->drops[slot];
    memset(d, 0, sizeof(*d));
    d->live = true;
    d->weapon = weapon;
    if (ammo) d->ammo = *ammo;
    else hta_ammo_init(&d->ammo, &g->weapons[weapon].def);
    /* A weapon put down is not mid-reload. */
    hta_ammo_cancel_reload(&d->ammo);
    for (int k = 0; k < 3; k++) { d->pos[k] = pos[k]; d->vel[k] = vel ? vel[k] : 0.0f; }
    d->yaw = yaw;
    return slot;
}

int32_t hta_game_drop_near(const hta_game *g, const float feet[3], float reach)
{
    if (!g || !feet) return -1;
    int32_t best = -1;
    float bd = reach;
    for (int32_t i = 0; i < HTA_GAME_MAX_DROPS; i++) {
        const hta_game_drop *d = &g->drops[i];
        if (!d->live) continue;
        float dz = fabsf(d->pos[2] - feet[2]);
        if (dz > 0.8f) continue;
        float dist = hypotf(d->pos[0] - feet[0], d->pos[1] - feet[1]);
        if (dist < bd) { bd = dist; best = i; }
    }
    return best;
}

bool hta_game_take_drop(hta_game *g, int32_t drop, int32_t *weapon, hta_ammo *ammo)
{
    if (!g || drop < 0 || drop >= HTA_GAME_MAX_DROPS || !g->drops[drop].live) return false;
    if (weapon) *weapon = g->drops[drop].weapon;
    if (ammo) *ammo = g->drops[drop].ammo;
    g->drops[drop].live = false;
    return true;
}

/* Weapons fall where they are put and lie still once they land. */
/* Something let go of: it falls, glances off walls and comes to rest on
 * the first floor it meets. False once it has fallen out of the world. */
static bool fall(const hta_game *g, float pos[3], float vel[3], bool *rest, float dt)
{
    if (*rest || !g->col) return true;
    vel[2] -= g->gravity * dt;
    float step[3] = { vel[0] * dt, vel[1] * dt, vel[2] * dt };
    float len = sqrtf(step[0]*step[0] + step[1]*step[1] + step[2]*step[2]);
    float t;
    if (len > 1e-5f) {
        float dir[3] = { step[0]/len, step[1]/len, step[2]/len };
        float nrm[3];
        if (hta_collision_ray(g->col, pos, dir, len + 0.03f, &t, NULL, nrm)) {
            for (int k = 0; k < 3; k++) pos[k] += dir[k] * fmaxf(0.0f, t - 0.03f);
            if (nrm[2] > 0.6f) { *rest = true; memset(vel, 0, 3 * sizeof(float)); }
            else {
                float vn = vel[0]*nrm[0] + vel[1]*nrm[1] + vel[2]*nrm[2];
                for (int k = 0; k < 3; k++) vel[k] = (vel[k] - 2.0f * vn * nrm[k]) * 0.3f;
            }
            return true;
        }
        for (int k = 0; k < 3; k++) pos[k] += step[k];
    }
    float gz;
    if (hta_collision_ground(g->col, pos[0], pos[1], pos[2] + 0.05f, &gz) &&
        pos[2] <= gz + 0.02f) {
        pos[2] = gz + 0.02f;
        *rest = true;
        memset(vel, 0, 3 * sizeof(float));
    }
    return pos[2] >= -200.0f;
}

static void drops_update(hta_game *g, float dt)
{
    for (int32_t i = 0; i < HTA_GAME_MAX_DROPS; i++) {
        hta_game_drop *d = &g->drops[i];
        if (!d->live) continue;
        d->age += dt;
        if (d->age >= HTA_DROP_LIFE) { d->live = false; continue; }
        if (!fall(g, d->pos, d->vel, &d->rest, dt)) d->live = false;
    }
}

/* ---------------------------------------------------------------- the flag */

/* Tell everyone, in the words the Trial uses for this device's player. */
static void flag_event(hta_game *g, int32_t unit, int team, hta_flag_event what)
{
    team &= 1;
    hta_game_event e = { .kind = HTA_EV_FLAG, .a = unit, .b = team, .pool = (int32_t)what,
                         .for_local = true };
    for (int k = 0; k < 3; k++) e.pos[k] = g->flags[team].pos[k];
    int mine = g->local >= 0 ? g->units[g->local].team : -1;
    bool me = unit >= 0 && unit == g->local;
    bool ally = unit >= 0 && g->units[unit].team == mine;
    const char *who = unit >= 0 ? g->units[unit].name : "";
    char fmt[96];
    fmt[0] = 0;
    switch (what) {
    case HTA_FLAG_TAKEN:
        /* Named for the side that now holds it. */
        e.line = team == HTA_TEAM_BLUE ? HTA_LINE_RED_HAS_FLAG : HTA_LINE_BLUE_HAS_FLAG;
        if (!me) text(g, team == mine ? 145 : 147, fmt, sizeof(fmt),
                      team == mine ? "The enemy has your flag." : "Your ally has the flag.");
        break;
    case HTA_FLAG_RETURN:
        e.line = team == HTA_TEAM_RED ? HTA_LINE_RED_RETURNED : HTA_LINE_BLUE_RETURNED;
        if (unit < 0)
            text(g, team == mine ? 149 : 150, fmt, sizeof(fmt),
                 team == mine ? "Your flag was returned." : "The enemy's flag was returned.");
        else if (me) text(g, 144, fmt, sizeof(fmt), "You returned the flag.");
        else text(g, ally ? 148 : 146, fmt, sizeof(fmt),
                  ally ? "Your ally returned the flag." : "The enemy returned the flag.");
        break;
    case HTA_FLAG_CAPTURE:
        e.line = team == HTA_TEAM_BLUE ? HTA_LINE_RED_SCORE : HTA_LINE_BLUE_SCORE;
        if (me) text(g, 167, fmt, sizeof(fmt), "You scored a flag!");
        else text(g, ally ? 168 : 169, fmt, sizeof(fmt),
                  ally ? "Ally %s scored a flag!" : "Enemy %s scored a flag!");
        break;
    default:
        e.line = HTA_LINE_NONE;
        break;
    }
    snprintf(e.text, sizeof(e.text), fmt, who);
    emit(g, &e);
}

void hta_game_mirror_rules(hta_game *g, hta_game_mode mode, int score_limit,
                           const int team_score[2], int32_t winner_team,
                           const hta_game_flag_state flags[2])
{
    if (!g || !team_score || !flags) return;
    if (g->mode != mode) hta_game_set_mode(g, mode);
    g->score_limit = score_limit;
    for (int t = 0; t < 2; t++) {
        hta_game_flag *f = &g->flags[t];
        const hta_game_flag_state *n = &flags[t];
        int32_t was = f->state == HTA_FLAG_CARRIED ? f->carrier : HTA_GAME_NONE;
        int32_t now = n->state == HTA_FLAG_CARRIED ? n->carrier : HTA_GAME_NONE;
        if (now >= (int32_t)g->unit_count) now = HTA_GAME_NONE;
        if (g->mode == HTA_MODE_CTF && (f->state != n->state || was != now)) {
            if (n->state == HTA_FLAG_CARRIED && now >= 0)
                flag_event(g, now, t, HTA_FLAG_TAKEN);
            else if (f->state == HTA_FLAG_CARRIED && n->state == HTA_FLAG_DROPPED)
                flag_event(g, was, t, HTA_FLAG_DROP);
            else if (f->state != HTA_FLAG_HOME && n->state == HTA_FLAG_HOME) {
                /* Home again: scored (the other side's count went up) or
                 * returned. */
                int other = t ^ 1;
                if (team_score[other] > g->team_score[other] && was >= 0)
                    flag_event(g, was, t, HTA_FLAG_CAPTURE);
                else flag_event(g, -1, t, HTA_FLAG_RETURN);
            }
        }
        if (was >= 0 && was < (int32_t)g->unit_count && g->units[was].flag == t)
            g->units[was].flag = -1;
        if (now >= 0) g->units[now].flag = (int8_t)t;
        f->state = n->state;
        f->carrier = now;
        for (int k = 0; k < 3; k++) f->pos[k] = n->pos[k];
        f->yaw = n->yaw;
        f->rest = n->state != HTA_FLAG_CARRIED;
    }
    g->team_score[0] = team_score[0];
    g->team_score[1] = team_score[1];
    g->winner_team = winner_team;
}

static void flag_home(hta_game *g, int team)
{
    hta_game_flag *f = &g->flags[team & 1];
    if (f->state == HTA_FLAG_CARRIED && f->carrier >= 0 && f->carrier < (int32_t)g->unit_count)
        g->units[f->carrier].flag = -1;
    for (int k = 0; k < 3; k++) { f->pos[k] = f->home[k]; f->vel[k] = 0.0f; }
    f->yaw = f->home_yaw;
    f->state = HTA_FLAG_HOME;
    f->carrier = HTA_GAME_NONE;
    f->idle = 0.0f;
    f->rest = true;
}

static void drop_flag(hta_game *g, int32_t idx, bool thrown)
{
    hta_unit *u = &g->units[idx];
    if (u->flag < 0) return;
    hta_game_flag *f = &g->flags[u->flag & 1];
    int team = u->flag & 1;
    u->flag = -1;
    u->flag_wait = HTA_FLAG_REGRAB;
    f->state = HTA_FLAG_DROPPED;
    f->carrier = HTA_GAME_NONE;
    f->idle = 0.0f;
    f->rest = false;
    f->yaw = u->eye.yaw;
    f->pos[0] = u->body.pos[0];
    f->pos[1] = u->body.pos[1];
    f->pos[2] = u->body.pos[2] + 0.3f;
    if (thrown) {
        /* Put down in front, not under your feet where you would take it
         * straight back. */
        f->vel[0] = cosf(u->eye.yaw) * 1.2f;
        f->vel[1] = sinf(u->eye.yaw) * 1.2f;
        f->vel[2] = 0.8f;
    } else {
        f->vel[0] = u->body.velocity[0] * 0.5f;
        f->vel[1] = u->body.velocity[1] * 0.5f;
        f->vel[2] = 0.5f;
    }
    flag_event(g, idx, team, HTA_FLAG_DROP);
}

void hta_game_team_color(int team, float out[3])
{
    static const float C[2][3] = { { 0.78f, 0.10f, 0.08f }, { 0.12f, 0.24f, 0.85f } };
    for (int k = 0; k < 3; k++) out[k] = C[team & 1][k];
}

bool hta_game_flag_model(const hta_game *g, int team, float model[16])
{
    if (!g || g->mode != HTA_MODE_CTF) return false;
    const hta_game_flag *f = &g->flags[team & 1];
    if (!f->present || f->state == HTA_FLAG_CARRIED) return false;
    float c = cosf(f->yaw), s = sinf(f->yaw);
    const float m[16] = { c, s, 0, 0,   -s, c, 0, 0,   0, 0, 1, 0,
                          f->pos[0], f->pos[1], f->pos[2], 1 };
    memcpy(model, m, sizeof(m));
    return true;
}

bool hta_game_drop_flag(hta_game *g, int32_t unit)
{
    if (!g || unit < 0 || unit >= (int32_t)g->unit_count || g->units[unit].flag < 0) return false;
    drop_flag(g, unit, true);
    return true;
}

/* Is a point within `reach` of where this unit stands -- level with its
 * body, not a floor above or below? */
static bool touches(const hta_unit *u, const float p[3], float reach)
{
    float dz = p[2] - u->body.pos[2];
    return hypotf(p[0] - u->body.pos[0], p[1] - u->body.pos[1]) <= reach &&
           dz > -0.4f && dz < 0.9f;
}

static void flags_update(hta_game *g, float dt)
{
    for (int t = 0; t < 2; t++) {
        hta_game_flag *f = &g->flags[t];
        if (!f->present) continue;
        if (f->state == HTA_FLAG_CARRIED) {
            const hta_unit *c = f->carrier >= 0 && f->carrier < (int32_t)g->unit_count
                              ? &g->units[f->carrier] : NULL;
            if (!c || c->kind == HTA_UNIT_NONE || !c->alive || c->flag != t) {
                /* Lost without a drop: back to its stand. */
                flag_home(g, t);
                flag_event(g, -1, t, HTA_FLAG_RETURN);
                continue;
            }
            for (int k = 0; k < 3; k++) f->pos[k] = c->body.pos[k];
            f->yaw = c->eye.yaw;
        } else if (f->state == HTA_FLAG_DROPPED) {
            f->idle += dt;
            if (!fall(g, f->pos, f->vel, &f->rest, dt) || f->idle >= HTA_FLAG_RESET) {
                flag_home(g, t);
                flag_event(g, -1, t, HTA_FLAG_RETURN);
            }
        }
    }
    if (g->over) return;
    for (uint32_t i = 0; i < g->unit_count; i++) {
        hta_unit *u = &g->units[i];
        if (u->flag_wait > 0.0f) u->flag_wait -= dt;
        if (u->kind == HTA_UNIT_NONE || !u->alive || u->vehicle >= 0) continue;
        for (int t = 0; t < 2; t++) {
            hta_game_flag *f = &g->flags[t];
            if (!f->present || f->state == HTA_FLAG_CARRIED) continue;
            if (!touches(u, f->pos, HTA_FLAG_REACH)) continue;
            if (u->team != t) {
                if (u->flag >= 0 || u->flag_wait > 0.0f) continue;
                f->state = HTA_FLAG_CARRIED;
                f->carrier = (int32_t)i;
                f->idle = 0.0f;
                u->flag = (int8_t)t;
                u->cooldown = 0.5f;
                flag_event(g, (int32_t)i, t, HTA_FLAG_TAKEN);
            } else if (f->state == HTA_FLAG_DROPPED) {
                flag_home(g, t);
                flag_event(g, (int32_t)i, t, HTA_FLAG_RETURN);
            }
        }
        /* Home with theirs while ours is on its stand: a capture. */
        if (u->flag >= 0) {
            const hta_game_flag *own = &g->flags[u->team & 1u];
            if (own->state == HTA_FLAG_HOME && touches(u, own->home, HTA_CAPTURE_REACH)) {
                int taken = u->flag;
                flag_home(g, taken);
                u->score++;
                g->team_score[u->team & 1u]++;
                flag_event(g, (int32_t)i, taken, HTA_FLAG_CAPTURE);
                team_scored(g, u->team);
            }
        }
    }
}

bool hta_game_ctf_goal(const hta_game *g, int32_t unit, float out[3], int *stand)
{
    if (stand) *stand = -1;
    if (!g || g->mode != HTA_MODE_CTF || unit < 0 || unit >= (int32_t)g->unit_count)
        return false;
    const hta_unit *u = &g->units[unit];
    int me = u->team & 1, them = me ^ 1;
    const hta_game_flag *mine = &g->flags[me], *theirs = &g->flags[them];
    const float *to = NULL;
    int at = -1;
    if (u->flag >= 0) { to = mine->home; at = me; }             /* run it home */
    else if (mine->state == HTA_FLAG_DROPPED) to = mine->pos;   /* take ours back */
    else if (mine->state == HTA_FLAG_CARRIED && mine->carrier >= 0)
        to = g->units[mine->carrier].body.pos;                   /* chase the thief */
    else {
        /* Every third player on a side minds the stand. Ours. */
        int rank = 0;
        for (int32_t i = 0; i < unit; i++)
            if (g->units[i].kind != HTA_UNIT_NONE && g->units[i].team == u->team) rank++;
        if (rank % 3 == 2) { to = mine->home; at = me; }
        else if (theirs->state == HTA_FLAG_CARRIED && theirs->carrier >= 0)
            to = g->units[theirs->carrier].body.pos;             /* see the carrier home */
        else {
            to = theirs->pos;                                    /* go and get it */
            if (theirs->state == HTA_FLAG_HOME) at = them;
        }
    }
    for (int k = 0; k < 3; k++) out[k] = to[k];
    if (stand) *stand = at;
    return true;
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
    hulls_reset(g);
    /* A hull going up: the tank shell's own explosion, damage and all. */
    g->wreck_effect = find_tag_path(g->cache, HTA_FOURCC('e','f','f','e'),
                                    "vehicles\\scorpion\\shell explosion");
    if (g->wreck_effect)
        hta_effect_damage(g->cache, g->wreck_effect, &g->wreck_radius, &g->wreck_core,
                          &g->wreck_damage);
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
    /* The flag is carried on foot. Ours. */
    if (!u->alive || u->vehicle >= 0 || u->flag >= 0) return -1;
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
    {   /* Autoaim: the gun's own cone finds an enemy near the crosshair. */
        float dir[3], pt[3];
        aim_dir(&u->eye, dir);
        if (u->kind != HTA_UNIT_BOT &&
            hta_game_aim_target(g, idx, wi, u->eye.pos, dir, pt) >= 0)
            memcpy(target, pt, sizeof(pt));
    }
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
    /* The cannon's kick: back along the barrel, nose up on its springs. */
    if (w->travels && w->blast_damage > 0.0f && g->simulate_vehicles) {
        float mass = c->mass > 1.0f ? c->mass : 20000.0f;
        float k = fminf(HTA_RECOIL_MAX, w->blast_damage * HTA_RECOIL_PUSH * 20000.0f / mass);
        float back[3] = { -aim[0] * k, -aim[1] * k, 0.0f };
        hta_vehicles_push(g->vehicles, car, back, 0.0f, 0.08f);
    }

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
                g->pool_scale[w->pool][slot] = 1.0f;     /* a vehicle's gun */
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
            int32_t struck = hta_game_car_at(g, wh, 0.1f);
            if (struck != (int32_t)car) hta_game_hurt_car_jpt(g, struck, idx, w->impact_jpt, 1, wh);
        }
    }
    own->active = was;
    for (uint32_t v = 0; v < g->unit_count; v++)
        if (hits[v]) hta_game_hurt_jpt_scaled(g, (int32_t)v, idx, w->impact_jpt, hits[v], hit_at[v], w->damage_scale);
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
        /* With the flag in hand the swap button puts it down. */
        if (u->flag >= 0) drop_flag(g, idx, true);
        else if (u->carry[u->slot ^ 1u].weapon >= 0) {
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
        if (u->throwing <= 0.0f && u->swing <= 0.0f && u->grenades > 0 && u->flag < 0)
            u->throwing = UNIT_THROW_TIME;
    }
    if (u->throwing > 0.0f) {
        u->throwing -= dt;
        if (u->throwing <= 0.0f) throw_grenade(g, idx);
    }
    const hta_game_weapon *hw = hta_game_held(g, idx);
    /* A broom in hand is a broom ridden (the local player's flight is
     * the platform's; everyone is drawn seated on it). */
    u->riding = ((hw && hw->mount) || u->flying) && u->vehicle < 0;
    if (hw && hw->melee_only) {
        /* A bat: the trigger swings it. */
        if (in->move.fire && u->swing <= 0.0f && u->throwing <= 0.0f) {
            u->swing = UNIT_SWING_TIME;
            hta_game_melee(g, idx);
        }
    } else if (in->move.fire && u->cooldown <= 0.0f && u->swing <= 0.0f && u->throwing <= 0.0f &&
        c->ammo.phase == HTA_AMMO_READY && u->flag < 0)
        fire(g, idx, dt);
    take_items(g, idx);
    in->pickup = false;
}

static void fly(hta_game *g, float dt)
{
    for (uint32_t p = 0; p < g->pool_count; p++) {
        hta_projectiles *pool = &g->pools[p];
        /* Where each round was: a shell covers more than a body's width in
         * an update, so it is the path that hits people, not the point. */
        float was[HTA_PROJ_MAX][3];
        for (uint32_t k = 0; k < HTA_PROJ_MAX; k++)
            if (pool->live[k].alive) memcpy(was[k], pool->live[k].pos, sizeof(was[k]));
        bool fresh[HTA_PROJ_MAX];
        for (uint32_t k = 0; k < HTA_PROJ_MAX; k++) fresh[k] = !pool->live[k].alive;
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
            int32_t ignore = q->age < 0.1f ? owner : -1;
            int32_t who = -1;
            if (!fresh[k]) {
                float d[3] = { q->pos[0]-was[k][0], q->pos[1]-was[k][1], q->pos[2]-was[k][2] };
                float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
                float t, at[3];
                if (len > 1e-4f) {
                    for (int m = 0; m < 3; m++) d[m] /= len;
                    who = hta_game_ray(g, was[k], d, len, ignore, &t, at);
                    if (who >= 0) memcpy(q->pos, at, sizeof(at));
                }
            }
            if (who < 0) who = hta_game_near(g, q->pos, 0.02f, ignore);
            if (who < 0) continue;
            float sc = g->pool_scale[p][k] > 0.0f ? g->pool_scale[p][k] : 1.0f;
            if (jpt) hta_game_hurt_jpt_scaled(g, who, owner, jpt, 1, q->pos, sc);
            if (pool->blast_damage > 0.0f)
                hta_game_blast(g, owner, q->pos, pool->blast_damage * sc,
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
            float sc = g->pool_scale[p][pool->blasts[b].slot] > 0.0f ? g->pool_scale[p][pool->blasts[b].slot] : 1.0f;
            /* A bolt into a hull: its own impact damage. A blast's is below. */
            if (jpt && !(pool->blast_damage > 0.0f))
                hta_game_hurt_car_jpt(g, hta_game_car_at(g, pool->blasts[b].pos, 0.15f),
                                      owner, jpt, 1, pool->blasts[b].pos);
            if (pool->blast_damage > 0.0f)
                hta_game_blast(g, owner, pool->blasts[b].pos, pool->blast_damage * sc,
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
        if (g->teams) {
            int d = g->team_score[0] - g->team_score[1];
            finish_team(g, d > 0 ? HTA_TEAM_RED : d < 0 ? HTA_TEAM_BLUE : -1);
        } else {
            int32_t order[HTA_GAME_MAX_UNITS];
            finish(g, hta_game_standings(g, order, HTA_GAME_MAX_UNITS) ? order[0] : HTA_GAME_NONE);
        }
    }

    vguns_tick(g, dt);
    hulls_tick(g, dt);
    for (uint32_t i = 0; i < g->unit_count; i++) {
        hta_unit *u = &g->units[i];
        if (u->kind == HTA_UNIT_NONE) continue;
        u->since_attacked += dt;
        u->since_blast += dt;
        if (u->protect > 0.0f) u->protect = u->fired ? 0.0f : u->protect - dt;
        if (!u->alive || g->over) u->ability_active=0.0f;
        if (u->ability_active>0.0f && u->character>=0 && (uint32_t)u->character<g->character_count) {
            u->ability_active-=dt; u->ability_tick-=dt;
            if (u->ability_tick<=0.0f && u->ability_active>0.0f) {
                const hta_oal_asset *a=g->characters[u->character];
                u->ability_tick=fmaxf(a->ability_interval,0.10f);
                hero_shot(g,(int32_t)i);
            }
        } else if (u->ability_cool > 0.0f) {
            /* The wait starts when the attack finishes, so a long beam
             * cannot be fired again the moment it ends. */
            u->ability_cool -= dt;
        }
        if (u->kind != HTA_UNIT_LOCAL && (u->knock[0] != 0.0f || u->knock[1] != 0.0f || u->knock[2] != 0.0f)) {
            for (int k = 0; k < 3; k++) { u->body.velocity[k] += u->knock[k]; u->knock[k] = 0.0f; }
            u->body.on_ground = false;
        }
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
            u->since_shot += dt;
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
        if(u->kind==HTA_UNIT_BOT && u->alive && u->character>=0 &&
           g->brains[i].visible && g->brains[i].target>=0 && u->ability_cool<=0.0f && u->in.move.fire) {
            const hta_oal_asset *a=g->characters[u->character];
            const hta_unit *target=&g->units[g->brains[i].target];
            float dist=hypotf(target->body.pos[0]-u->body.pos[0],target->body.pos[1]-u->body.pos[1]);
            if(a->ability_radius<=0.0f || dist<a->ability_radius) hta_game_ability(g,(int32_t)i);
        }

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
    if (g->simulate_drops) drops_update(g, dt);
    if (g->simulate_drops && g->mode == HTA_MODE_CTF) flags_update(g, dt);

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
    if (g->teams) {
        /* "Red leads Blue 2 to 1 Captures". */
        char fmt[96], a[16], b[16], unitw[24];
        int r = g->team_score[0], bl = g->team_score[1];
        bool ctf = g->mode == HTA_MODE_CTF;
        text(g, ctf ? 22 : 24, unitw, sizeof(unitw), ctf ? "Captures" : "Frags");
        if (r == bl) {
            text(g, 62, fmt, sizeof(fmt), "Teams tied at %s %s");
            snprintf(a, sizeof(a), "%d", r);
            snprintf(out, outlen, fmt, a, unitw);
            return;
        }
        text(g, r > bl ? 60 : 61, fmt, sizeof(fmt),
             r > bl ? "Red leads Blue %s to %s %s" : "Blue leads Red %s to %s %s");
        snprintf(a, sizeof(a), "%d", r > bl ? r : bl);
        snprintf(b, sizeof(b), "%d", r > bl ? bl : r);
        snprintf(out, outlen, fmt, a, b, unitw);
        return;
    }
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

/* ------------------------------------------------------- imported content */

int32_t hta_game_add_character(hta_game *g, const hta_oal_asset *a)
{
    if (!g || !a || !a->loaded || strcmp(a->kind, "character") || g->character_count >= HTA_GAME_MAX_CHARACTERS)
        return -1;
    g->characters[g->character_count] = a;
    g->char_ability[g->character_count] = -1;
    if (a->ability_base[0] && g->weapon_count < HTA_GAME_MAX_WEAPONS) {
        /* Its ability's shot: a copy of a Halo weapon at its own damage,
         * never carried and never offered in a class. */
        int32_t base = -1;
        for (uint32_t w = 0; w < g->weapon_count && base < 0; w++)
            if (!g->weapons[w].vehicle && !g->weapons[w].asset && !g->weapons[w].hidden &&
                (int32_t)w != g->flag_weapon && strstr(g->weapons[w].def.path, a->ability_base)) base = (int32_t)w;
        if (base >= 0) {
            int32_t idx = (int32_t)g->weapon_count++;
            hta_game_weapon *w = &g->weapons[idx];
            *w = g->weapons[base];
            w->hidden = true;
            w->hero = a;
            w->beam = a->ability_beam;
            w->base = base;
            w->damage_scale = a->ability_damage > 0.0f ? a->ability_damage : 1.0f;
            snprintf(w->display, sizeof(w->display), "%s", a->ability_name[0] ? a->ability_name : "ability");
            g->char_ability[g->character_count] = idx;
        }
    }
    return (int32_t)g->character_count++;
}

int32_t hta_game_add_imported_weapon(hta_game *g, const hta_oal_asset *a)
{
    if (!g || !a || !a->loaded || strcmp(a->kind, "weapon") || g->weapon_count >= HTA_GAME_MAX_WEAPONS) return -1;
    int32_t base = -1;
    for (uint32_t w = 0; w < g->weapon_count && base < 0; w++) {
        if (g->weapons[w].vehicle || g->weapons[w].asset || (int32_t)w == g->flag_weapon) continue;
        if (strstr(g->weapons[w].def.path, a->base)) base = (int32_t)w;
    }
    if (base < 0) return -1;
    int32_t idx = (int32_t)g->weapon_count++;
    hta_game_weapon *w = &g->weapons[idx];
    *w = g->weapons[base];
    w->asset = a;
    w->base = base;
    w->model = 0;                  /* the package's world model, not a tag's */
    w->damage_scale = a->damage_scale > 0.0f ? a->damage_scale : 1.0f;
    w->melee_only = a->melee;
    w->mount = a->mount;
    w->knockback = a->knockback;
    snprintf(w->display, sizeof(w->display), "%s", a->display);
    if (a->rounds_per_second > 0.0f) {
        w->def.rof = w->def.rof_initial = a->rounds_per_second;
        w->def.cooldown = 1.0f / a->rounds_per_second;
    }
    /* Halo's "rounds total initial" counts the loaded magazine too: a
     * weapon starts with a full magazine and its spare rounds. */
    int spare = w->def.rounds_initial - w->def.rounds_loaded_max;
    if (spare < 0) spare = 0;
    if (a->magazine > 0) w->def.rounds_loaded_max = w->def.rounds_reloaded = a->magazine;
    if (a->reserve > 0) { w->def.rounds_reserve_max = a->reserve; spare = a->reserve; }
    if (spare > w->def.rounds_reserve_max) spare = w->def.rounds_reserve_max;
    w->def.rounds_initial = w->def.rounds_loaded_max + spare;
    if (a->reload_rounds > 0) w->def.rounds_reloaded = a->reload_rounds;
    if (a->reload_seconds > 0.0f) w->def.reload_time = a->reload_seconds;
    if (a->recharge > 0.0f) {
        /* Charge, not ammunition: a full magazine, no spare, refilling. */
        w->def.recharge = a->recharge;
        w->def.rounds_reserve_max = 0;
        w->def.rounds_initial = w->def.rounds_loaded_max;
    }
    if (a->spread_scale > 0.0f) {
        w->def.error_angle[0] *= a->spread_scale;
        w->def.error_angle[1] *= a->spread_scale;
        w->def.min_error *= a->spread_scale;
    }
    return idx;
}
