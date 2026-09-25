/* MEGAMOD SANDBOX: the engine's own systems on the desktop, with no game
 * data at all. A generated arena with destructible crates, explosive
 * barrels, glass and concrete, target dummies that come apart, grenades,
 * weather, and live video presets -- everything the Android build gains
 * this session, on PC, as a starting point for other games on this engine.
 *
 *   megamod-sandbox [--preset potato|low|medium|high|ultra] [--weather storm]
 *                   [--size WxH] [--fullscreen]
 *                   [--demo SECONDS] [--shot out.ppm]    (scripted; --shot renders offscreen)
 *
 * Keys: WASD move, mouse look (click to capture, Esc to release / quit),
 * LMB shoot, RMB or G grenade, Space jump, C crouch, F2 preset, F3 weather,
 * F4 gore, F5 dynamic resolution, F11 fullscreen, R reset the arena. */
#include "engine/camera.h"
#include "engine/player.h"
#include "game/world_fx_audio.h"
#include "game/world_fx_gpu.h"
#include "gfx/gfx.h"
#include "platform/audio_sdl.h"
#include "platform/desktop_sdl.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- the arena */

#define MAXV 4096
#define MAXI 8192
static hta_vertex g_v[MAXV];
static uint32_t   g_i[MAXI];
static uint32_t   g_nv, g_ni;

static void quad(const float a[3], const float b[3], const float c[3], const float d[3], float us, float vs)
{
    const float *p[4] = { a, b, c, d };
    const float uv[4][2] = { {0,0}, {us,0}, {us,vs}, {0,vs} };
    float e1[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] }, e2[3] = { d[0]-a[0], d[1]-a[1], d[2]-a[2] };
    float n[3] = { e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0] };
    float l = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    uint32_t base = g_nv;
    for (int i = 0; i < 4; i++) {
        hta_vertex *v = &g_v[g_nv++];
        memset(v, 0, sizeof(*v));
        memcpy(v->pos, p[i], 12);
        for (int k = 0; k < 3; k++) v->normal[k] = l > 0 ? n[k] / l : 0;
        v->uv[0] = uv[i][0]; v->uv[1] = uv[i][1];
        /* A baked "lightmap" coordinate per face: sun-facing faces bright. */
        v->lm_uv[0] = 0.5f; v->lm_uv[1] = 0.5f;
    }
    const uint32_t t[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++) g_i[g_ni++] = base + t[i];
}

static void block(float x0, float y0, float z0, float x1, float y1, float z1)
{
    float c[8][3];
    for (int i = 0; i < 8; i++) { c[i][0] = i & 1 ? x1 : x0; c[i][1] = i & 2 ? y1 : y0; c[i][2] = i & 4 ? z1 : z0; }
    float sx = (x1 - x0) * 0.5f, sy = (y1 - y0) * 0.5f, sz = (z1 - z0) * 0.5f;
    quad(c[4], c[5], c[7], c[6], sx, sy);   /* top */
    quad(c[0], c[1], c[5], c[4], sx, sz);   quad(c[3], c[2], c[6], c[7], sx, sz);
    quad(c[2], c[0], c[4], c[6], sy, sz);   quad(c[1], c[3], c[7], c[5], sy, sz);
}

static hta_bsp_texture g_tex[2];

