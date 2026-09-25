#include "weather.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Every number here is OURS (see the ledger). Rain falls at ~9 m/s = 3 wu/s
 * terminal; snow at ~1 m/s. */

static const char *const kNames[HTA_WEATHER_COUNT] = { "clear", "rain", "storm", "snow", "ash", "sandstorm" };

const char *hta_weather_name(hta_weather_kind k)
{
    return (unsigned)k < HTA_WEATHER_COUNT ? kNames[k] : "clear";
}

bool hta_weather_from_name(const char *name, hta_weather_kind *out)
{
    if (!name) return false;
    for (int i = 0; i < HTA_WEATHER_COUNT; i++) {
        const char *a = name, *b = kNames[i];
        while (*a && *b && tolower((unsigned char)*a) == *b) { a++; b++; }
        if (!*a && !*b) { if (out) *out = (hta_weather_kind)i; return true; }
    }
    return false;
}

typedef struct {
    float fall;          /* wu/s down */
    float size;          /* half width */
    float length;        /* streak length factor (0: a flake) */
    float alpha;
    float color[3];
    uint8_t tile;
    float drops;         /* at intensity 1 and full density, of max */
    float wind_mul;
    float flutter;       /* sideways wobble, wu */
} kind_look;

static const kind_look kLook[HTA_WEATHER_COUNT] = {
    [HTA_WEATHER_CLEAR]     = { 0,    0,      0,    0,    {0,0,0},             HTA_TILE_STREAK, 0,    0,    0 },
    [HTA_WEATHER_RAIN]      = { 3.0f, 0.004f, 1.0f, 0.35f, {0.75f,0.8f,0.85f}, HTA_TILE_STREAK, 0.7f, 0.3f, 0 },
    [HTA_WEATHER_STORM]     = { 3.6f, 0.006f, 1.2f, 0.55f, {0.7f,0.75f,0.8f},  HTA_TILE_STREAK, 1.0f, 0.6f, 0 },
    [HTA_WEATHER_SNOW]      = { 0.35f,0.012f, 0.0f, 0.85f, {1,1,1},            HTA_TILE_FLAKE,  0.6f, 0.8f, 0.15f },
    [HTA_WEATHER_ASH]       = { 0.18f,0.010f, 0.0f, 0.7f,  {0.45f,0.43f,0.42f},HTA_TILE_FLAKE,  0.4f, 0.6f, 0.2f },
    [HTA_WEATHER_SANDSTORM] = { 0.1f, 0.008f, 0.6f, 0.5f,  {0.75f,0.62f,0.42f},HTA_TILE_STREAK, 0.8f, 1.0f, 0.05f },
};

static float frand(hta_weather *w)
{
    uint32_t x = w->rng ? w->rng : 0xABCDEFu;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    w->rng = x;
    return (float)(x & 0xFFFFFF) / 16777215.0f;
}

bool hta_weather_init(hta_weather *w, uint32_t max_drops, const hta_bsp_texture *atlas,
                      const hta_collision *world)
{
    if (!w) return false;
    memset(w, 0, sizeof(*w));
    w->max_drops = max_drops ? max_drops : 1;
    w->world = world;
    w->density = 1.0f;
    w->rng = 0x7F4A7C15u;
    w->box[0] = w->box[1] = HTA_ROOF_CELL * HTA_ROOF_N * 0.5f;   /* matches the roof map */
    w->box[2] = 7.0f;
    w->thunder_in = -1.0f;
    w->drops = calloc(w->max_drops, sizeof(hta_drop));
    w->verts = calloc((size_t)w->max_drops * 4u, sizeof(hta_vertex));
    hta_bsp_mesh *m = &w->mesh;
    m->vertex_count = w->max_drops * 4u;
    m->index_count = w->max_drops * 6u;
    m->vertices = calloc(m->vertex_count, sizeof(hta_vertex));
    m->indices = malloc(m->index_count * sizeof(uint32_t));
    m->submeshes = calloc(1, sizeof(hta_submesh));
    if (!w->drops || !w->verts || !m->vertices || !m->indices || !m->submeshes) { hta_weather_free(w); return false; }
    for (uint32_t i = 0; i < w->max_drops; i++) {
        const uint32_t q[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k = 0; k < 6; k++) m->indices[i * 6u + (uint32_t)k] = i * 4u + q[k];
    }
    hta_submesh_init(&m->submeshes[0]);
    m->submeshes[0].index_count = m->index_count;
    m->submeshes[0].albedo_tex = 0;
    m->submeshes[0].draw_mode = HTA_DRAW_ALPHA;
    m->submesh_count = 1;
    m->textures = (hta_bsp_texture *)atlas;   /* borrowed */
    m->texture_count = atlas ? 1u : 0u;
    for (int k = 0; k < 3; k++) { m->bounds_min[k] = -1e4f; m->bounds_max[k] = 1e4f; }
    return true;
}

