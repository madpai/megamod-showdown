#include "view.h"
#include "../asset/model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How long an action overlay (fire, melee, throw) holds the body before the
 * base clip is allowed back. The clips are one-shot base animations here,
 * so this is their length at 30 fps, capped. */
#define ACTION_MAX 1.0f

static uint32_t find_cyborg(const hta_cache *c)
{
    for (uint32_t i = 0; i < c->tag_count; i++) {
        hta_tag_entry t;
        char p[256];
        if (!hta_cache_tag(c, i, &t) || t.primary_class != HTA_FOURCC('b','i','p','d')) continue;
        if (hta_cache_tag_path(c, &t, p, sizeof(p)) && strstr(p, "cyborg_mp")) return t.tag_id;
    }
    return 0;
}

bool hta_game_view_load(hta_game_view *v, const hta_game *g,
                        const hta_resource_map *bitmaps, uint32_t units,
                        char *err, size_t errlen)
{
    if (!v || !g || !g->cache) { if (err) snprintf(err, errlen, "bad arguments"); return false; }
    memset(v, 0, sizeof(*v));
    v->hand_node = -1;
    v->rng = 0x51ED27u;
    uint32_t bip = find_cyborg(g->cache);
    if (!bip) { if (err) snprintf(err, errlen, "no cyborg_mp biped"); return false; }
    if (units > HTA_GAME_MAX_UNITS) units = HTA_GAME_MAX_UNITS;
    uint32_t bodies = 0;
    char aerr[HTA_ERRLEN];
    for (uint32_t i = 0; i < units; i++) {
        if (!hta_actor_load(&v->actor[i], g->cache, bitmaps, bip, aerr, sizeof(aerr))) break;
        bodies++;
    }
    if (!bodies) { if (err) snprintf(err, errlen, "cyborg: %s", aerr); return false; }
    hta_actor_find_marker(&v->actor[0], g->cache, "right hand", &v->hand_node, v->hand_offset);

    uint32_t weapons = 0;
    for (uint32_t w = 0; w < g->weapon_count; w++) {
        if (!g->weapons[w].model) continue;
        hta_bsp_mesh *m = &v->weapon_mesh[w];
        memset(m, 0, sizeof(*m));
        m->textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
        if (!m->textures) continue;
        const float zero[3] = { 0.0f, 0.0f, 0.0f };
        if (hta_model_instance(m, g->cache, bitmaps, g->weapons[w].model, zero, zero,
                               aerr, sizeof(aerr)) && m->index_count) {
            /* Lit by the scene like the bodies: these have no lightmap. */
            for (uint32_t s = 0; s < m->submesh_count; s++) m->submeshes[s].scene_lit = true;
            v->have_weapon[w] = true;
            weapons++;
        } else {
            hta_bsp_free(m);
        }
    }
    v->loaded = true;
    if (err && errlen)
        snprintf(err, errlen, "%u bodies (%u verts each), %u held weapon models, hand node %d",
                 bodies, v->actor[0].mesh.vertex_count, weapons, (int)v->hand_node);
    return true;
}

void hta_game_view_free(hta_game_view *v)
{
    if (!v) return;
    for (uint32_t i = 0; i < HTA_GAME_MAX_UNITS; i++) hta_actor_free(&v->actor[i]);
    for (uint32_t w = 0; w < HTA_GAME_MAX_WEAPONS; w++)
        if (v->have_weapon[w]) hta_bsp_free(&v->weapon_mesh[w]);
    memset(v, 0, sizeof(*v));
}

/* The graph has no clip for every stance a weapon asks for -- the rocket
 * launcher crouched has no airborne, the pistol has no move-left in some
 * stances. Fall back through the rifle's, which exist for everything. */
