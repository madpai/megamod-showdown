#include "world_fx.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

/* Pool sizes. Sprites are cheap (a quad) but blend; debris is a lit box.
 * The debris pool follows the max_debris setting; sprites scale with it. */
static uint32_t sprite_slots(const hta_gfx_settings *s)
{
    uint32_t n = 256u + s->max_debris * 3u;
    return n > 2048u ? 2048u : n;
}
static uint32_t weather_drops(const hta_gfx_settings *s)
{
    return s->weather_density > 0.0f ? 3000u : 1u;
}

bool hta_wfx_init(hta_world_fx *w, const hta_collision *col, const hta_gfx_settings *s)
{
    if (!w || !s) return false;
    memset(w, 0, sizeof(*w));
    uint32_t debris = s->max_debris ? s->max_debris : 1u;
    if (!hta_rigid_init(&w->rigid, debris, col)) return false;
    if (!hta_fx_init(&w->fx, debris, sprite_slots(s), 128u, col)) { hta_rigid_free(&w->rigid); return false; }
    if (!hta_weather_init(&w->weather, weather_drops(s), &w->fx.atlas, col)) {
        hta_fx_free(&w->fx); hta_rigid_free(&w->rigid); return false;
    }
    hta_props_init(&w->props, 256);
    w->weather_choice = HTA_WFX_WEATHER_AUTO;
    w->dynres = *s;
    hta_wfx_settings(w, s);
    w->ready = true;
    return true;
}

void hta_wfx_free(hta_world_fx *w)
{
    if (!w) return;
    hta_props_free(&w->props);
    hta_weather_free(&w->weather);
    hta_fx_free(&w->fx);
    hta_rigid_free(&w->rigid);
    w->ready = false;
}

void hta_wfx_settings(hta_world_fx *w, const hta_gfx_settings *s)
{
    if (!w || !s) return;
    /* The debris pool is fixed to the fx mesh's slots, which were sized at
     * init; shrinking is free, growing waits for the next init. */
    uint32_t cap = s->max_debris < w->fx.debris_slots ? s->max_debris : w->fx.debris_slots;
    hta_rigid_resize(&w->rigid, cap ? cap : 1u);
    w->fx.density = s->particle_density;
    w->weather.density = s->weather_density;
    w->gib_level = s->gib_level;
    /* Chunks per broken prop follow the budget: 6 on a potato, 20 up top. */
    uint32_t c = s->max_debris / 12u;
    w->props.chunks_per_break = c < 6u ? 6u : c > 20u ? 20u : c;
    float ceiling = w->dynres.render_scale;
    w->dynres = *s;
    if (ceiling > 0.0f) w->dynres.render_scale = ceiling;
}

void hta_wfx_reset(hta_world_fx *w)
{
    if (!w || !w->ready) return;
    hta_rigid_clear(&w->rigid);
    for (uint32_t i = 0; i < w->fx.alpha_slots + w->fx.add_slots; i++) w->fx.pool[i].active = false;
    memset(w->weather.roof_known, 0, sizeof(w->weather.roof_known));
    w->weather.live = 0;
}

static void apply_weather(hta_world_fx *w)
{
    hta_weather_kind k = w->weather_choice == HTA_WFX_WEATHER_AUTO ? w->map_weather
                                                                   : (hta_weather_kind)w->weather_choice;
    float I = w->weather_choice == HTA_WFX_WEATHER_AUTO ? w->map_weather_intensity : 1.0f;
    float wind[2] = { 0.3f, 0.15f };
    if (k == HTA_WEATHER_STORM) { wind[0] = 0.9f; wind[1] = 0.35f; }
    if (k == HTA_WEATHER_SANDSTORM) { wind[0] = 6.0f; wind[1] = 1.5f; }
    hta_weather_set(&w->weather, k, I > 0.0f ? I : 1.0f, wind);
}