static void build_arena(hta_bsp_mesh *m)
{
    g_nv = g_ni = 0;
    /* Ground: 120 x 120 wu, a checker at 1 wu. */
    { float a[3]={-60,-60,0}, b[3]={60,-60,0}, c[3]={60,60,0}, d[3]={-60,60,0}; quad(a, b, c, d, 60, 60); }
    uint32_t ground_end = g_ni;
    /* A courtyard: walls, a ramp, a raised deck, pillars, a roof to stand under in the rain. */
    block(-20, -20, 0, 20, -19, 4);   block(-20, 19, 0, 20, 20, 4);
    block(-20, -19, 0, -19, 19, 4);   block(19, -19, 0, 20, -3, 4);   block(19, 3, 0, 20, 19, 4);
    block(8, 8, 0, 18, 18, 2);        /* deck */
    { float a[3]={2,8,0}, b[3]={8,8,2}, c[3]={8,18,2}, d[3]={2,18,0}; quad(a, b, c, d, 3, 5); }   /* ramp */
    for (int k = 0; k < 4; k++) block(-14 + k * 7, -2, 0, -13 + k * 7, -1, 6);
    block(-18, 6, 5, -6, 18, 5.4f);   /* roof on posts */
    block(-18, 6, 0, -17.6f, 6.4f, 5); block(-6.4f, 6, 0, -6, 6.4f, 5);
    block(-18, 17.6f, 0, -17.6f, 18, 5); block(-6.4f, 17.6f, 0, -6, 18, 5);

    static hta_submesh sm[2];
    hta_submesh_init(&sm[0]); hta_submesh_init(&sm[1]);
    sm[0].first_index = 0; sm[0].index_count = ground_end; sm[0].albedo_tex = 0;
    sm[1].first_index = ground_end; sm[1].index_count = g_ni - ground_end; sm[1].albedo_tex = 1;
    sm[0].scene_lit = sm[1].scene_lit = true;
    for (int t = 0; t < 2; t++) {
        uint32_t n = 64;
        uint8_t *px = malloc(n * n * 4);
        for (uint32_t y = 0; y < n; y++) for (uint32_t x = 0; x < n; x++) {
            uint8_t *p = px + (y * n + x) * 4;
            int chk = ((x >> 5) ^ (y >> 5)) & 1;
            uint32_t h = (x * 73856093u) ^ (y * 19349663u) ^ (uint32_t)t * 83492791u;
            int noise = (int)(h % 17) - 8;
            if (t == 0) { p[0] = (uint8_t)(94 + chk * 14 + noise); p[1] = (uint8_t)(104 + chk * 12 + noise); p[2] = (uint8_t)(78 + noise); }
            else { p[0] = (uint8_t)(150 + noise); p[1] = (uint8_t)(145 + noise); p[2] = (uint8_t)(136 + noise); }
            p[3] = 255;
        }
        g_tex[t] = (hta_bsp_texture){ .width = n, .height = n, .rgba = px, .tint = 0xFFFFFF, .tag_id = (uint32_t)t };
    }
    memset(m, 0, sizeof(*m));
    m->vertices = g_v; m->vertex_count = g_nv; m->indices = g_i; m->index_count = g_ni;
    m->submeshes = sm; m->submesh_count = 2;
    m->textures = g_tex; m->texture_count = 2;
    m->bounds_min[0] = m->bounds_min[1] = -60; m->bounds_min[2] = 0;
    m->bounds_max[0] = m->bounds_max[1] = 60; m->bounds_max[2] = 6;
}

/* ---------------------------------------------------------------- dummies */

typedef struct { float pos[3]; float health; bool alive; float respawn; } dummy;
#define NDUMMY 6
static dummy g_dummy[NDUMMY];

/* ------------------------------------------------------------------ state */

typedef struct {
    hta_collision col;
    hta_collision_instance merged[300];
    hta_world_fx wfx;
    hta_gfx_settings video;
    hta_player player;
    hta_camera cam;
    hta_scene scene;
    /* intact props and dummies, drawn as lit boxes from the fx atlas */
    hta_bsp_mesh boxes;
    hta_vertex *box_verts;
    uint32_t box_slots;
    hta_gfx_mesh *gpu_world, *gpu_boxes;
    /* grenades in flight: rigid bodies with a fuse */
    uint32_t nade_body[8];
    float    nade_fuse[8];
    float    shake;
    int      weather;
    hta_quality preset;
    /* sound: SDL out, the procedural bank */
    hta_audio     audio;
    hta_wfx_audio wfx_audio;
} sandbox;

