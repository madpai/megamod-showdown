/* Effects on top of rigid bodies: the procedural atlas, sprites and
 * splats, gibs, destructible props, and weather. */
#include "engine/fx.h"
#include "engine/gore.h"
#include "engine/props.h"
#include "engine/weather.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static hta_vertex V[64];
static uint32_t I[96];
static uint32_t nv, ni;

static void quad(const float a[3], const float b[3], const float c[3], const float d[3])
{
    const float *p[6] = { a, b, c, a, c, d };
    for (int i = 0; i < 6; i++) { memset(&V[nv], 0, sizeof(V[nv])); memcpy(V[nv].pos, p[i], 12); I[ni++] = nv++; }
}

/* A floor 60 x 60 at z=0, and a roof 4 wu up over x,y in [10,20]. */
static void build(hta_bsp_mesh *m, hta_collision *col)
{
    nv = ni = 0;
    { float a[3]={-30,-30,0}, b[3]={30,-30,0}, c[3]={30,30,0}, d[3]={-30,30,0}; quad(a,b,c,d); }
    { float a[3]={10,10,4}, b[3]={20,10,4}, c[3]={20,20,4}, d[3]={10,20,4}; quad(a,b,c,d); }
    memset(m, 0, sizeof(*m));
    m->vertices = V; m->vertex_count = nv; m->indices = I; m->index_count = ni;
    m->bounds_min[0] = -30; m->bounds_min[1] = -30; m->bounds_max[0] = 30; m->bounds_max[1] = 30; m->bounds_max[2] = 4;
    assert(hta_collision_build(col, m));
}

static const uint8_t *texel(const hta_bsp_texture *t, uint32_t tile, uint32_t x, uint32_t y)
{
    uint32_t ax = (tile % 4u) * (HTA_FX_ATLAS / 4u) + x, ay = (tile / 4u) * (HTA_FX_ATLAS / 4u) + y;
    return t->rgba + ((size_t)ay * HTA_FX_ATLAS + ax) * 4u;
}

static void atlas(void)
{
    hta_bsp_texture a, b;
    assert(hta_fx_atlas(&a) && hta_fx_atlas(&b));
    assert(a.width == HTA_FX_ATLAS && a.height == HTA_FX_ATLAS);
    assert(memcmp(a.rgba, b.rgba, (size_t)HTA_FX_ATLAS * HTA_FX_ATLAS * 4) == 0);   /* deterministic */
    /* Material tiles are opaque; sprite tiles fade to nothing at the rim. */
    for (int t = HTA_TILE_WOOD; t <= HTA_TILE_DIRT; t++) assert(texel(&a, (uint32_t)t, 7, 90)[3] == 255);
    assert(texel(&a, HTA_TILE_BLOB, 0, 0)[3] == 0 && texel(&a, HTA_TILE_BLOB, 64, 64)[3] > 200);
    /* The additive spark is premultiplied: black where transparent. */
    const uint8_t *e = texel(&a, HTA_TILE_SPARK, 2, 2);
    assert(e[0] == 0 && e[3] == 0);
    /* Wood is brown, flesh is red, metal is grey-blue. */
    const uint8_t *w = texel(&a, HTA_TILE_WOOD, 40, 40), *f = texel(&a, HTA_TILE_FLESH, 40, 40), *m = texel(&a, HTA_TILE_METAL, 40, 40);
    assert(w[0] > w[2] + 20 && f[0] > f[1] + 40 && m[2] >= m[0]);
    hta_fx_atlas_free(&a); hta_fx_atlas_free(&b);
}