void hta_wfx_load_map(hta_world_fx *w, const hta_external_map *m, float respawn)
{
    if (!w || !w->ready) return;
    uint32_t chunks = w->props.chunks_per_break;
    hta_props_free(&w->props);
    uint32_t n = m ? m->breakable_count : 0;
    hta_props_init(&w->props, n > 256 ? n : 256);
    w->props.chunks_per_break = chunks ? chunks : 10;
    static const hta_rigid_material mat[4] = { HTA_RMAT_WOOD, HTA_RMAT_METAL, HTA_RMAT_CONCRETE, HTA_RMAT_GLASS };
    for (uint32_t i = 0; i < n; i++) {
        const hta_external_breakable *b = &m->breakables[i];
        float c[3], h[3];
        for (int k = 0; k < 3; k++) {
            c[k] = 0.5f * (b->min[k] + b->max[k]);
            h[k] = 0.5f * (b->max[k] - b->min[k]);
            if (h[k] < 0.02f) h[k] = 0.02f;         /* a flat model still needs a box */
        }
        uint32_t p = hta_props_add(&w->props, c, h, 0.0f, mat[b->material < 4 ? b->material : 0],
                                   b->health, i + 1u);   /* user: the submesh tag */
        if (p == UINT32_MAX) continue;
        hta_prop *pr = &w->props.props[p];
        pr->respawn_time = respawn;
        pr->explosive = b->explosive;
        pr->blast_damage = b->blast_damage > 0 ? b->blast_damage : 150.0f;
        pr->blast_radius = b->blast_radius > 0 ? b->blast_radius : 3.0f;
    }
    if (m && m->weather >= 1 && m->weather < HTA_WEATHER_COUNT)
        hta_wfx_set_map_weather(w, (hta_weather_kind)m->weather, m->weather_intensity);
    else
        hta_wfx_set_map_weather(w, HTA_WEATHER_CLEAR, 0.0f);
}

void hta_wfx_set_map_weather(hta_world_fx *w, hta_weather_kind k, float intensity)
{
    if (!w) return;
    w->map_weather = k;
    w->map_weather_intensity = intensity;
    apply_weather(w);
}

void hta_wfx_choose_weather(hta_world_fx *w, int choice)
{
    if (!w) return;
    w->weather_choice = (choice >= 0 && choice < HTA_WEATHER_COUNT) ? choice : HTA_WFX_WEATHER_AUTO;
    apply_weather(w);
}

/* Clods of earth and a column of dust thrown up by an explosion, and
 * anything already lying about gets thrown again. */
static void detonation(hta_world_fx *w, const float pos[3], const float nrm[3], float radius)
{
    float r = radius > 0.1f ? radius : 1.0f;
    hta_rigid_blast(&w->rigid, pos, r * 2.0f, 2.5f + r);
    hta_props_blast(&w->props, pos, 60.0f * r, r * 1.5f, &w->rigid, &w->fx);
    float up[3] = { 0, 0, 1 };
    if (nrm && (nrm[0] || nrm[1] || nrm[2])) memcpy(up, nrm, sizeof(up));
    hta_fx_burst(&w->fx, HTA_BURST_DUST, pos, up, 8);
    /* Only off the ground (a normal that points up), and only a few. */
    if (up[2] < 0.5f) return;
    uint32_t n = (uint32_t)(4.0f * w->fx.density + 0.5f);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t s = w->rigid.rng = w->rigid.rng * 1664525u + 1013904223u;
        float jx = (float)((s >> 8) & 255) / 127.5f - 1.0f, jy = (float)((s >> 16) & 255) / 127.5f - 1.0f;
        float jz = (float)((s >> 24) & 255) / 255.0f;
        hta_rigid_desc d;
        memset(&d, 0, sizeof(d));
        d.shape = HTA_RIGID_BOX; d.material = HTA_RMAT_DIRT;
        d.half[0] = 0.02f + 0.02f * jz; d.half[1] = d.half[0] * 0.8f; d.half[2] = d.half[0] * 0.6f;
        d.pos[0] = pos[0] + jx * 0.1f; d.pos[1] = pos[1] + jy * 0.1f; d.pos[2] = pos[2] + 0.05f;
        d.vel[0] = jx * 2.0f; d.vel[1] = jy * 2.0f; d.vel[2] = 2.0f + 2.0f * jz;
        d.ang[0] = jx * 10.0f; d.ang[1] = jy * 10.0f;
        d.life = 4.0f; d.fade = 1.0f;
        hta_rigid_spawn(&w->rigid, &d);
    }
}