void hta_weather_free(hta_weather *w)
{
    if (!w) return;
    free(w->drops); free(w->verts);
    free(w->mesh.vertices); free(w->mesh.indices); free(w->mesh.submeshes);
    memset(w, 0, sizeof(*w));
}

void hta_weather_set(hta_weather *w, hta_weather_kind kind, float intensity, const float wind[2])
{
    if (!w) return;
    if ((unsigned)kind >= HTA_WEATHER_COUNT) kind = HTA_WEATHER_CLEAR;
    bool changed = w->kind != kind;
    w->kind = kind;
    w->intensity = intensity < 0 ? 0 : intensity > 1 ? 1 : intensity;
    if (wind) { w->wind[0] = wind[0]; w->wind[1] = wind[1]; }
    if (changed) { w->live = 0; w->flash = 0; w->next_strike = 4.0f; w->thunder_in = -1.0f; }
}

/* ---------------------------------------------------------------- roofs */

static int cell_of(float v) { return (int)floorf(v / HTA_ROOF_CELL); }
static uint32_t wrap(int v) { int n = (int)HTA_ROOF_N; return (uint32_t)(((v % n) + n) % n); }

bool hta_weather_open(const hta_weather *w, float x, float y, float z)
{
    if (!w) return true;
    int cx = cell_of(x), cy = cell_of(y);
    uint32_t i = wrap(cx), j = wrap(cy);
    if (!w->roof_known[i][j] || w->roof_key[i][j][0] != cx || w->roof_key[i][j][1] != cy) return true;
    return z > w->roof[i][j];
}

/* Probe up to `budget` unknown columns nearest the camera. */
static void roof_probe(hta_weather *w, const hta_camera *cam, uint32_t budget)
{
    if (!w->world) return;
    int c0 = cell_of(cam->pos[0]), c1 = cell_of(cam->pos[1]);
    int half = (int)HTA_ROOF_N / 2;
    for (int ring = 0; ring < half && budget; ring++) {
        for (int dy = -ring; dy <= ring && budget; dy++) for (int dx = -ring; dx <= ring && budget; dx++) {
            if (abs(dx) != ring && abs(dy) != ring) continue;
            int cx = c0 + dx, cy = c1 + dy;
            uint32_t i = wrap(cx), j = wrap(cy);
            if (w->roof_known[i][j] && w->roof_key[i][j][0] == cx && w->roof_key[i][j][1] == cy) continue;
            /* Straight up from just above the camera's feet level. */
            float o[3] = { ((float)cx + 0.5f) * HTA_ROOF_CELL, ((float)cy + 0.5f) * HTA_ROOF_CELL,
                           cam->pos[2] - 0.5f };
            float up[3] = { 0, 0, 1 }, t;
            float roof = -1e9f;
            if (hta_collision_ray(w->world, o, up, 400.0f, &t, NULL, NULL)) roof = o[2] + t;
            w->roof[i][j] = roof;
            w->roof_key[i][j][0] = cx; w->roof_key[i][j][1] = cy;
            w->roof_known[i][j] = true;
            w->roof_probes++;
            budget--;
        }
    }
}

/* ---------------------------------------------------------------- update */

static void respawn_drop(hta_weather *w, hta_drop *d, const hta_camera *cam, bool anywhere)
{
    d->pos[0] = cam->pos[0] + (frand(w) * 2 - 1) * w->box[0];
    d->pos[1] = cam->pos[1] + (frand(w) * 2 - 1) * w->box[1];
    d->pos[2] = anywhere ? cam->pos[2] + (frand(w) * 2 - 1) * w->box[2] : cam->pos[2] + w->box[2];
    d->phase = frand(w) * 6.2831853f;
}