static void sprites_and_splats(const hta_collision *col)
{
    hta_fx fx;
    assert(hta_fx_init(&fx, 16, 256, 64, col));
    float p[3] = { 0, 0, 1.0f }, d[3] = { 1, 0, 0.3f };
    hta_fx_burst(&fx, HTA_BURST_BLOOD, p, d, 40);
    uint32_t live = hta_fx_live(&fx);
    assert(live >= 40);
    /* The drops fall, strike the floor and leave splats lying on it. */
    for (int i = 0; i < 120; i++) hta_fx_update(&fx, 1.0f / 60.0f);
    printf("blood: %u splats\n", fx.splats);
    assert(fx.splats >= 20);
    uint32_t flat = 0;
    for (uint32_t i = 0; i < fx.alpha_slots; i++)
        if (fx.pool[i].active && fx.pool[i].mode == HTA_SPRITE_FLAT) {
            flat++;
            assert(fabsf(fx.pool[i].pos[2]) < 0.01f && fx.pool[i].normal[2] > 0.99f);
        }
    assert(flat >= 20);

    /* Density scales bursts. */
    fx.density = 0.25f;
    uint32_t before = hta_fx_live(&fx);
    hta_fx_burst(&fx, HTA_BURST_DUST, p, d, 40);
    assert(hta_fx_live(&fx) - before == 10);

    /* Vertices: live sprites have area, dead slots are collapsed. */
    hta_camera cam;
    hta_camera_init(&cam);
    cam.pos[0] = -3; cam.pos[2] = 1.5f;
    hta_fx_build_sprites(&fx, &cam);
    uint32_t drawn = 0;
    for (uint32_t i = 0; i < fx.alpha_slots + fx.add_slots; i++) {
        const hta_vertex *v = fx.sprite_verts + i * 4u;
        float e1[3] = { v[1].pos[0]-v[0].pos[0], v[1].pos[1]-v[0].pos[1], v[1].pos[2]-v[0].pos[2] };
        float e2[3] = { v[3].pos[0]-v[0].pos[0], v[3].pos[1]-v[0].pos[1], v[3].pos[2]-v[0].pos[2] };
        float cr[3] = { e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0] };
        float area = sqrtf(cr[0]*cr[0] + cr[1]*cr[1] + cr[2]*cr[2]);
        if (fx.pool[i].active) { assert(area > 1e-6f); drawn++; }
        else assert(area == 0.0f);
    }
    assert(drawn == hta_fx_live(&fx));
    /* Sprites fade and go. */
    for (int i = 0; i < 60 * 30; i++) hta_fx_update(&fx, 1.0f / 60.0f);
    assert(hta_fx_live(&fx) == 0);
    hta_fx_free(&fx);
}

static void gibs(const hta_collision *col)
{
    float st;
    assert(!hta_gibs_should(0.3f, &st));
    assert(hta_gibs_should(0.8f, &st) && st > 0.3f && st <= 1.0f);
    assert(hta_gibs_should(5.0f, &st) && st == 1.0f);

    hta_rigid_world w;
    hta_fx fx;
    assert(hta_rigid_init(&w, 64, col));
    assert(hta_fx_init(&fx, 64, 512, 64, col));
    hta_gib_desc d = { .pos = { 0, 0, 0.4f }, .from = { -1, 0, 0.1f }, .strength = 1.0f };
    assert(hta_gibs_spawn(&w, &fx, &d, 0) == 0);             /* gore off */
    uint32_t n = hta_gibs_spawn(&w, &fx, &d, 2);
    assert(n >= 10 && hta_rigid_active(&w) == n);
    /* Thrown away from the blast: on average toward +x, and up. */
    float mx = 0, mz = 0;
    for (uint32_t i = 0; i < w.cap; i++) if (w.bodies[i].active) { mx += w.bodies[i].vel[0]; mz += w.bodies[i].vel[2]; }
    assert(mx > 0 && mz > 0);
    uint32_t splats0 = fx.splats;
    for (int i = 0; i < 240; i++) {
        hta_rigid_step(&w, 1.0f / 60.0f);
        hta_gibs_update(&w, &fx, 1.0f / 60.0f, 2);
        hta_fx_update(&fx, 1.0f / 60.0f);
    }
    printf("gibs: %u chunks, %u splats\n", n, fx.splats - splats0);
    assert(fx.splats - splats0 >= 8);
    /* Everything lands on the floor. */
    for (uint32_t i = 0; i < w.cap; i++) if (w.bodies[i].active) assert(w.bodies[i].pos[2] > -0.01f && w.bodies[i].pos[2] < 0.3f);
    /* Debris vertices: 24 per body, each body's box around its centre. */
    hta_fx_build_debris(&fx, &w);
    const hta_vertex *v = fx.debris_verts;
    for (uint32_t i = 0; i < fx.debris_slots && i < w.cap; i++) {
        if (!w.bodies[i].active) continue;
        float c[3] = { 0, 0, 0 };
        for (int k = 0; k < 24; k++) for (int q = 0; q < 3; q++) c[q] += v[i * 24 + (uint32_t)k].pos[q] / 24.0f;
        for (int q = 0; q < 3; q++) assert(fabsf(c[q] - w.bodies[i].pos[q]) < 1e-3f);
    }
    hta_fx_free(&fx);
    hta_rigid_free(&w);
}