/* A wrecked hull sheds panels. */
static void wreck(hta_world_fx *w, const float pos[3])
{
    hta_rigid_blast(&w->rigid, pos, 4.0f, 5.0f);
    hta_fx_burst(&w->fx, HTA_BURST_SPARKS, pos, NULL, 30);
    hta_fx_burst(&w->fx, HTA_BURST_DUST, pos, NULL, 10);
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t s = w->rigid.rng = w->rigid.rng * 1664525u + 1013904223u;
        float jx = (float)((s >> 8) & 255) / 127.5f - 1.0f, jy = (float)((s >> 16) & 255) / 127.5f - 1.0f;
        float jz = (float)((s >> 24) & 255) / 255.0f;
        hta_rigid_desc d;
        memset(&d, 0, sizeof(d));
        d.shape = HTA_RIGID_BOX; d.material = HTA_RMAT_METAL;
        d.half[0] = 0.06f + 0.08f * jz; d.half[1] = 0.04f + 0.05f * (1.0f - jz); d.half[2] = 0.008f;
        d.pos[0] = pos[0] + jx * 0.4f; d.pos[1] = pos[1] + jy * 0.4f; d.pos[2] = pos[2] + 0.3f + jz * 0.2f;
        d.vel[0] = jx * 3.0f; d.vel[1] = jy * 3.0f; d.vel[2] = 2.0f + 2.5f * jz;
        d.ang[0] = jy * 12.0f; d.ang[1] = jx * 12.0f; d.ang[2] = jz * 6.0f;
        d.life = 14.0f; d.fade = 2.0f;
        hta_rigid_spawn(&w->rigid, &d);
    }
}

void hta_wfx_game_event(hta_world_fx *w, const hta_game_event *e, const hta_game *g)
{
    if (!w || !w->ready || !e) return;
    switch (e->kind) {
    case HTA_EV_KILL: {
        if (!g || e->a < 0 || (uint32_t)e->a >= g->unit_count) break;
        const hta_unit *u = &g->units[e->a];
        if (!u->gibbed) break;
        hta_gib_desc d;
        memset(&d, 0, sizeof(d));
        memcpy(d.pos, e->pos, sizeof(d.pos));
        memcpy(d.vel, u->body.velocity, sizeof(d.vel));
        memcpy(d.from, e->dir, sizeof(d.from));
        hta_gibs_should(e->amount, &d.strength);
        /* A body's height: its standing eye plus a little. */
        d.height = u->body.eye_height > 0.1f ? u->body.eye_height * 1.12f : 0.7f;
        hta_gibs_spawn(&w->rigid, &w->fx, &d, w->gib_level);
        w->gibbed++;
        break;
    }
    case HTA_EV_DETONATE: {
        float radius = 1.0f;
        if (g && e->pool >= 0 && (uint32_t)e->pool < g->pool_count) {
            float r = g->pools[e->pool].blast_radius;
            if (r > 0.0f) radius = r; else break;       /* a bullet's "detonation" */
        }
        detonation(w, e->pos, e->dir, radius);
        break;
    }
    case HTA_EV_WRECK:
        wreck(w, e->pos);
        break;
    case HTA_EV_HIT_WORLD: {
        /* A round that struck a prop: `dir` is the surface normal, so
         * look back along it for the face that was hit. */
        if (!w->props.count) break;
        float o[3] = { e->pos[0] + e->dir[0] * 0.05f, e->pos[1] + e->dir[1] * 0.05f, e->pos[2] + e->dir[2] * 0.05f };
        float d[3] = { -e->dir[0], -e->dir[1], -e->dir[2] }, t, n[3];
        uint32_t p;
        if (hta_props_ray(&w->props, o, d, 0.12f, &p, &t, n)) {
            float from[3] = { e->pos[0] + e->dir[0], e->pos[1] + e->dir[1], e->pos[2] + e->dir[2] };
            hta_props_damage(&w->props, p, HTA_WFX_BULLET_DAMAGE, e->pos, from, &w->rigid, &w->fx);
        }
        break;
    }
    case HTA_EV_HIT_UNIT:
        /* A little blood from a hit that got through, at full gore. */
        if (w->gib_level >= 2 && g && e->a >= 0 && (uint32_t)e->a < g->unit_count &&
            g->units[e->a].vitals.shield <= 0.0f) {
            float d[3] = { -e->dir[0], -e->dir[1], 0.3f };
            hta_fx_burst(&w->fx, HTA_BURST_BLOOD, e->pos, d, 5);
        }
        break;
    default:
        break;
    }
}