void hta_weather_update(hta_weather *w, float dt, const hta_camera *cam, hta_fx *fx)
{
    if (!w || !cam || dt <= 0.0f) return;
    w->time += dt;
    const kind_look *lk = &kLook[w->kind];
    uint32_t want = (uint32_t)((float)w->max_drops * lk->drops * w->intensity * w->density);
    if (want > w->max_drops) want = w->max_drops;
    /* New drops appear anywhere in the box the first time, from the top after. */
    while (w->live < want) respawn_drop(w, &w->drops[w->live++], cam, true);
    if (w->live > want) w->live = want;

    roof_probe(w, cam, 12);

    float wx = w->wind[0] * lk->wind_mul, wy = w->wind[1] * lk->wind_mul;
    for (uint32_t i = 0; i < w->live; i++) {
        hta_drop *d = &w->drops[i];
        d->pos[0] += wx * dt + cosf(w->time * 1.3f + d->phase) * lk->flutter * dt;
        d->pos[1] += wy * dt + sinf(w->time * 1.1f + d->phase) * lk->flutter * dt;
        d->pos[2] -= lk->fall * dt;
        /* Wrap around the camera horizontally: the box travels with it. */
        for (int k = 0; k < 2; k++) {
            float rel = d->pos[k] - cam->pos[k];
            if (rel > w->box[k]) d->pos[k] -= 2 * w->box[k];
            else if (rel < -w->box[k]) d->pos[k] += 2 * w->box[k];
        }
        if (d->pos[2] < cam->pos[2] - w->box[2] || !hta_weather_open(w, d->pos[0], d->pos[1], d->pos[2] + 0.2f)) {
            respawn_drop(w, d, cam, false);
        }
    }

    /* Splashes where rain meets open ground near the camera. */
    if (fx && w->world && (w->kind == HTA_WEATHER_RAIN || w->kind == HTA_WEATHER_STORM)) {
        w->splash_acc += dt * 60.0f * w->intensity * w->density;
        while (w->splash_acc >= 1.0f) {
            w->splash_acc -= 1.0f;
            float x = cam->pos[0] + (frand(w) * 2 - 1) * 8.0f;
            float y = cam->pos[1] + (frand(w) * 2 - 1) * 8.0f;
            float z;
            if (hta_collision_ground(w->world, x, y, cam->pos[2] + 3.0f, &z) && hta_weather_open(w, x, y, z + 0.1f)) {
                float p[3] = { x, y, z };
                hta_fx_burst(fx, HTA_BURST_SPLASH, p, NULL, 1);
            }
        }
    }

    /* Lightning. */
    w->flash *= expf(-dt * 5.0f);
    if (w->kind == HTA_WEATHER_STORM && w->intensity > 0.2f) {
        w->next_strike -= dt;
        if (w->next_strike <= 0.0f) {
            float dist = 20.0f + frand(w) * 300.0f;              /* wu */
            w->flash = fminf(1.0f, 0.5f + 0.5f * frand(w)) * (dist < 100.0f ? 1.0f : 0.6f);
            /* Sound: 343 m/s = 112.5 wu/s. */
            w->thunder_in = dist / 112.5f;
            w->thunder_gain = dist < 60.0f ? 1.0f : fmaxf(0.25f, 60.0f / dist);
            w->next_strike = (6.0f + frand(w) * 14.0f) / (0.5f + w->intensity);
        }
    }
    if (w->thunder_in >= 0.0f) {
        w->thunder_in -= dt;
        if (w->thunder_in < 0.0f) { w->thunder_ready = true; w->thunder_in = -1.0f; }
    }
}

bool hta_weather_thunder(hta_weather *w, float *gain)
{
    if (!w || !w->thunder_ready) return false;
    w->thunder_ready = false;
    if (gain) *gain = w->thunder_gain;
    return true;
}