static void reset_arena(sandbox *s)
{
    hta_props_free(&s->wfx.props);
    hta_props_init(&s->wfx.props, 128);
    hta_rigid_clear(&s->wfx.rigid);
    /* Crates in stacks, barrels, glass panes, a concrete barrier. */
    for (int i = 0; i < 12; i++) {
        float c[3] = { -10.0f + (float)(i % 4) * 0.8f, 10.0f + (float)(i / 4 % 2) * 0.8f, 0.3f + (float)(i / 8) * 0.6f };
        float h[3] = { 0.3f, 0.3f, 0.3f };
        hta_props_add(&s->wfx.props, c, h, 0.1f * (float)i, HTA_RMAT_WOOD, 0, 0);
    }
    for (int i = 0; i < 4; i++) {
        float c[3] = { 4.0f + (float)i * 1.5f, -8.0f, 0.45f }, h[3] = { 0.25f, 0.25f, 0.45f };
        uint32_t b = hta_props_add(&s->wfx.props, c, h, 0, HTA_RMAT_METAL, 30, 0);
        if (b != UINT32_MAX) {
            s->wfx.props.props[b].explosive = true;
            s->wfx.props.props[b].blast_damage = 150; s->wfx.props.props[b].blast_radius = 3.0f;
        }
    }
    for (int i = 0; i < 3; i++) {
        float c[3] = { 0.0f, 4.0f + (float)i * 1.3f, 0.8f }, h[3] = { 0.03f, 0.6f, 0.8f };
        hta_props_add(&s->wfx.props, c, h, 0, HTA_RMAT_GLASS, 0, 0);
    }
    for (int i = 0; i < 3; i++) {
        float c[3] = { -4.0f + (float)i * 2.2f, -12.0f, 0.5f }, h[3] = { 1.0f, 0.3f, 0.5f };
        hta_props_add(&s->wfx.props, c, h, 0, HTA_RMAT_CONCRETE, 0, 0);
    }
    for (int i = 0; i < NDUMMY; i++) {
        g_dummy[i] = (dummy){ .pos = { 12.0f, -12.0f + (float)i * 2.0f, 0.0f }, .health = 100, .alive = true };
    }
}

static const float kSun[3] = { -0.4f, 0.3f, -0.85f };

static void setup(sandbox *s, const hta_gfx_settings *v)
{
    static hta_bsp_mesh arena;
    build_arena(&arena);
    hta_collision_build(&s->col, &arena);
    s->video = *v;
    hta_wfx_init(&s->wfx, &s->col, &s->video);
    reset_arena(s);
    hta_player_init(&s->player);
    s->player.pos[0] = -5; s->player.pos[1] = -5; s->player.pos[2] = 0.1f;
    hta_camera_init(&s->cam);
    s->cam.yaw = 0.7f;
    s->cam.znear = 0.02f; s->cam.zfar = 400.0f;
    memcpy(s->scene.light_dir, kSun, 12);
    for (int k = 0; k < 3; k++) { s->scene.light_color[k] = 0.6f; s->scene.ambient[k] = 0.24f; }
    s->scene.clear[0] = 0.46f; s->scene.clear[1] = 0.6f; s->scene.clear[2] = 0.78f;
    /* One box per prop and dummy slot, reusing the fx atlas. */
    s->box_slots = 128 + NDUMMY;
    s->boxes = s->wfx.fx.debris;                 /* same layout: 24 verts per box */
    hta_fx tmp;
    if (hta_fx_init(&tmp, s->box_slots, 1, 1, NULL)) {
        s->boxes = tmp.debris;
        tmp.debris.vertices = NULL; tmp.debris.indices = NULL; tmp.debris.submeshes = NULL;
        s->boxes.textures = &s->wfx.fx.atlas;
        hta_fx_free(&tmp);
    }
    s->box_verts = calloc((size_t)s->box_slots * 24, sizeof(hta_vertex));
    s->weather = HTA_WFX_WEATHER_AUTO;
    s->preset = v->preset;
}