static void props(hta_collision *col)
{
    hta_props P;
    hta_rigid_world w;
    hta_fx fx;
    assert(hta_props_init(&P, 8));
    assert(hta_rigid_init(&w, 128, col));
    assert(hta_fx_init(&fx, 128, 256, 64, col));
    float c[3] = { 5, 0, 0.3f }, h[3] = { 0.3f, 0.3f, 0.3f };
    uint32_t crate = hta_props_add(&P, c, h, 0.4f, HTA_RMAT_WOOD, 0, 42);
    float c2[3] = { -5, 0, 0.5f }, h2[3] = { 0.25f, 0.25f, 0.5f };
    uint32_t barrel = hta_props_add(&P, c2, h2, 0, HTA_RMAT_METAL, 30, 43);
    assert(crate == 0 && barrel == 1);
    P.props[barrel].explosive = true;
    P.props[barrel].blast_damage = 100; P.props[barrel].blast_radius = 3;

    /* Whole, it is solid: the ground under its middle is its top. */
    hta_collision_instance merged[8];
    col->instances = merged;
    col->instance_count = hta_props_instances(&P, NULL, 0, merged, 8);
    float gz;
    assert(hta_collision_ground(col, 5, 0, 3.0f, &gz) && fabsf(gz - 0.6f) < 1e-3f);

    /* Bullets: a ray from the west hits its west face. */
    float o[3] = { 0, 0, 0.3f }, dir[3] = { 1, 0, 0 }, t, n[3];
    uint32_t hit;
    assert(hta_props_ray(&P, o, dir, 20, &hit, &t, n) && hit == crate);
    assert(n[0] < -0.5f && t > 4.5f && t < 5.0f);

    /* Chip damage holds, enough breaks it. */
    float from[3] = { 0, 0, 0.3f };
    assert(!hta_props_damage(&P, crate, 10, c, from, &w, &fx));
    assert(!P.props[crate].broken && hta_rigid_active(&w) == 0);
    assert(hta_props_damage(&P, crate, 60, c, from, &w, &fx));
    assert(P.props[crate].broken && hta_rigid_active(&w) >= 4);
    hta_prop_event e;
    assert(hta_props_pop(&P, &e) && e.kind == HTA_PROP_EV_BROKE && e.prop == crate);
    assert(!hta_props_ray(&P, o, dir, 20, &hit, &t, n) || hit != crate);
    /* Broken, it is no longer solid (re-merge each frame). */
    col->instance_count = hta_props_instances(&P, NULL, 0, merged, 8);
    assert(!hta_collision_ground(col, 5, 0, 3.0f, &gz) || gz < 0.1f);
    /* Its chunks were thrown away from the shooter (toward +x). */
    float mx = 0;
    for (uint32_t i = 0; i < w.cap; i++) if (w.bodies[i].active) mx += w.bodies[i].vel[0];
    assert(mx > 0);

    /* A blast: the barrel inside the radius breaks, and being explosive
     * raises its own blast for the caller. Far away, nothing. */
    float far[3] = { -5, 20, 0 };
    hta_props_blast(&P, far, 200, 3, &w, &fx);
    assert(!P.props[barrel].broken);
    float near[3] = { -5.8f, 0, 0.2f };
    hta_props_blast(&P, near, 60, 3, &w, &fx);
    assert(P.props[barrel].broken);
    bool exploded = false;
    while (hta_props_pop(&P, &e)) if (e.kind == HTA_PROP_EV_EXPLODED) exploded = e.damage == 100 && e.radius == 3;
    assert(exploded);

    /* Respawn. */
    P.props[crate].respawn_time = 2.0f; P.props[crate].respawn_in = 2.0f;
    hta_props_update(&P, 1.0f);
    assert(P.props[crate].broken);
    hta_props_update(&P, 1.5f);
    assert(!P.props[crate].broken && P.props[crate].health == P.props[crate].max_health);
    assert(hta_props_pop(&P, &e) && e.kind == HTA_PROP_EV_RESPAWNED);
    col->instances = NULL; col->instance_count = 0;

    /* LAN: the host's mask says the barrel is broken; a client with the
     * same props, remote, breaks it with the same events and nothing it
     * does locally breaks or restores a prop. */
    {
        uint8_t mask[64];
        memset(mask, 0xAA, sizeof(mask));
        assert(hta_props_broken_mask(&P, mask, 512) == 2);
        assert(mask[0] == (1u << barrel) && mask[1] == 0);
        hta_props Q;
        assert(hta_props_init(&Q, 8));
        hta_props_add(&Q, c, h, 0.4f, HTA_RMAT_WOOD, 0, 42);
        hta_props_add(&Q, c2, h2, 0, HTA_RMAT_METAL, 30, 43);
        Q.props[barrel].explosive = true;
        Q.props[barrel].blast_damage = 100; Q.props[barrel].blast_radius = 3;
        Q.props[crate].respawn_time = 1.0f;
        Q.remote = true;
        assert(!hta_props_damage(&Q, crate, 500, c, from, &w, &fx) && !Q.props[crate].broken);
        hta_props_blast(&Q, c, 500, 5, &w, &fx);
        assert(!Q.props[crate].broken && !Q.props[barrel].broken && Q.event_count == 0);
        hta_props_apply_mask(&Q, mask, 2, &w, &fx);
        assert(Q.props[barrel].broken && !Q.props[barrel].inst->active && !Q.props[crate].broken);
        bool broke = false, boom = false;
        while (hta_props_pop(&Q, &e)) { broke |= e.kind == HTA_PROP_EV_BROKE; boom |= e.kind == HTA_PROP_EV_EXPLODED; }
        assert(broke && boom);
        /* The same mask again changes nothing. */
        hta_props_apply_mask(&Q, mask, 2, &w, &fx);
        assert(Q.event_count == 0);
        /* The host breaks the crate and restores the barrel. */
        mask[0] = (uint8_t)(1u << crate);
        hta_props_apply_mask(&Q, mask, 2, &w, &fx);
        assert(Q.props[crate].broken && !Q.props[barrel].broken && Q.props[barrel].inst->active);
        /* Remote: no local respawn however long it waits. */
        hta_props_update(&Q, 10.0f);
        assert(Q.props[crate].broken);
        /* A count short of the props leaves the rest alone. */
        memset(mask, 0, sizeof(mask));
        hta_props_apply_mask(&Q, mask, 0, &w, &fx);
        assert(Q.props[crate].broken);
        /* A quiet sync: the barrel breaks, but raises no explosion. */
        while (hta_props_pop(&Q, &e)) {}
        mask[0] = (uint8_t)((1u << crate) | (1u << barrel));
        hta_props_apply_mask(&Q, mask, 2, NULL, NULL);
        assert(Q.props[barrel].broken && hta_props_pop(&Q, &e) && e.kind == HTA_PROP_EV_BROKE && Q.event_count == 0);
        hta_props_free(&Q);
    }

    hta_fx_free(&fx);
    hta_rigid_free(&w);
    hta_props_free(&P);
}