void hta_weather_build(hta_weather *w, const hta_camera *cam)
{
    if (!w || !cam || !w->verts) return;
    const kind_look *lk = &kLook[w->kind];
    float right[3], up[3];
    hta_camera_right(cam, right);
    hta_camera_up(cam, up);
    float wx = w->wind[0] * lk->wind_mul, wy = w->wind[1] * lk->wind_mul;
    float vel[3] = { wx, wy, -lk->fall };
    float vl = sqrtf(vel[0]*vel[0] + vel[1]*vel[1] + vel[2]*vel[2]);
    float dir[3] = { 0, 0, -1 };
    if (vl > 1e-4f) for (int k = 0; k < 3; k++) dir[k] = vel[k] / vl;
    float light = 1.0f + w->flash * 1.5f;
    float col[3] = { lk->color[0] * light, lk->color[1] * light, lk->color[2] * light };
    float tx = (float)(lk->tile % 4u) * 0.25f + 3.0f / HTA_FX_ATLAS, ty = (float)(lk->tile / 4u) * 0.25f + 3.0f / HTA_FX_ATLAS;
    float span = 0.25f - 6.0f / HTA_FX_ATLAS;
    for (uint32_t i = 0; i < w->max_drops; i++) {
        hta_vertex *v = w->verts + i * 4u;
        if (i >= w->live) { memset(v, 0, 4 * sizeof(hta_vertex)); continue; }
        const hta_drop *d = &w->drops[i];
        float ax[3], ay[3];
        if (lk->length > 0.0f) {
            float to[3] = { d->pos[0]-cam->pos[0], d->pos[1]-cam->pos[1], d->pos[2]-cam->pos[2] };
            float side[3] = { dir[1]*to[2] - dir[2]*to[1], dir[2]*to[0] - dir[0]*to[2], dir[0]*to[1] - dir[1]*to[0] };
            float sl = sqrtf(side[0]*side[0] + side[1]*side[1] + side[2]*side[2]);
            if (sl < 1e-5f) { memcpy(side, right, 12); sl = 1; }
            float len = lk->length * (0.06f + vl * 0.03f);
            for (int k = 0; k < 3; k++) { ax[k] = side[k] / sl * lk->size; ay[k] = dir[k] * len; }
        } else {
            float a = d->phase + w->time * 1.5f, c = cosf(a), s = sinf(a);
            for (int k = 0; k < 3; k++) {
                ax[k] = (right[k] * c + up[k] * s) * lk->size;
                ay[k] = (up[k] * c - right[k] * s) * lk->size;
            }
        }
        /* Fade drops near the box's edges so the wrap never pops. */
        float ex = 1.0f - fabsf(d->pos[0] - cam->pos[0]) / w->box[0];
        float ey = 1.0f - fabsf(d->pos[1] - cam->pos[1]) / w->box[1];
        float edge = fminf(ex, ey) * 4.0f;
        float alpha = lk->alpha * (edge < 1.0f ? (edge > 0 ? edge : 0) : 1.0f);
        const float corner[4][2] = { {-1,-1}, {1,-1}, {1,1}, {-1,1} };
        const float uvs[4][2] = { {0,1}, {1,1}, {1,0}, {0,0} };
        for (int k = 0; k < 4; k++) {
            for (int q = 0; q < 3; q++) v[k].pos[q] = d->pos[q] + ax[q] * corner[k][0] + ay[q] * corner[k][1];
            memcpy(v[k].normal, col, 12);
            v[k].uv[0] = tx + uvs[k][0] * span;
            v[k].uv[1] = ty + uvs[k][1] * span;
            v[k].lm_uv[0] = alpha;
            v[k].lm_uv[1] = 0.5f;
        }
    }
}

void hta_weather_atmosphere(const hta_weather *w, float color[3], float *weight, float *density_mul)
{
    float c[3] = { 0.6f, 0.62f, 0.66f }, wt = 0.0f, dm = 1.0f;
    if (w) {
        float I = w->intensity;
        switch (w->kind) {
        case HTA_WEATHER_RAIN:      wt = 0.4f * I; dm = 1.0f + 1.5f * I; break;
        case HTA_WEATHER_STORM:     c[0] = 0.4f; c[1] = 0.43f; c[2] = 0.48f; wt = 0.6f * I; dm = 1.0f + 2.5f * I; break;
        case HTA_WEATHER_SNOW:      c[0] = 0.85f; c[1] = 0.87f; c[2] = 0.9f; wt = 0.6f * I; dm = 1.0f + 3.0f * I; break;
        case HTA_WEATHER_ASH:       c[0] = 0.35f; c[1] = 0.32f; c[2] = 0.3f; wt = 0.6f * I; dm = 1.0f + 2.0f * I; break;
        case HTA_WEATHER_SANDSTORM: c[0] = 0.66f; c[1] = 0.52f; c[2] = 0.34f; wt = 0.9f * I; dm = 1.0f + 12.0f * I; break;
        default: break;
        }
        /* Lightning lights the air. */
        for (int k = 0; k < 3; k++) c[k] = c[k] + (1.0f - c[k]) * w->flash * 0.6f;
    }
    if (color) memcpy(color, c, sizeof(c));
    if (weight) *weight = wt;
    if (density_mul) *density_mul = dm;
}