static void upload(sandbox *s, hta_gfx *g)
{
    char err[256];
    static hta_bsp_mesh arena;
    if (!arena.vertices) build_arena(&arena);
    s->gpu_world = hta_gfx_mesh_upload(g, &arena, err, sizeof err);
    s->gpu_boxes = hta_gfx_mesh_upload_dynamic_world(g, &s->boxes, err, sizeof err);
    hta_wfx_gpu_upload(&s->wfx, g);
}

/* Intact props and standing dummies as lit boxes. */
static void build_boxes(sandbox *s)
{
    /* Borrow the debris builder: a throwaway rigid world holding one
     * sleeping body per box, then its vertices. */
    static hta_rigid_world w;
    if (!w.cap) hta_rigid_init(&w, s->box_slots, NULL);
    hta_rigid_clear(&w);
    for (uint32_t i = 0; i < s->wfx.props.count; i++) {
        const hta_prop *p = &s->wfx.props.props[i];
        if (p->broken) continue;
        hta_rigid_desc d;
        memset(&d, 0, sizeof d);
        d.shape = HTA_RIGID_BOX; d.material = (hta_rigid_material)p->material;
        memcpy(d.half, p->half, 12); memcpy(d.pos, p->centre, 12);
        d.rot[0] = cosf(p->yaw * 0.5f); d.rot[3] = sinf(p->yaw * 0.5f);
        hta_rigid_spawn(&w, &d);
    }
    for (int i = 0; i < NDUMMY; i++) {
        if (!g_dummy[i].alive) continue;
        hta_rigid_desc d;
        memset(&d, 0, sizeof d);
        d.shape = HTA_RIGID_BOX; d.material = HTA_RMAT_FLESH;
        d.half[0] = 0.12f; d.half[1] = 0.2f; d.half[2] = 0.35f;
        memcpy(d.pos, g_dummy[i].pos, 12); d.pos[2] += 0.35f;
        hta_rigid_spawn(&w, &d);
    }
    hta_fx fake;
    memset(&fake, 0, sizeof fake);
    fake.debris_slots = s->box_slots;
    fake.debris_verts = s->box_verts;
    hta_fx_build_debris(&fake, &w);
}

/* ------------------------------------------------------------------ play */

static void explode(sandbox *s, const float at[3], float damage, float radius)
{
    hta_game_event e;
    memset(&e, 0, sizeof e);
    e.kind = HTA_EV_DETONATE; e.pool = -1; e.dir[2] = 1; memcpy(e.pos, at, 12);
    hta_wfx_game_event(&s->wfx, &e, NULL);
    hta_props_blast(&s->wfx.props, at, damage, radius, &s->wfx.rigid, &s->wfx.fx);
    hta_fx_burst(&s->wfx.fx, HTA_BURST_SPARKS, at, e.dir, 40);
    for (int i = 0; i < NDUMMY; i++) {
        dummy *d = &g_dummy[i];
        if (!d->alive) continue;
        float c[3] = { d->pos[0], d->pos[1], d->pos[2] + 0.35f };
        float dx = c[0]-at[0], dy = c[1]-at[1], dz = c[2]-at[2];
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
        if (dist >= radius) continue;
        float hit = damage * (1.0f - dist / radius);
        d->health -= hit;
        if (d->health <= 0) {
            d->alive = false; d->respawn = 6.0f;
            float st;
            if (s->video.gib_level > 0 && hta_gibs_should(hit / 100.0f, &st)) {
                hta_gib_desc g = { .strength = st };
                memcpy(g.pos, c, 12); memcpy(g.from, at, 12);
                hta_gibs_spawn(&s->wfx.rigid, &s->wfx.fx, &g, s->video.gib_level);
                hta_wfx_push_cue(&s->wfx, HTA_WFX_CUE_GIB, HTA_RMAT_FLESH, c, st, false);
            }
        }
    }
    float d2 = (s->cam.pos[0]-at[0])*(s->cam.pos[0]-at[0]) + (s->cam.pos[1]-at[1])*(s->cam.pos[1]-at[1]);
    s->shake = fmaxf(s->shake, 0.6f / (1.0f + d2 * 0.05f));
}

