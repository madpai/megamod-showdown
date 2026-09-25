#include "view.h"
#include "../asset/model.h"
#include "../asset/bitmap.h"
#include "../asset/bsp.h"
#include "imported.h"

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

/* Object +332 is its widgets (32 each, the tag at +0); the flag weapon's
 * one widget is its cloth. Flag reconciles at 96: vertices across and down
 * at +12/+14, the cell size at +16/+20, the red and blue shaders at +24 and
 * +68, and the attachment points at +84 (52 each, marker name at +20):
 * "flag top" and "flag bottom" on the pole. */
#define OBJ_WIDGETS        332u
#define FLAG_WIDTH          12u
#define FLAG_HEIGHT         14u
#define FLAG_CELL_W         16u
#define FLAG_CELL_H         20u
#define FLAG_RED_SHADER     24u
#define FLAG_BLUE_SHADER    68u
/* Halo blows the cloth about with a spring model and wind. Ours is still:
 * a gentle ripple baked in along its length, this deep, so it reads as
 * cloth rather than a sign. */
#define CLOTH_RIPPLE      0.035f

static uint32_t tag_base(const hta_cache *c, uint32_t id)
{
    int32_t ti = hta_cache_find_tag_by_id(c, id);
    hta_tag_entry t;
    uint32_t off = 0;
    if (ti < 0 || !hta_cache_tag(c, (uint32_t)ti, &t) ||
        !hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) return 0;
    return off;
}

/* The flag's cloth for `team`, appended to the pole's mesh as one more
 * submesh in the team's own bitmap. Returns its submesh index, 0 if none. */