static void weather(const hta_collision *col)
{
    hta_bsp_texture atlas;
    assert(hta_fx_atlas(&atlas));
    hta_weather W;
    assert(hta_weather_init(&W, 2000, &atlas, col));
    hta_weather_kind k;
    assert(hta_weather_from_name("SandStorm", &k) && k == HTA_WEATHER_SANDSTORM);
    assert(!hta_weather_from_name("hail", &k));
    hta_camera cam;
    hta_camera_init(&cam);
    cam.pos[0] = 15; cam.pos[1] = 15; cam.pos[2] = 1.0f;   /* under the roof */
    float wind[2] = { 0.5f, 0 };
    hta_weather_set(&W, HTA_WEATHER_RAIN, 1.0f, wind);
    for (int i = 0; i < 120; i++) hta_weather_update(&W, 1.0f / 60.0f, &cam, NULL);
    assert(W.live == 1400);                   /* 70% of 2000 for rain */
    /* The roof map knows the roof overhead and the open sky beside it. */
    assert(!hta_weather_open(&W, 15, 15, 2.0f));
    assert(hta_weather_open(&W, 15, 15, 5.0f));
    assert(hta_weather_open(&W, 5, 5, 2.0f));
    /* No drop is drawn under the roof. */
    for (uint32_t i = 0; i < W.live; i++) {
        const hta_drop *d = &W.drops[i];
        bool under = d->pos[0] > 10.5f && d->pos[0] < 19.5f && d->pos[1] > 10.5f && d->pos[1] < 19.5f && d->pos[2] < 3.8f;
        assert(!under);
    }
    hta_weather_build(&W, &cam);
    /* Density halves the drops. */
    W.density = 0.5f;
    hta_weather_update(&W, 1.0f / 60.0f, &cam, NULL);
    assert(W.live == 700);
    /* A storm flashes and thunders after the sound's delay. */
    hta_weather_set(&W, HTA_WEATHER_STORM, 1.0f, wind);
    float maxflash = 0, gain = 0;
    bool thunder = false;
    for (int i = 0; i < 60 * 40 && !thunder; i++) {
        hta_weather_update(&W, 1.0f / 60.0f, &cam, NULL);
        if (W.flash > maxflash) maxflash = W.flash;
        thunder = hta_weather_thunder(&W, &gain);
    }
    assert(maxflash > 0.4f && thunder && gain > 0.2f);
    /* Sandstorms want thick brown fog; clear wants none. */
    float c[3], wt, dm;
    hta_weather_set(&W, HTA_WEATHER_SANDSTORM, 1.0f, wind);
    hta_weather_atmosphere(&W, c, &wt, &dm);
    assert(dm > 5 && c[0] > c[2] && wt > 0.5f);
    hta_weather_set(&W, HTA_WEATHER_CLEAR, 1.0f, wind);
    hta_weather_update(&W, 0.1f, &cam, NULL);
    hta_weather_atmosphere(&W, c, &wt, &dm);
    assert(W.live == 0 && wt == 0 && dm == 1.0f);
    hta_weather_free(&W);
    hta_fx_atlas_free(&atlas);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    hta_bsp_mesh m;
    hta_collision col;
    build(&m, &col);
    atlas();
    sprites_and_splats(&col);
    gibs(&col);
    props(&col);
    weather(&col);
    hta_collision_free(&col);
    puts("world fx OK");
    return 0;
}