static void shoot(sandbox *s)
{
    float fwd[3];
    hta_camera_forward(&s->cam, fwd);
    float far = 200.0f, t = far, n[3] = { 0, 0, 1 }, hit[3];
    bool world = hta_collision_ray(&s->col, s->cam.pos, fwd, far, &t, hit, n);
    uint32_t prop = UINT32_MAX;
    float pt, pn[3];
    /* Props are instances in the world grid, so the world ray already
     * stops on them; ask the props which one, for the damage. */
    if (hta_props_ray(&s->wfx.props, s->cam.pos, fwd, far, &prop, &pt, pn) && pt <= t + 1e-3f) {
        float at[3] = { s->cam.pos[0]+fwd[0]*pt, s->cam.pos[1]+fwd[1]*pt, s->cam.pos[2]+fwd[2]*pt };
        hta_props_damage(&s->wfx.props, prop, 15.0f, at, s->cam.pos, &s->wfx.rigid, &s->wfx.fx);
        return;
    }
    /* Dummies: a capsule-ish test against a vertical segment. */
    for (int i = 0; i < NDUMMY; i++) {
        dummy *d = &g_dummy[i];
        if (!d->alive) continue;
        float to[3] = { d->pos[0]-s->cam.pos[0], d->pos[1]-s->cam.pos[1], d->pos[2]+0.35f-s->cam.pos[2] };
        float along = to[0]*fwd[0] + to[1]*fwd[1] + to[2]*fwd[2];
        if (along <= 0 || along > t) continue;
        float px = to[0]-fwd[0]*along, py = to[1]-fwd[1]*along, pz = to[2]-fwd[2]*along;
        if (px*px + py*py < 0.2f*0.2f && fabsf(pz) < 0.4f) {
            float at[3] = { s->cam.pos[0]+fwd[0]*along, s->cam.pos[1]+fwd[1]*along, s->cam.pos[2]+fwd[2]*along };
            float back[3] = { fwd[0], fwd[1], fwd[2] + 0.2f };
            hta_fx_burst(&s->wfx.fx, HTA_BURST_BLOOD, at, back, 10);
            d->health -= 25;
            if (d->health <= 0) { d->alive = false; d->respawn = 6.0f; }
            return;
        }
    }
    if (world) {
        float up[3] = { n[0], n[1], n[2] };
        hta_fx_burst(&s->wfx.fx, HTA_BURST_DUST, hit, up, 3);
        hta_fx_burst(&s->wfx.fx, HTA_BURST_SPARKS, hit, up, 6);
    }
}

static void throw_grenade(sandbox *s)
{
    for (int k = 0; k < 8; k++) {
        if (s->nade_fuse[k] > 0) continue;
        float fwd[3];
        hta_camera_forward(&s->cam, fwd);
        hta_rigid_desc d;
        memset(&d, 0, sizeof d);
        d.shape = HTA_RIGID_SPHERE; d.material = HTA_RMAT_METAL; d.half[0] = 0.04f;
        for (int q = 0; q < 3; q++) { d.pos[q] = s->cam.pos[q] + fwd[q] * 0.3f; d.vel[q] = fwd[q] * 7.0f; }
        d.vel[2] += 1.5f;
        d.user = 0x6E000000u | (uint32_t)k;
        s->nade_body[k] = hta_rigid_spawn(&s->wfx.rigid, &d);
        s->nade_fuse[k] = 1.8f;
        return;
    }
}