static bool play_best(hta_actor *a, const char *want, bool hold)
{
    if (hta_actor_play(a, want, hold)) return true;
    char alt[64];
    const char *sp = strchr(want, ' ');
    if (!sp) return false;
    const char *sp2 = strchr(sp + 1, ' ');
    if (!sp2) return false;
    snprintf(alt, sizeof(alt), "%.*s rifle%s", (int)(sp - want), want, sp2);
    if (hta_actor_play(a, alt, hold)) return true;
    snprintf(alt, sizeof(alt), "stand rifle%s", sp2);
    if (hta_actor_play(a, alt, hold)) return true;
    return hta_actor_play(a, "stand rifle idle", hold);
}

void hta_game_view_update(hta_game_view *v, const hta_game *g, int32_t skip, float dt)
{
    if (!v || !v->loaded || !g) return;
    for (uint32_t i = 0; i < g->unit_count && i < HTA_GAME_MAX_UNITS; i++) {
        hta_actor *a = &v->actor[i];
        const hta_unit *u = &g->units[i];
        v->shown[i] = false;
        if (!a->loaded || u->kind == HTA_UNIT_NONE || (int32_t)i == skip) continue;
        if (!u->alive) {
            /* The body stays where it fell until the unit comes back. */
            if (!v->dying[i]) {
                v->dying[i] = true;
                hta_actor_play_death(a, &v->rng);
                a->yaw = u->death_yaw;
            }
            hta_actor_update(a, dt);
            hta_actor_place(a, NULL, a->yaw);
            v->shown[i] = u->dead_for < g->respawn_time - 0.05f;
            continue;
        }
        v->dying[i] = false;
        char base[48], action[64];
        hta_game_anim(g, (int32_t)i, base, sizeof(base), action, sizeof(action));
        int32_t act = action[0] ? hta_anim_find(&a->graph, action) : -1;
        if (act >= 0 && a->graph.anims[act].type != 0) {
            /* Fire (overlay) and melee (replacement) keyframe only the
             * nodes they move; played as a base clip every other node takes
             * the clip's defaults and the body turns inside out. */
            hta_actor_play_overlay(a, action);
        } else if (act >= 0 && hta_actor_play(a, action, false)) {
            const hta_animation *an = &a->graph.anims[a->clip];
            float len = (float)an->frame_count / HTA_ANIM_FPS;
            v->action_left[i] = len < ACTION_MAX ? len : ACTION_MAX;
            v->base_clip[i][0] = 0;
        }
        if (act >= 0 && a->graph.anims[act].type == 0) {
            /* a base-clip action is running */
        } else if (v->action_left[i] > 0.0f) {
            v->action_left[i] -= dt;
        } else if (strcmp(base, v->base_clip[i]) != 0) {
            play_best(a, base, false);
            snprintf(v->base_clip[i], sizeof(v->base_clip[i]), "%s", base);
        }
        if (u->hurt && !hta_actor_overlaying(a)) {
            /* s-ping is an overlay too. */
            static const char *const FLINCH[] = { "s-ping front gut%0", "s-ping front gut%1",
                                                  "s-ping front gut%2" };
            v->rng = v->rng * 1664525u + 1013904223u;
            hta_actor_play_overlay(a, FLINCH[(v->rng >> 16) % 3u]);
        }
        hta_actor_update(a, dt);
        hta_actor_place(a, u->body.pos, u->eye.yaw);
        v->shown[i] = true;
    }
}

uint32_t hta_game_view_weapons(const hta_game_view *v, const hta_game *g, int32_t skip,
                               hta_game_held_weapon *out, uint32_t max)
{
    uint32_t n = 0;
    if (!v || !v->loaded || !g || v->hand_node < 0) return 0;
    for (uint32_t i = 0; i < g->unit_count && i < HTA_GAME_MAX_UNITS && n < max; i++) {
        const hta_unit *u = &g->units[i];
        if (!v->shown[i] || (int32_t)i == skip || !u->alive) continue;
        int32_t w = u->carry[u->slot & 1u].weapon;
        if (w < 0 || !v->have_weapon[w]) continue;
        out[n].weapon = w;
        hta_actor_marker_matrix(&v->actor[i], v->hand_node, v->hand_offset, out[n].model);
        n++;
    }
    return n;
}