void hta_wfx_net_fx(hta_world_fx *w, hta_wfx_net_kind kind, const float pos[3], const float dir[3], float amount)
{
    if (!w || !w->ready || !pos) return;
    switch (kind) {
    case HTA_WFX_NET_DETONATE: detonation(w, pos, dir, amount > 0.0f ? amount : 1.0f); break;
    case HTA_WFX_NET_WRECK:    wreck(w, pos); break;
    case HTA_WFX_NET_GIB: {
        hta_gib_desc d;
        memset(&d, 0, sizeof(d));
        memcpy(d.pos, pos, sizeof(d.pos));
        if (dir) memcpy(d.from, dir, sizeof(d.from));
        hta_gibs_should(amount, &d.strength);
        hta_gibs_spawn(&w->rigid, &w->fx, &d, w->gib_level);
        break;
    }
    }
}

void hta_wfx_update(hta_world_fx *w, float dt, const hta_camera *cam)
{
    if (!w || !w->ready || dt <= 0.0f) return;
    hta_rigid_step(&w->rigid, dt);
    hta_gibs_update(&w->rigid, &w->fx, dt, w->gib_level);
    /* Hard knocks: sparks off metal, dust off stone. */
    for (uint32_t i = 0; i < w->rigid.impact_count; i++) {
        const hta_rigid_impact *im = &w->rigid.impacts[i];
        if (im->speed < 2.0f) continue;
        if (im->material == HTA_RMAT_METAL) hta_fx_burst(&w->fx, HTA_BURST_SPARKS, im->point, im->normal, 3);
        else if (im->material == HTA_RMAT_CONCRETE) hta_fx_burst(&w->fx, HTA_BURST_DUST, im->point, im->normal, 1);
    }
    hta_fx_update(&w->fx, dt);
    hta_props_update(&w->props, dt);
    if (cam) hta_weather_update(&w->weather, dt, cam, &w->fx);
    /* Sprites are lit by the flash too. */
    w->fx.light = 1.0f + w->weather.flash;
}

void hta_wfx_frame_look(const hta_world_fx *w, const hta_gfx_settings *base,
                        hta_gfx_settings *out, float ambient[3], float light[3])
{
    if (!out || !base) return;
    *out = *base;
    if (!w || !w->ready) return;
    float c[3], wt, dm;
    hta_weather_atmosphere(&w->weather, c, &wt, &dm);
    if (wt > 0.0f) {
        /* Weather wants fog even on a preset that has it off. */
        out->fog = true;
        out->fog_from_scene = false;
        const float *fc = base->fog_color;
        for (int k = 0; k < 3; k++) out->fog_color[k] = fc[k] + (c[k] - fc[k]) * wt;
        float d = base->fog_density > 0.0f ? base->fog_density : 0.0009f;
        out->fog_density = d * dm;
        if (out->fog_density > 0.05f) out->fog_density = 0.05f;
        out->fog_start = base->fog_start / dm;
    }
    float f = w->weather.flash;
    if (f > 0.01f) {
        out->exposure = base->exposure * (1.0f + f * 0.8f);
        for (int k = 0; k < 3; k++) {
            if (ambient) ambient[k] += f * 0.6f;
            if (light) light[k] += f * 0.8f;
        }
    }
}

int hta_wfx_parse_weather(const char *text, size_t len)
{
    if (!text) return HTA_WFX_WEATHER_AUTO;
    const char *end = text + len;
    for (const char *p = text; p < end;) {
        const char *ls = p;
        while (p < end && *p != '\n') p++;
        const char *le = p;
        if (p < end) p++;
        while (ls < le && isspace((unsigned char)*ls)) ls++;
        if ((size_t)(le - ls) < 7 || strncmp(ls, "weather", 7) != 0) continue;
        const char *q = ls + 7;
        while (q < le && (isspace((unsigned char)*q) || *q == '=')) q++;
        char val[32];
        size_t n = 0;
        while (q < le && !isspace((unsigned char)*q) && n + 1 < sizeof(val)) val[n++] = *q++;
        val[n] = 0;
        hta_weather_kind k;
        if (hta_weather_from_name(val, &k)) return (int)k;
        return HTA_WFX_WEATHER_AUTO;
    }
    return HTA_WFX_WEATHER_AUTO;
}