static void step(sandbox *s, const hta_input *in, float dt)
{
    hta_player_input pi = { in->move_forward, in->move_right, in->look_yaw, in->look_pitch, in->jump, false, in->crouch };
    s->col.instances = s->merged;
    s->col.instance_count = hta_props_instances(&s->wfx.props, NULL, 0, s->merged, 300);
    hta_player_update(&s->player, &s->cam, &s->col, &pi, dt);
    if (in->fire_pressed) shoot(s);
    if (in->alt_pressed || in->key_pressed[SDL_SCANCODE_G]) throw_grenade(s);
    for (int k = 0; k < 8; k++) {
        if (s->nade_fuse[k] <= 0) continue;
        s->nade_fuse[k] -= dt;
        const hta_rigid_body *b = &s->wfx.rigid.bodies[s->nade_body[k]];
        if (s->nade_fuse[k] <= 0 && b->active && (b->user & 0xFF000000u) == 0x6E000000u) {
            float at[3];
            memcpy(at, b->pos, 12);
            hta_rigid_remove(&s->wfx.rigid, s->nade_body[k]);
            explode(s, at, 180.0f, 3.0f);
        }
    }
    /* Explosive props that broke raise their own blast. */
    hta_prop_event pe;
    while (hta_props_pop(&s->wfx.props, &pe))
        if (pe.kind == HTA_PROP_EV_EXPLODED) explode(s, pe.pos, pe.damage, pe.radius);
    for (int i = 0; i < NDUMMY; i++)
        if (!g_dummy[i].alive && (g_dummy[i].respawn -= dt) <= 0) { g_dummy[i].alive = true; g_dummy[i].health = 100; }
    hta_wfx_update(&s->wfx, dt, &s->cam);
    s->shake *= expf(-dt * 6.0f);
}

/* ------------------------------------------------------------------- main */

static bool frame(sandbox *s, hta_gfx *g, float dt)
{
    hta_gfx_settings look;
    hta_scene sc = s->scene;
    hta_wfx_frame_look(&s->wfx, &s->video, &look, sc.ambient, sc.light_color);
    hta_gfx_apply_settings(g, &look, NULL, 0);
    hta_wfx_gpu_frame(&s->wfx, g, &s->video, dt);
    build_boxes(s);
    hta_gfx_dynamic dyn[8];
    memset(dyn, 0, sizeof dyn);
    uint32_t n = 0;
    if (s->gpu_boxes) {
        dyn[n].mesh = s->gpu_boxes; dyn[n].vertices = s->box_verts;
        dyn[n].vertex_count = s->box_slots * 24; dyn[n].lit = true; n++;
    }
    n = hta_wfx_gpu_draw(&s->wfx, &s->cam, dyn, n, 8);
    hta_camera cam = s->cam;
    if (s->shake > 0.01f) {
        float t = (float)SDL_GetTicks() * 0.05f;
        cam.yaw += sinf(t * 1.7f) * s->shake * 0.02f;
        cam.pitch += cosf(t * 2.3f) * s->shake * 0.02f;
    }
    uint32_t w, h;
    hta_gfx_extent(g, &w, &h);
    cam.aspect = h ? (float)w / (float)h : 1.0f;
    cam.fov_y = 2.0f * atanf(tanf(s->video.fov_degrees * 3.14159265f / 360.0f) / fmaxf(cam.aspect, 1e-3f) * (16.0f / 9.0f));
    return hta_gfx_draw(g, &cam, &sc, s->gpu_world, NULL, NULL, dyn, n, NULL, NULL);
}

static void demo_script(sandbox *s, double t, hta_input *in)
{
    /* Aim at the crates and throw, then at the barrels, then the dummies. */
    memset(in, 0, sizeof(*in));
    static int stage = -1;
    int want = t < 2.5 ? 0 : t < 5.0 ? 1 : 2;
    const float tgt[3][3] = { { -8.8f, 10.4f, 0.4f }, { 6.2f, -8.0f, 0.4f }, { 12.0f, -8.0f, 0.4f } };
    float dx = tgt[want][0] - s->cam.pos[0], dy = tgt[want][1] - s->cam.pos[1], dz = tgt[want][2] - s->cam.pos[2];
    float yaw = atan2f(dy, dx), pitch = atan2f(dz, sqrtf(dx*dx + dy*dy)) + 0.25f;
    in->look_yaw = yaw - s->cam.yaw;
    in->look_pitch = pitch - s->cam.pitch;
    if (want != stage) { stage = want; in->key_pressed[SDL_SCANCODE_G] = true; }
    if (fmod(t, 0.4) < 0.017) in->fire_pressed = true;
}