static uint32_t append_cloth(hta_bsp_mesh *m, const hta_game *g,
                             const hta_resource_map *bitmaps, uint32_t weap, int team)
{
    const hta_cache *c = g->cache;
    uint32_t wb = tag_base(c, weap), count = 0, ptr = 0, off = 0, flag = 0;
    if (!wb || !hta_read_reflexive(c, wb + OBJ_WIDGETS, &count, &ptr) || !count ||
        !hta_cache_ptr_to_offset(c, ptr, &off) || !hta_rd_u32(c, off + 12u, &flag)) return 0;
    uint32_t fb = tag_base(c, flag);
    if (!fb) return 0;
    uint16_t nw = 0, nh = 0;
    float cw = 0.0f, ch = 0.0f;
    uint32_t shader = 0;
    hta_rd_u16(c, fb + FLAG_WIDTH, &nw);
    hta_rd_u16(c, fb + FLAG_HEIGHT, &nh);
    hta_rd_f32(c, fb + FLAG_CELL_W, &cw);
    hta_rd_f32(c, fb + FLAG_CELL_H, &ch);
    hta_rd_u32(c, fb + (team ? FLAG_BLUE_SHADER : FLAG_RED_SHADER) + 12u, &shader);
    if (nw < 2 || nh < 2 || nw > 64 || nh > 64 || !(cw > 0.0f) || !(ch > 0.0f)) return 0;
    uint32_t bitmap = hta_shader_base_bitmap(c, shader);
    if (!bitmap) return 0;

    /* The edge it hangs from: the pole's markers, else the cloth's own
     * height down from the model's top. */
    float top[3] = { 0.0f, 0.0f, 0.0f }, bottom[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t model = g->weapons[g->flag_weapon].model;
    bool have_top = hta_model_marker_position(c, model, "flag top", top);
    bool have_bottom = hta_model_marker_position(c, model, "flag bottom", bottom);
    if (!have_top) { top[2] = 0.8f; }
    if (!have_bottom) { bottom[0] = top[0]; bottom[1] = top[1]; bottom[2] = top[2] - ch * (float)(nh - 1); }

    uint32_t nv = (uint32_t)nw * nh, ni = (uint32_t)(nw - 1) * (nh - 1) * 12u;
    hta_vertex *vv = realloc(m->vertices, (m->vertex_count + nv) * sizeof(hta_vertex));
    if (!vv) return 0;
    m->vertices = vv;
    uint32_t *ii = realloc(m->indices, (m->index_count + ni) * sizeof(uint32_t));
    if (!ii) return 0;
    m->indices = ii;
    hta_submesh *ss = realloc(m->submeshes, (m->submesh_count + 1u) * sizeof(hta_submesh));
    if (!ss) return 0;
    m->submeshes = ss;

    /* It trails back from the pole, along the model's -x. */
    uint32_t v0 = m->vertex_count;
    for (uint32_t j = 0; j < nh; j++) {
        float fj = (float)j / (float)(nh - 1);
        for (uint32_t i = 0; i < nw; i++) {
            float fi = (float)i / (float)(nw - 1);
            float along = cw * (float)i;
            float ripple = CLOTH_RIPPLE * fi * sinf(fi * 3.0f * 3.14159265f);
            float slope = CLOTH_RIPPLE * fi * 3.0f * 3.14159265f * cosf(fi * 3.0f * 3.14159265f);
            hta_vertex *p = &m->vertices[m->vertex_count++];
            p->pos[0] = bottom[0] + (top[0] - bottom[0]) * fj - along;
            p->pos[1] = bottom[1] + (top[1] - bottom[1]) * fj + ripple;
            p->pos[2] = bottom[2] + (top[2] - bottom[2]) * fj;
            float nl = sqrtf(1.0f + slope * slope);
            p->normal[0] = slope / nl; p->normal[1] = 1.0f / nl; p->normal[2] = 0.0f;
            p->uv[0] = fi;
            p->uv[1] = 1.0f - fj;
            p->lm_uv[0] = p->lm_uv[1] = 0.0f;
        }
    }
    uint32_t first = m->index_count;
    for (uint32_t j = 0; j + 1 < nh; j++)
        for (uint32_t i = 0; i + 1 < nw; i++) {
            uint32_t a = v0 + j * nw + i, b = a + 1, d = a + nw, e = d + 1;
            /* Both faces: a cloth is seen from either side. */
            const uint32_t q[12] = { a, b, e, a, e, d,   a, e, b, a, d, e };
            for (int k = 0; k < 12; k++) m->indices[m->index_count++] = q[k];
        }
    hta_submesh *sm = &m->submeshes[m->submesh_count];
    hta_submesh_init(sm);
    sm->first_index = first;
    sm->index_count = m->index_count - first;
    sm->shader_tag_id = shader;
    sm->albedo_tex = hta_mesh_intern_bitmap(m, c, bitmaps, bitmap, 0);
    sm->draw_mode = HTA_DRAW_OPAQUE;
    return m->submesh_count++;
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
        if (g->weapons[w].asset) {
            v->weapon_mesh[w] = g->weapons[w].asset->models[0].mesh;
            v->have_weapon[w] = v->borrowed[w] = true;
            weapons++;
            continue;
        }
        if (!g->weapons[w].model) continue;
        hta_bsp_mesh *m = &v->weapon_mesh[w];
        memset(m, 0, sizeof(*m));
        m->textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
        if (!m->textures) continue;
        const float zero[3] = { 0.0f, 0.0f, 0.0f };
        if (hta_model_instance(m, g->cache, bitmaps, g->weapons[w].model, zero, zero,
                               aerr, sizeof(aerr)) && m->index_count) {
            if ((int32_t)w == g->flag_weapon) {
                v->flag_pole_submeshes = m->submesh_count;
                for (int t = 0; t < 2; t++)
                    v->flag_cloth[t] = append_cloth(m, g, bitmaps, g->weapons[w].tag, t);
            }
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
        if (v->have_weapon[w] && !v->borrowed[w]) hta_bsp_free(&v->weapon_mesh[w]);
    for (uint32_t i = 0; i < HTA_GAME_MAX_UNITS; i++) free(v->oal_posed[i]);
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

/* How long a body without a death clip takes to topple, and how far it
 * falls (radians, onto its back). Ours: Source bodies ragdoll instead. */
#define TOPPLE_TIME 0.45f
#define TOPPLE_ANGLE 1.5f

static const hta_oal_model *body_of(const hta_game *g, const hta_unit *u)
{
    if (u->character < 0 || (uint32_t)u->character >= g->character_count || !g->characters[u->character])
        return NULL;
    return &g->characters[u->character]->models[0];
}

/* An imported body: pick the clip the game state calls for, pose, skin. */
static void imported_update(hta_game_view *v, const hta_game *g, uint32_t i, float dt)
{
    const hta_unit *u = &g->units[i];
    const hta_oal_model *m = body_of(g, u);
    if (!m) return;
    if (v->oal_capacity[i] < m->mesh.vertex_count || v->oal_char[i] != u->character) {
        free(v->oal_posed[i]);
        v->oal_posed[i] = malloc(m->mesh.vertex_count * sizeof(hta_vertex));
        v->oal_capacity[i] = v->oal_posed[i] ? m->mesh.vertex_count : 0;
        v->oal_char[i] = u->character;
        v->oal_clip[i] = -2;
    }
    if (!v->oal_posed[i]) return;
    const char *role;
    char base[48], action[64];
    float topple = 0.0f;
    if (!u->alive) {
        role = "death";
        if (hta_oal_clip_find(m, role) < 0) {
            role = "idle";
            float k = u->dead_for / TOPPLE_TIME;
            topple = (k > 1.0f ? 1.0f : k) * TOPPLE_ANGLE;
        }
    } else if (u->flying) {
        /* Superman 64's imported jump clip loses its head. A self-powered
         * flyer leans forward in the complete idle pose; broom riders use air. */
        role = "idle";
        topple = 0.12f;
    } else if (u->riding) {
        role = "air";           /* on a broom: legs hanging, as in the jump */
    } else if (u->vehicle >= 0) {
        role = "crouch_idle";   /* seated: Source bodies have no seat clips */
    } else {
        hta_game_anim(g, (int32_t)i, base, sizeof(base), action, sizeof(action));
        role = u->body.on_ground ? hta_imported_role(base) : "air";
    }
    const hta_game_weapon *held=hta_game_held(g,(int32_t)i);
    char stance='r', posed_role[24];
    if(held && held->asset) {
        const char *h=held->asset->hold_type;
        stance=!strcmp(h,"fist")?'f':!strcmp(h,"melee")?'m':!strcmp(h,"pistol")?'p':'r';
    } else if(held && !strcmp(held->anim_class,"pistol")) stance='p';
    if(v->oal_attack[i]>0.0f) v->oal_attack[i]-=dt;
    if(v->oal_attack[i]<=0.0f && (u->meleed || (u->fired && stance!='r'))) v->oal_attack[i]=.45f;
    if(u->alive && !u->riding && !u->flying && u->vehicle<0) {
        const char *pose_role=(u->swing>0.0f || v->oal_attack[i]>0.0f || u->ability_active>0.0f)?"attack":role;
        snprintf(posed_role,sizeof(posed_role),"%c_%s",stance,pose_role);
        if(hta_oal_clip_find(m,posed_role)>=0) role=posed_role;
    }
    int32_t clip = hta_oal_clip_find(m, role);
    if (clip < 0) clip = hta_oal_clip_find(m, "idle");
    if (clip != v->oal_clip[i]) { v->oal_clip[i] = clip; v->oal_time[i] = 0.0f; }
    else v->oal_time[i] += dt;
    hta_oal_pose(m, clip, v->oal_time[i], v->oal_world[i]);
    float yaw = u->alive ? u->eye.yaw : u->death_yaw;
    const float *at = u->body.pos;
    hta_transform seat;
    if (u->alive && u->vehicle >= 0 && hta_game_seat_root(g, (int32_t)i, &seat)) {
        /* The seat's place and heading (yaw of its rotation). */
        const float *q = seat.q;
        yaw = atan2f(2.0f*(q[3]*q[2] + q[0]*q[1]), 1.0f - 2.0f*(q[1]*q[1] + q[2]*q[2]));
        at = seat.t;
    }
    float cy = cosf(yaw), sy = sinf(yaw), ct = cosf(topple), st = sinf(topple);
    /* Yaw about +Z, after a fall backwards about the body's own +Y. */
    float *r = v->oal_root[i];
    r[0] = cy*ct;  r[1] = -sy; r[2] = cy*st;  r[3] = at[0];
    r[4] = sy*ct;  r[5] = cy;  r[6] = sy*st;  r[7] = at[1];
    r[8] = -st;    r[9] = 0;   r[10] = ct;    r[11] = at[2];
    hta_oal_skin(m, (const float (*)[12])v->oal_world[i], r, v->oal_posed[i]);
}

const hta_bsp_mesh *hta_game_view_body_mesh(const hta_game_view *v, const hta_game *g, uint32_t i)
{
    if (!v || !g || i >= HTA_GAME_MAX_UNITS) return NULL;
    const hta_oal_model *m = i < g->unit_count ? body_of(g, &g->units[i]) : NULL;
    return m ? &m->mesh : &v->actor[i].mesh;
}

const hta_vertex *hta_game_view_body_vertices(const hta_game_view *v, const hta_game *g, uint32_t i)
{
    if (!v || !g || i >= HTA_GAME_MAX_UNITS) return NULL;
    const hta_oal_model *m = i < g->unit_count ? body_of(g, &g->units[i]) : NULL;
    return m ? v->oal_posed[i] : v->actor[i].posed;
}

void hta_game_view_update(hta_game_view *v, const hta_game *g, int32_t skip, float dt)
{
    if (!v || !v->loaded || !g) return;
    for (uint32_t i = 0; i < g->unit_count && i < HTA_GAME_MAX_UNITS; i++) {
        hta_actor *a = &v->actor[i];
        const hta_unit *u = &g->units[i];
        v->shown[i] = false;
        if (u->kind != HTA_UNIT_NONE && (int32_t)i != skip && body_of(g, u)) {
            imported_update(v, g, i, dt);
            v->shown[i] = v->oal_posed[i] && (u->alive || (!u->gibbed && u->dead_for < g->respawn_time - 0.05f));
            continue;
        }
        if (!a->loaded || u->kind == HTA_UNIT_NONE || (int32_t)i == skip) continue;
        if (!u->alive) {
            /* The body stays where it fell until the unit comes back. */
            if (!v->dying[i]) {
                v->dying[i] = true;
                hta_actor_play_death(a, &v->rng);
                a->yaw = u->death_yaw;
                /* Forget the clip it was in: back in the same seat after a
                 * respawn, an unchanged name would leave the death playing. */
                v->base_clip[i][0] = 0;
                v->action_left[i] = 0.0f;
            }
            hta_actor_update(a, dt);
            hta_actor_place(a, NULL, a->yaw);
            v->shown[i] = !u->gibbed && u->dead_for < g->respawn_time - 0.05f;
            continue;
        }
        v->dying[i] = false;
        if (u->vehicle >= 0 && g->vehicles) {
            /* Sitting: the seat's own clip, on the seat's own marker. The
             * Scorpion's driver is inside the hull and not drawn. */
            hta_transform root;
            const hta_vehicle *car = &g->vehicles->cars[u->vehicle];
            const hta_vehicle_seat *st = hta_vehicles_seat(g->vehicles, (uint32_t)u->vehicle,
                                                           (uint32_t)u->seat);
            if (!st || !hta_game_seat_root(g, (int32_t)i, &root)) continue;
            if (car->kind == HTA_VK_TANK && (st->flags & HTA_SEAT_DRIVER)) continue;
            const hta_game_weapon *w = hta_game_held(g, (int32_t)i);
            char want[4][64];
            const char *z = w && w->z_prefix ? "z" : "";
            snprintf(want[0], 64, "%s%s %s idle", z, st->label,
                     (st->flags & HTA_SEAT_ALLOWS_WEAPONS) && w ? w->anim_class : "fixed");
            snprintf(want[1], 64, "%s unarmed idle", st->label);
            snprintf(want[2], 64, "%s rifle idle", st->label);
            /* The Scorpion's riders on the right and at the back share the
             * left front's clips. */
            snprintf(want[3], 64, "scorpionLF rifle idle");
            if (strcmp(v->base_clip[i], want[0])) {
                bool ok = false;
                for (int k = 0; k < 4 && !ok; k++)
                    if (hta_anim_find(&a->graph, want[k]) >= 0 && hta_actor_play(a, want[k], false))
                        ok = true;
                snprintf(v->base_clip[i], sizeof(v->base_clip[i]), "%.47s", want[0]);
            }
            hta_actor_update(a, dt);
            hta_actor_place_root(a, &root);
            v->shown[i] = true;
            continue;
        }
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

void hta_game_view_weapon_space(const hta_game *g, int32_t weapon, float model[16])
{
    if (!g || weapon < 0 || (uint32_t)weapon >= g->weapon_count || !g->weapons[weapon].asset) return;
    float s34[12], s[16], t[16];
    hta_imported_source_in_halo_hand(&g->weapons[weapon].asset->models[0], s34);
    hta_oal_to_mat4(s34, s);
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float acc = 0;
            for (int k = 0; k < 4; k++) acc += model[k*4+r] * s[c*4+k];
            t[c*4+r] = acc;
        }
    memcpy(model, t, sizeof(t));
}

uint32_t hta_game_view_weapons(const hta_game_view *v, const hta_game *g, int32_t skip,
                               hta_game_held_weapon *out, uint32_t max)
{
    uint32_t n = 0;
    if (!v || !v->loaded || !g || v->hand_node < 0) return 0;
    for (uint32_t i = 0; i < g->unit_count && i < HTA_GAME_MAX_UNITS && n < max; i++) {
        const hta_unit *u = &g->units[i];
        if (!v->shown[i] || (int32_t)i == skip || !u->alive) continue;
        int32_t w = u->flag >= 0 ? g->flag_weapon : u->carry[u->slot & 1u].weapon;
        if (w < 0 || !v->have_weapon[w]) continue;
        if (u->vehicle >= 0 && g->vehicles) {
            const hta_vehicle_seat *st = hta_vehicles_seat(g->vehicles, (uint32_t)u->vehicle,
                                                           (uint32_t)u->seat);
            if (!st || !(st->flags & HTA_SEAT_ALLOWS_WEAPONS)) continue;
        }
        out[n].weapon = w;
        out[n].first_submesh = out[n].submesh_count = 0;
        const hta_oal_model *body = body_of(g, u);
        const hta_oal_asset *wa = g->weapons[w].asset;
        if (wa && wa->mount) {
            /* A broom rides under the body, not in its hand. */
            float root[12], m34[12];
            if (body) memcpy(root, v->oal_root[i], sizeof(root));
            else {
                float cy = cosf(u->eye.yaw), sy = sinf(u->eye.yaw);
                const float r[12] = { cy, -sy, 0, u->body.pos[0],  sy, cy, 0, u->body.pos[1],  0, 0, 1, u->body.pos[2] };
                memcpy(root, r, sizeof(root));
            }
            hta_imported_mount_matrix(root, wa, m34);
            hta_oal_to_mat4(m34, out[n].model);
            n++;
            continue;
        }
        if (body) {
            /* An imported body: bone-merge its own family's weapon, or hold
             * a Halo one across the two conventions. */
            float m34[12];
            bool ok = wa ? hta_imported_hold_matrix(body, (const float (*)[12])v->oal_world[i], v->oal_root[i],
                                                    &wa->models[0], m34)
                         : hta_imported_halo_in_source_hand(body, (const float (*)[12])v->oal_world[i],
                                                            v->oal_root[i], m34);
            if (!ok) continue;
            hta_oal_to_mat4(m34, out[n].model);
            n++;
            continue;
        }
        hta_actor_marker_matrix(&v->actor[i], v->hand_node, v->hand_offset, out[n].model);
        /* A Halo body holding an imported weapon. */
        if (wa) hta_game_view_weapon_space(g, w, out[n].model);
        if (u->flag >= 0) {
            /* The pole and the cloth of whichever flag it is. */
            uint32_t first[2], count[2];
            uint32_t parts = hta_game_view_flag_parts(v, u->flag, first, count);
            for (uint32_t p = 0; p < parts && n < max; p++) {
                out[n] = out[n - (p ? 1u : 0u)];
                out[n].first_submesh = first[p];
                out[n].submesh_count = count[p];
                n++;
            }
            continue;
        }
        n++;
    }
    return n;
}

uint32_t hta_game_view_flag_parts(const hta_game_view *v, int team,
                                  uint32_t first[2], uint32_t count[2])
{
    if (!v || !v->flag_pole_submeshes) return 0;
    uint32_t n = 0;
    first[n] = 0; count[n] = v->flag_pole_submeshes; n++;
    if (v->flag_cloth[team & 1]) { first[n] = v->flag_cloth[team & 1]; count[n] = 1; n++; }
    return n;
}