int main(int argc, char **argv)
{
    hta_gfx_settings v;
    hta_gfx_settings_preset(&v, HTA_QUALITY_HIGH);
    const char *shot = NULL;
    double demo = 0;
    int weather = -2;
    for (int i = 1; i < argc; i++) {
        hta_quality q;
        hta_weather_kind wk;
        if (!strcmp(argv[i], "--preset") && i + 1 < argc && hta_quality_from_name(argv[i + 1], &q)) { hta_gfx_settings_preset(&v, q); i++; }
        else if (!strcmp(argv[i], "--weather") && i + 1 < argc && hta_weather_from_name(argv[i + 1], &wk)) { weather = (int)wk; i++; }
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) { sscanf(argv[++i], "%ux%u", &v.window_width, &v.window_height); }
        else if (!strcmp(argv[i], "--fullscreen")) v.window_mode = HTA_WINDOW_BORDERLESS;
        else if (!strcmp(argv[i], "--demo") && i + 1 < argc) demo = atof(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else { fprintf(stderr, "usage: %s [--preset P] [--weather W] [--size WxH] [--fullscreen] [--demo S] [--shot out.ppm]\n", argv[0]); return 2; }
    }
    hta_gfx_settings_clamp(&v);
    static sandbox s;
    setup(&s, &v);
    if (weather >= 0) hta_wfx_choose_weather(&s.wfx, weather);
    char err[512];

    if (shot) {
        /* Offscreen: the same frame code, read back to a file. */
        uint32_t w = v.window_width, h = v.window_height;
        hta_gfx *g = hta_gfx_create_offscreen_ex(w, h, &v, err, sizeof err);
        if (!g) { fprintf(stderr, "renderer: %s\n", err); return 1; }
        upload(&s, g);
        double t = 0, len = demo > 0 ? demo : 4.0;
        hta_input in;
        for (; t < len; t += 1.0 / 60.0) {
            demo_script(&s, t, &in);
            step(&s, &in, 1.0f / 60.0f);
            if (!frame(&s, g, 1.0f / 60.0f)) { fprintf(stderr, "draw failed\n"); return 1; }
        }
        uint8_t *px = malloc((size_t)w * h * 4);
        hta_gfx_readback(g, px, (size_t)w * h * 4);
        FILE *f = fopen(shot, "wb");
        if (!f) return 1;
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (uint32_t i = 0; i < w * h; i++) fwrite(px + i * 4, 1, 3, f);
        fclose(f);
        printf("sandbox: %s, %u debris, %u sprites, %u props broken, preset %s (%s)\n", shot,
               hta_rigid_active(&s.wfx.rigid), hta_fx_live(&s.wfx.fx), 0u, hta_quality_name(v.preset),
               hta_gfx_device_name(g));
        free(px);
        hta_wfx_gpu_free(&s.wfx, g);
        if (s.gpu_boxes) hta_gfx_mesh_free(g, s.gpu_boxes);
        if (s.gpu_world) hta_gfx_mesh_free(g, s.gpu_world);
        hta_gfx_destroy(g);
        return 0;
    }

    hta_desktop *d = hta_desktop_open("Megamod Sandbox", &v, err, sizeof err);
    if (!d) { fprintf(stderr, "desktop: %s\n", err); return 1; }
    hta_gfx *g = hta_desktop_gfx(d);
    upload(&s, g);
    printf("sandbox: %s, preset %s\n", hta_gfx_device_name(g), hta_quality_name(v.preset));
    /* Sound: none on a box without a device, and that is fine. */
    if (hta_audio_sdl_start(&s.audio, err, sizeof err)) {
        if (hta_wfx_audio_init(&s.wfx_audio, &s.audio, 0x5A7Du))
            printf("sandbox: audio %u Hz, %.1f MB of procedural sound\n", s.audio.out_rate,
                   (double)s.wfx_audio.bank.bytes / 1048576.0);
    } else fprintf(stderr, "sandbox: silent (%s)\n", err);
    hta_input in;
    memset(&in, 0, sizeof in);
    double t0 = hta_desktop_time(d), title_at = 0;
    uint32_t frames = 0;
    bool captured = false;
    while (hta_desktop_poll(d, &in, 0.0022f)) {
        float dt = hta_desktop_pace(d, v.fps_cap);
        double now = hta_desktop_time(d) - t0;
        if (demo > 0) {
            if (now > demo) break;
            demo_script(&s, now, &in);
        }
        if (in.fire_pressed && !captured && demo <= 0) { hta_desktop_capture_mouse(d, true); captured = true; in.fire_pressed = false; }
        if (in.key_pressed[SDL_SCANCODE_ESCAPE]) {
            if (captured) { hta_desktop_capture_mouse(d, false); captured = false; } else break;
        }
        if (in.key_pressed[SDL_SCANCODE_F2]) {
            s.preset = (hta_quality)((s.preset + 1) % HTA_QUALITY_CUSTOM);
            hta_gfx_settings n;
            hta_gfx_settings_preset(&n, s.preset);
            n.window_mode = v.window_mode; n.window_width = v.window_width; n.window_height = v.window_height;
            v = n;
            s.video = v;
            hta_wfx_settings(&s.wfx, &v);
            if (!hta_desktop_apply(d, &v, err, sizeof err)) fprintf(stderr, "preset: %s\n", err);
        }
        if (in.key_pressed[SDL_SCANCODE_F3]) {
            s.weather = (s.weather + 2) % (HTA_WEATHER_COUNT + 1) - 1;
            hta_wfx_choose_weather(&s.wfx, s.weather);
        }
        if (in.key_pressed[SDL_SCANCODE_F4]) { v.gib_level = (v.gib_level + 1) % 3; s.video = v; hta_wfx_settings(&s.wfx, &v); }
        if (in.key_pressed[SDL_SCANCODE_F5]) { v.dynamic_res = !v.dynamic_res; s.video = v; }
        if (in.key_pressed[SDL_SCANCODE_F11]) {
            v.window_mode = v.window_mode == HTA_WINDOW_WINDOWED ? HTA_WINDOW_BORDERLESS : HTA_WINDOW_WINDOWED;
            hta_desktop_apply(d, &v, err, sizeof err);
        }
        if (in.key_pressed[SDL_SCANCODE_R]) reset_arena(&s);
        step(&s, &in, dt);
        hta_wfx_audio_update(&s.wfx_audio, &s.wfx, &s.cam, dt, false);
        if (!frame(&s, g, dt)) {
            uint32_t w, h;
            hta_desktop_size(d, &w, &h);
            hta_gfx_resize(g, w, h, err, sizeof err);
        }
        frames++;
        if (now - title_at > 0.5) {
            char title[256];
            snprintf(title, sizeof title, "Megamod Sandbox | %s | %.0f fps | scale %.2f | debris %u | weather %s | gore %u | F2 preset F3 weather F4 gore",
                     hta_quality_name(v.preset), (double)frames / (now - title_at), (double)hta_gfx_render_scale(g),
                     hta_rigid_active(&s.wfx.rigid), hta_weather_name(s.wfx.weather.kind), v.gib_level);
            hta_desktop_set_title(d, title);
            title_at = now; frames = 0;
        }
    }
    hta_audio_sdl_stop();
    if (s.wfx_audio.ready)
        printf("sandbox: %u sounds played, %u voices stolen, %u requests dropped\n", s.wfx_audio.played,
               s.audio.stolen, (unsigned)atomic_load(&s.audio.dropped));
    hta_wfx_audio_free(&s.wfx_audio);
    hta_wfx_gpu_free(&s.wfx, g);
    if (s.gpu_boxes) hta_gfx_mesh_free(g, s.gpu_boxes);
    if (s.gpu_world) hta_gfx_mesh_free(g, s.gpu_world);
    hta_desktop_close(d);
    return 0;
}
