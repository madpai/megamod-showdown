#include "fx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- noise */

static uint32_t hash2(int x, int y, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float lattice(int x, int y, uint32_t seed) { return (float)(hash2(x, y, seed) & 0xFFFF) / 65535.0f; }
static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
/* Value noise, tiling with period `p` so a tile's edges meet. */
static float vnoise(float x, float y, int p, uint32_t seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = smooth(x - (float)xi), fy = smooth(y - (float)yi);
    int x0 = ((xi % p) + p) % p, y0 = ((yi % p) + p) % p, x1 = (x0 + 1) % p, y1 = (y0 + 1) % p;
    float a = lattice(x0, y0, seed), b = lattice(x1, y0, seed), c = lattice(x0, y1, seed), d = lattice(x1, y1, seed);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}
static float fbm(float x, float y, int p, uint32_t seed)
{
    float s = 0, amp = 0.5f, norm = 0;
    for (int o = 0; o < 4; o++) {
        s += vnoise(x, y, p, seed + (uint32_t)o) * amp;
        norm += amp; amp *= 0.5f; x *= 2; y *= 2; p *= 2;
    }
    return s / norm;
}
static uint8_t u8(float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return (uint8_t)(v * 255.0f + 0.5f); }

/* ---------------------------------------------------------------- atlas */

#define TILE (HTA_FX_ATLAS / 4u)

static void put(uint8_t *px, uint32_t tile, uint32_t x, uint32_t y, float r, float g, float b, float a)
{
    uint32_t ax = (tile % 4u) * TILE + x, ay = (tile / 4u) * TILE + y;
    uint8_t *p = px + ((size_t)ay * HTA_FX_ATLAS + ax) * 4u;
    p[0] = u8(r); p[1] = u8(g); p[2] = u8(b); p[3] = u8(a);
}

bool hta_fx_atlas(hta_bsp_texture *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    uint8_t *px = calloc((size_t)HTA_FX_ATLAS * HTA_FX_ATLAS, 4);
    if (!px) return false;
    for (uint32_t y = 0; y < TILE; y++) for (uint32_t x = 0; x < TILE; x++) {
        float u = (float)x / TILE, v = (float)y / TILE;
        float cu = u - 0.5f, cv = v - 0.5f, r = sqrtf(cu*cu + cv*cv) * 2.0f;   /* 0 centre, 1 edge */
        float n = fbm(u * 8, v * 8, 8, 11);

        /* wood: planks with grain along u */
        float grain = 0.5f + 0.5f * sinf((v * 38.0f + fbm(u * 2, v * 6, 2, 3) * 9.0f));
        float plank = (fmodf(v * 4.0f, 1.0f) < 0.04f) ? 0.55f : 1.0f;
        float wd = (0.55f + 0.25f * grain + 0.2f * n) * plank;
        put(px, HTA_TILE_WOOD, x, y, 0.55f * wd, 0.36f * wd, 0.20f * wd, 1);

        /* metal: brushed, a little blue, rivets at the corners */
        float br = 0.6f + 0.25f * vnoise(u * 64, v * 4, 64, 5) + 0.1f * n;
        float rv = 0;
        for (int k = 0; k < 4; k++) {
            float rx = (k & 1) ? 0.1f : 0.9f, ry = (k & 2) ? 0.1f : 0.9f;
            float d = sqrtf((u - rx) * (u - rx) + (v - ry) * (v - ry));
            if (d < 0.035f) rv = 0.25f * (1.0f - d / 0.035f);
        }
        put(px, HTA_TILE_METAL, x, y, 0.52f * br + rv, 0.55f * br + rv, 0.60f * br + rv, 1);

        /* concrete: speckled grey, some darker blotches */
        float sp = (hash2((int)x, (int)y, 7) & 0xFF) / 255.0f;
        float cc = 0.52f + 0.18f * n + 0.08f * (sp - 0.5f) - 0.12f * (fbm(u * 3, v * 3, 3, 9) > 0.62f);
        put(px, HTA_TILE_CONCRETE, x, y, cc, cc * 0.98f, cc * 0.94f, 1);

        /* glass: pale blue-green with bright streaks */
        float gs = 0.08f * vnoise((u + v) * 12, (u - v) * 2, 12, 13);
        put(px, HTA_TILE_GLASS, x, y, 0.62f + gs, 0.78f + gs, 0.80f + gs, 1);

        /* flesh: marbled red, dark veins */
        float m = fbm(u * 5, v * 5, 5, 17);
        float vein = fabsf(sinf(m * 18.0f)) < 0.18f ? 0.55f : 1.0f;
        put(px, HTA_TILE_FLESH, x, y, (0.55f + 0.3f * m) * vein, 0.08f + 0.12f * m, 0.08f + 0.1f * m, 1);

        /* dirt: brown clods */
        float dn = fbm(u * 6, v * 6, 6, 21);
        put(px, HTA_TILE_DIRT, x, y, 0.36f * (0.6f + dn), 0.27f * (0.6f + dn), 0.17f * (0.6f + dn), 1);

        /* soft round blob (straight alpha) */
        float blob = r < 1 ? powf(1.0f - r, 1.6f) : 0;
        put(px, HTA_TILE_BLOB, x, y, 1, 1, 1, blob);

        /* vertical streak: thin across, tapered along */
        float across = expf(-(cu * cu) / (2 * 0.06f * 0.06f));
        float along = 1.0f - fabsf(cv) * 2.0f;
        put(px, HTA_TILE_STREAK, x, y, 1, 1, 1, across * (along > 0 ? along : 0));

        /* six-armed flake */
        float ang = atan2f(cv, cu);
        float arms = powf(fabsf(cosf(ang * 3.0f)), 8.0f);
        float fl = r < 1 ? (arms * (1 - r) + (r < 0.25f ? 1 - r * 4 : 0)) : 0;
        put(px, HTA_TILE_FLAKE, x, y, 1, 1, 1, fl > 1 ? 1 : fl);

        /* spark: PREMULTIPLIED (drawn additive), a hot core */
        float sk = r < 1 ? powf(1.0f - r, 3.0f) : 0;
        put(px, HTA_TILE_SPARK, x, y, sk, sk * 0.9f, sk * 0.7f, sk);

        /* splash ring */
        float ring = expf(-((r - 0.7f) * (r - 0.7f)) / (2 * 0.08f * 0.08f)) * (r < 1);
        put(px, HTA_TILE_RING, x, y, 1, 1, 1, ring);

        /* smoke: a lumpy soft ball */
        float sm = r < 1 ? (1 - r) * (0.55f + 0.6f * fbm(u * 4, v * 4, 4, 31)) : 0;
        put(px, HTA_TILE_SMOKE, x, y, 1, 1, 1, sm > 1 ? 1 : sm);

        /* splat: irregular edge and satellite drops; its own dark red */
        float edge = 0.62f + 0.25f * fbm(cosf(ang) * 2 + 3, sinf(ang) * 2 + 3, 8, 41);
        float sat = 0;
        for (int k = 0; k < 7; k++) {
            float a2 = (float)k * 0.9f + 0.4f, rr = 0.8f + 0.1f * (float)(k % 3);
            float dx = cu * 2 - cosf(a2) * rr, dy = cv * 2 - sinf(a2) * rr;
            float d = sqrtf(dx * dx + dy * dy);
            float s2 = 0.07f + 0.03f * (float)(k % 2);
            if (d < s2) sat = 1;
        }
        float body = r < edge ? 1 : (r < edge + 0.05f ? 1 - (r - edge) / 0.05f : 0);
        float sa = body > sat ? body : sat;
        float shade = 0.75f + 0.25f * n;
        put(px, HTA_TILE_SPLAT, x, y, 0.42f * shade, 0.02f, 0.02f, sa * 0.92f);
    }
    out->width = out->height = HTA_FX_ATLAS;
    out->rgba = px;
    out->tint = 0xFFFFFF;
    out->tag_id = 0xFFFFFFFFu;
    return true;
}

void hta_fx_atlas_free(hta_bsp_texture *t)
{
    if (!t) return;
    free(t->rgba);
    t->rgba = NULL;
}

hta_fx_tile hta_fx_material_tile(hta_rigid_material m)
{
    switch (m) {
    case HTA_RMAT_WOOD:     return HTA_TILE_WOOD;
    case HTA_RMAT_METAL:    return HTA_TILE_METAL;
    case HTA_RMAT_CONCRETE: return HTA_TILE_CONCRETE;
    case HTA_RMAT_GLASS:    return HTA_TILE_GLASS;
    case HTA_RMAT_FLESH:    return HTA_TILE_FLESH;
    case HTA_RMAT_DIRT:     return HTA_TILE_DIRT;
    default:                return HTA_TILE_CONCRETE;
    }
}

/* A tile's uv rectangle, inset a few texels so mip levels do not borrow
 * from the neighbour. */
static void tile_uv(uint32_t tile, float frac, float uv0[2], float uv1[2])
{
    const float inset = 3.0f / (float)HTA_FX_ATLAS;
    float tx = (float)(tile % 4u) * 0.25f, ty = (float)(tile / 4u) * 0.25f;
    float span = (0.25f - 2 * inset) * (frac < 1.0f ? frac : 1.0f);
    uv0[0] = tx + inset; uv0[1] = ty + inset;
    uv1[0] = uv0[0] + span; uv1[1] = uv0[1] + span;
}

/* ------------------------------------------------------------- lifetime */

static bool mesh_slots(hta_bsp_mesh *m, uint32_t slots, uint32_t verts_per, const uint32_t *quad_ix,
                       uint32_t ix_per, uint32_t submesh_count, const uint32_t *sub_slots,
                       const uint8_t *modes)
{
    memset(m, 0, sizeof(*m));
    if (!slots) slots = 1;
    m->vertex_count = slots * verts_per;
    m->index_count = slots * ix_per;
    m->vertices = calloc(m->vertex_count, sizeof(hta_vertex));
    m->indices = malloc(sizeof(uint32_t) * m->index_count);
    m->submeshes = calloc(submesh_count, sizeof(hta_submesh));
    if (!m->vertices || !m->indices || !m->submeshes) return false;
    for (uint32_t s = 0; s < slots; s++)
        for (uint32_t k = 0; k < ix_per; k++)
            m->indices[s * ix_per + k] = s * verts_per + quad_ix[k];
    uint32_t first = 0;
    for (uint32_t i = 0; i < submesh_count; i++) {
        hta_submesh_init(&m->submeshes[i]);
        m->submeshes[i].first_index = first * ix_per;
        m->submeshes[i].index_count = sub_slots[i] * ix_per;
        m->submeshes[i].albedo_tex = 0;
        m->submeshes[i].draw_mode = modes[i];
        first += sub_slots[i];
    }
    m->submesh_count = submesh_count;
    m->bounds_min[0] = m->bounds_min[1] = m->bounds_min[2] = -1e4f;
    m->bounds_max[0] = m->bounds_max[1] = m->bounds_max[2] = 1e4f;
    return true;
}

bool hta_fx_init(hta_fx *fx, uint32_t debris_slots, uint32_t alpha_slots, uint32_t add_slots,
                 const hta_collision *world)
{
    if (!fx) return false;
    memset(fx, 0, sizeof(*fx));
    fx->world = world;
    fx->gravity = 3.215f;
    fx->density = 1.0f;
    fx->light = 1.0f;
    fx->rng = 0x1234567u;
    if (!hta_fx_atlas(&fx->atlas)) return false;

    /* Box: 6 faces x 4 verts, 36 indices. */
    uint32_t box_ix[36];
    for (uint32_t f = 0; f < 6; f++) {
        const uint32_t q[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k = 0; k < 6; k++) box_ix[f * 6 + (uint32_t)k] = f * 4 + q[k];
    }
    uint8_t opaque = HTA_DRAW_OPAQUE;
    fx->debris_slots = debris_slots ? debris_slots : 1;
    if (!mesh_slots(&fx->debris, fx->debris_slots, 24, box_ix, 36, 1, &fx->debris_slots, &opaque)) goto bad;
    fx->debris.textures = &fx->atlas;
    fx->debris.texture_count = 1;
    fx->debris.submeshes[0].scene_lit = true;
    fx->debris_verts = calloc(fx->debris.vertex_count, sizeof(hta_vertex));

    const uint32_t quad_ix[6] = { 0, 1, 2, 0, 2, 3 };
    fx->alpha_slots = alpha_slots ? alpha_slots : 1;
    fx->add_slots = add_slots ? add_slots : 1;
    uint32_t subs[2] = { fx->alpha_slots, fx->add_slots };
    uint8_t modes[2] = { HTA_DRAW_ALPHA, HTA_DRAW_ADD };
    if (!mesh_slots(&fx->sprites, fx->alpha_slots + fx->add_slots, 4, quad_ix, 6, 2, subs, modes)) goto bad;
    fx->sprites.textures = &fx->atlas;
    fx->sprites.texture_count = 1;
    fx->sprite_verts = calloc(fx->sprites.vertex_count, sizeof(hta_vertex));
    fx->pool = calloc(fx->alpha_slots + fx->add_slots, sizeof(hta_sprite));
    if (!fx->debris_verts || !fx->sprite_verts || !fx->pool) goto bad;
    return true;
bad:
    hta_fx_free(fx);
    return false;
}

static void free_mesh(hta_bsp_mesh *m)
{
    free(m->vertices); free(m->indices); free(m->submeshes);
    memset(m, 0, sizeof(*m));
}

void hta_fx_free(hta_fx *fx)
{
    if (!fx) return;
    free_mesh(&fx->debris);
    free_mesh(&fx->sprites);
    free(fx->debris_verts);
    free(fx->sprite_verts);
    free(fx->pool);
    hta_fx_atlas_free(&fx->atlas);
    memset(fx, 0, sizeof(*fx));
}

static float frand(hta_fx *fx)
{
    uint32_t x = fx->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    fx->rng = x;
    return (float)(x & 0xFFFFFF) / 16777215.0f;
}
static float srand1(hta_fx *fx) { return frand(fx) * 2.0f - 1.0f; }

bool hta_fx_emit(hta_fx *fx, const hta_sprite *s)
{
    if (!fx || !s || !fx->pool) return false;
    uint32_t lo = s->additive ? fx->alpha_slots : 0;
    uint32_t hi = s->additive ? fx->alpha_slots + fx->add_slots : fx->alpha_slots;
    uint32_t slot = UINT32_MAX;
    float oldest = -1.0f;
    for (uint32_t i = lo; i < hi; i++) {
        if (!fx->pool[i].active) { slot = i; break; }
        /* Replace whatever is furthest through its life. */
        float f = fx->pool[i].life > 0 ? fx->pool[i].age / fx->pool[i].life : 1.0f;
        if (f > oldest) { oldest = f; slot = i; }
    }
    if (slot == UINT32_MAX) return false;
    bool was_free = !fx->pool[slot].active;
    fx->pool[slot] = *s;
    fx->pool[slot].active = true;
    fx->pool[slot].age = 0.0f;
    return was_free;
}

void hta_fx_splat(hta_fx *fx, const float pos[3], const float normal[3], float size,
                  const float color[4], float life)
{
    hta_sprite s;
    memset(&s, 0, sizeof(s));
    memcpy(s.pos, pos, 12);
    memcpy(s.normal, normal, 12);
    s.size0 = size * 0.7f; s.size1 = size;     /* it spreads a little */
    memcpy(s.color, color, 16);
    s.life = life;
    s.angle = frand(fx) * 6.2831853f;
    s.tile = HTA_TILE_SPLAT;
    s.mode = HTA_SPRITE_FLAT;
    hta_fx_emit(fx, &s);
    fx->splats++;
}

static uint32_t scaled(hta_fx *fx, uint32_t n)
{
    if (!n) return 0;
    uint32_t m = (uint32_t)((float)n * fx->density + 0.5f);
    return m ? m : 1;
}

/* A random direction inside a cone of `spread` around unit `d`. */
static void cone(hta_fx *fx, const float d[3], float spread, float out[3])
{
    for (int tries = 0; tries < 8; tries++) {
        float r[3] = { srand1(fx), srand1(fx), srand1(fx) };
        float l = sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
        if (l < 1e-3f || l > 1.0f) continue;
        for (int k = 0; k < 3; k++) out[k] = d[k] + r[k] / l * spread;
        return;
    }
    memcpy(out, d, 12);
}

void hta_fx_burst(hta_fx *fx, hta_burst kind, const float pos[3], const float dir_in[3], uint32_t count)
{
    if (!fx) return;
    float d[3] = { 0, 0, 1 };
    if (dir_in) {
        float l = sqrtf(dir_in[0]*dir_in[0] + dir_in[1]*dir_in[1] + dir_in[2]*dir_in[2]);
        if (l > 1e-5f) for (int k = 0; k < 3; k++) d[k] = dir_in[k] / l;
    }
    uint32_t n = scaled(fx, count);
    for (uint32_t i = 0; i < n; i++) {
        hta_sprite s;
        memset(&s, 0, sizeof(s));
        memcpy(s.pos, pos, 12);
        float v[3];
        switch (kind) {
        case HTA_BURST_BLOOD:
        case HTA_BURST_GORE: {
            bool gore = kind == HTA_BURST_GORE;
            cone(fx, d, gore ? 1.1f : 0.6f, v);
            float sp = (gore ? 2.5f : 1.4f) * (0.4f + frand(fx));
            for (int k = 0; k < 3; k++) s.vel[k] = v[k] * sp;
            s.size0 = (gore ? 0.035f : 0.02f) * (0.6f + frand(fx));
            s.size1 = s.size0 * 0.6f;
            s.color[0] = 0.45f; s.color[1] = 0.02f; s.color[2] = 0.02f; s.color[3] = 0.95f;
            s.life = 0.6f + frand(fx) * 0.8f;
            s.gravity = 1.0f; s.drag = 0.6f;
            s.tile = HTA_TILE_STREAK; s.mode = HTA_SPRITE_STREAK;
            s.collide = true;
            hta_fx_emit(fx, &s);
            /* And a mist that hangs for a moment. */
            if ((i & 3u) == 0u) {
                hta_sprite m = s;
                for (int k = 0; k < 3; k++) m.vel[k] = v[k] * 0.3f;
                m.size0 = gore ? 0.12f : 0.06f; m.size1 = m.size0 * 2.5f;
                m.color[3] = 0.5f; m.life = 0.5f + frand(fx) * 0.4f;
                m.gravity = 0.05f; m.drag = 2.0f;
                m.tile = HTA_TILE_SMOKE; m.mode = HTA_SPRITE_BILLBOARD; m.collide = false;
                m.spin = srand1(fx) * 2.0f;
                hta_fx_emit(fx, &m);
            }
            break;
        }
        case HTA_BURST_DUST:
        case HTA_BURST_SPLINTERS:
            cone(fx, d, 1.2f, v);
            for (int k = 0; k < 3; k++) s.vel[k] = v[k] * (0.3f + frand(fx) * 0.6f);
            s.size0 = 0.08f + frand(fx) * 0.06f; s.size1 = s.size0 * 3.0f;
            if (kind == HTA_BURST_DUST) { s.color[0] = 0.62f; s.color[1] = 0.58f; s.color[2] = 0.52f; }
            else { s.color[0] = 0.55f; s.color[1] = 0.42f; s.color[2] = 0.28f; }
            s.color[3] = 0.55f;
            s.life = 1.2f + frand(fx) * 1.2f;
            s.gravity = -0.02f; s.drag = 1.5f; s.spin = srand1(fx);
            s.tile = HTA_TILE_SMOKE; s.mode = HTA_SPRITE_BILLBOARD;
            hta_fx_emit(fx, &s);
            break;
        case HTA_BURST_SPARKS:
            cone(fx, d, 0.8f, v);
            for (int k = 0; k < 3; k++) s.vel[k] = v[k] * (2.0f + frand(fx) * 3.0f);
            s.size0 = 0.012f; s.size1 = 0.006f;
            s.color[0] = 1.0f; s.color[1] = 0.8f; s.color[2] = 0.45f; s.color[3] = 1.0f;
            s.life = 0.25f + frand(fx) * 0.35f;
            s.gravity = 1.0f; s.drag = 0.5f;
            s.tile = HTA_TILE_SPARK; s.mode = HTA_SPRITE_STREAK; s.additive = true;
            hta_fx_emit(fx, &s);
            break;
        case HTA_BURST_GLASS:
            cone(fx, d, 1.0f, v);
            for (int k = 0; k < 3; k++) s.vel[k] = v[k] * (1.0f + frand(fx) * 2.0f);
            s.size0 = s.size1 = 0.01f + frand(fx) * 0.012f;
            s.color[0] = 0.85f; s.color[1] = 0.95f; s.color[2] = 1.0f; s.color[3] = 1.0f;
            s.life = 0.6f + frand(fx) * 0.6f;
            s.gravity = 1.0f; s.drag = 0.2f; s.spin = srand1(fx) * 12.0f;
            s.tile = HTA_TILE_SPARK; s.mode = HTA_SPRITE_BILLBOARD; s.additive = true;
            hta_fx_emit(fx, &s);
            break;
        case HTA_BURST_SPLASH:
            s.size0 = 0.015f; s.size1 = 0.06f;
            s.color[0] = s.color[1] = s.color[2] = 0.85f; s.color[3] = 0.5f;
            s.life = 0.25f;
            s.normal[2] = 1.0f; s.pos[2] += 0.01f;
            s.tile = HTA_TILE_RING; s.mode = HTA_SPRITE_FLAT;
            hta_fx_emit(fx, &s);
            break;
        default:
            break;
        }
    }
}

void hta_fx_update(hta_fx *fx, float dt)
{
    if (!fx || !fx->pool || dt <= 0.0f) return;
    uint32_t n = fx->alpha_slots + fx->add_slots;
    for (uint32_t i = 0; i < n; i++) {
        hta_sprite *s = &fx->pool[i];
        if (!s->active) continue;
        s->age += dt;
        if (s->age >= s->life) { s->active = false; continue; }
        if (s->mode == HTA_SPRITE_FLAT) continue;
        float drag = expf(-s->drag * dt);
        s->vel[2] -= s->gravity * fx->gravity * dt;
        for (int k = 0; k < 3; k++) s->vel[k] *= drag;
        s->angle += s->spin * dt;
        float mv[3] = { s->vel[0] * dt, s->vel[1] * dt, s->vel[2] * dt };
        if (s->collide && fx->world) {
            float t, hit[3], nrm[3];
            if (hta_collision_ray(fx->world, s->pos, mv, 1.0f, &t, hit, nrm) && t <= 1.0f) {
                /* A blood drop leaves its mark where it lands. */
                float sz = 0.05f + s->size0 * 2.5f;
                float col[4] = { s->color[0], s->color[1], s->color[2], 0.9f };
                s->active = false;
                hta_fx_splat(fx, hit, nrm, sz, col, 25.0f);
                continue;
            }
        }
        for (int k = 0; k < 3; k++) s->pos[k] += mv[k];
    }
}

uint32_t hta_fx_live(const hta_fx *fx)
{
    uint32_t n = 0;
    if (!fx || !fx->pool) return 0;
    for (uint32_t i = 0; i < fx->alpha_slots + fx->add_slots; i++) n += fx->pool[i].active;
    return n;
}

/* --------------------------------------------------------------- vertices */

static void vset(hta_vertex *v, const float p[3], const float n[3], float u, float t, float lu)
{
    memcpy(v->pos, p, 12);
    memcpy(v->normal, n, 12);
    v->uv[0] = u; v->uv[1] = t;
    v->lm_uv[0] = lu; v->lm_uv[1] = 0.5f;
}

void hta_fx_build_debris(hta_fx *fx, const hta_rigid_world *w)
{
    if (!fx || !fx->debris_verts) return;
    /* Face corners in the box's own space, and each face's normal. */
    static const float fn[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    static const float fc[6][4][3] = {
        { {1,-1,-1}, {1,1,-1}, {1,1,1}, {1,-1,1} },
        { {-1,1,-1}, {-1,-1,-1}, {-1,-1,1}, {-1,1,1} },
        { {1,1,-1}, {-1,1,-1}, {-1,1,1}, {1,1,1} },
        { {-1,-1,-1}, {1,-1,-1}, {1,-1,1}, {-1,-1,1} },
        { {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} },
        { {-1,1,-1}, {1,1,-1}, {1,-1,-1}, {-1,-1,-1} },
    };
    for (uint32_t s = 0; s < fx->debris_slots; s++) {
        hta_vertex *v = fx->debris_verts + s * 24u;
        const hta_rigid_body *b = (w && s < w->cap) ? &w->bodies[s] : NULL;
        if (!b || !b->active) { memset(v, 0, 24 * sizeof(hta_vertex)); continue; }
        float a = hta_rigid_alpha(b);
        float m[16];
        hta_rigid_matrix(b, m);
        float sc = a;       /* fading bodies shrink away */
        float h[3] = { b->half[0] * sc, b->half[1] * sc, b->half[2] * sc };
        if (b->shape == HTA_RIGID_SPHERE) h[1] = h[2] = h[0];
        uint32_t tile = hta_fx_material_tile((hta_rigid_material)b->material);
        /* Show a patch of the texture as big as the face, so small chunks
         * are not a whole plank squeezed down. 0.5 wu is a full tile. */
        for (int f = 0; f < 6; f++) {
            int ax0 = f < 2 ? 1 : 0, ax1 = f < 4 ? 2 : 1;
            float frac = fmaxf(h[ax0], h[ax1]) * 4.0f;
            float uv0[2], uv1[2];
            tile_uv(tile, frac, uv0, uv1);
            /* A per-body offset into the tile so chunks differ. */
            float off = (float)((b->generation * 7u + s * 13u) % 5u) * 0.01f;
            float nw[3] = { m[0]*fn[f][0] + m[4]*fn[f][1] + m[8]*fn[f][2],
                            m[1]*fn[f][0] + m[5]*fn[f][1] + m[9]*fn[f][2],
                            m[2]*fn[f][0] + m[6]*fn[f][1] + m[10]*fn[f][2] };
            const float uvq[4][2] = { {uv0[0], uv0[1]}, {uv1[0], uv0[1]}, {uv1[0], uv1[1]}, {uv0[0], uv1[1]} };
            for (int k = 0; k < 4; k++) {
                float l[3] = { fc[f][k][0] * h[0], fc[f][k][1] * h[1], fc[f][k][2] * h[2] };
                float p[3] = { m[0]*l[0] + m[4]*l[1] + m[8]*l[2] + m[12],
                               m[1]*l[0] + m[5]*l[1] + m[9]*l[2] + m[13],
                               m[2]*l[0] + m[6]*l[1] + m[10]*l[2] + m[14] };
                vset(&v[f * 4 + k], p, nw, uvq[k][0] + off * 0.25f, uvq[k][1], 0.5f);
            }
        }
    }
}

void hta_fx_build_sprites(hta_fx *fx, const hta_camera *cam)
{
    if (!fx || !fx->sprite_verts || !cam) return;
    float right[3], up[3], fwd[3];
    hta_camera_right(cam, right);
    hta_camera_up(cam, up);
    hta_camera_forward(cam, fwd);
    uint32_t n = fx->alpha_slots + fx->add_slots;
    for (uint32_t i = 0; i < n; i++) {
        hta_vertex *v = fx->sprite_verts + i * 4u;
        const hta_sprite *s = &fx->pool[i];
        if (!s->active) { memset(v, 0, 4 * sizeof(hta_vertex)); continue; }
        float t = s->life > 0 ? s->age / s->life : 1.0f;
        float size = s->size0 + (s->size1 - s->size0) * t;
        /* Fade in fast, out over the last third. */
        float alpha = s->color[3] * (t < 0.05f ? t / 0.05f : 1.0f) * (t > 0.66f ? (1.0f - t) / 0.34f : 1.0f);
        float light = s->additive ? 1.0f : fx->light;
        float col[3] = { s->color[0] * light, s->color[1] * light, s->color[2] * light };
        float ax[3], ay[3];
        if (s->mode == HTA_SPRITE_STREAK) {
            float sp = sqrtf(s->vel[0]*s->vel[0] + s->vel[1]*s->vel[1] + s->vel[2]*s->vel[2]);
            float dir[3] = { 0, 0, 1 };
            if (sp > 1e-4f) for (int k = 0; k < 3; k++) dir[k] = s->vel[k] / sp;
            float to[3] = { s->pos[0]-cam->pos[0], s->pos[1]-cam->pos[1], s->pos[2]-cam->pos[2] };
            float side[3] = { dir[1]*to[2] - dir[2]*to[1], dir[2]*to[0] - dir[0]*to[2], dir[0]*to[1] - dir[1]*to[0] };
            float sl = sqrtf(side[0]*side[0] + side[1]*side[1] + side[2]*side[2]);
            if (sl < 1e-5f) { memcpy(side, right, 12); sl = 1; }
            float len = size + sp * 0.04f;
            for (int k = 0; k < 3; k++) { ax[k] = side[k] / sl * size; ay[k] = dir[k] * len; }
        } else if (s->mode == HTA_SPRITE_FLAT) {
            const float *nn = s->normal;
            float ref[3] = { 0, 0, 1 };
            if (fabsf(nn[2]) > 0.9f) { ref[0] = 1; ref[2] = 0; }
            float t1[3] = { nn[1]*ref[2] - nn[2]*ref[1], nn[2]*ref[0] - nn[0]*ref[2], nn[0]*ref[1] - nn[1]*ref[0] };
            float l1 = sqrtf(t1[0]*t1[0] + t1[1]*t1[1] + t1[2]*t1[2]);
            for (int k = 0; k < 3; k++) t1[k] /= (l1 > 1e-6f ? l1 : 1);
            float t2[3] = { nn[1]*t1[2] - nn[2]*t1[1], nn[2]*t1[0] - nn[0]*t1[2], nn[0]*t1[1] - nn[1]*t1[0] };
            float c = cosf(s->angle), sn = sinf(s->angle);
            for (int k = 0; k < 3; k++) {
                ax[k] = (t1[k] * c + t2[k] * sn) * size;
                ay[k] = (t2[k] * c - t1[k] * sn) * size;
            }
        } else {
            float c = cosf(s->angle), sn = sinf(s->angle);
            for (int k = 0; k < 3; k++) {
                ax[k] = (right[k] * c + up[k] * sn) * size;
                ay[k] = (up[k] * c - right[k] * sn) * size;
            }
        }
        float lift[3] = { 0, 0, 0 };
        if (s->mode == HTA_SPRITE_FLAT) for (int k = 0; k < 3; k++) lift[k] = s->normal[k] * 0.004f;
        float uv0[2], uv1[2];
        tile_uv(s->tile, 1.0f, uv0, uv1);
        const float corner[4][2] = { {-1,-1}, {1,-1}, {1,1}, {-1,1} };
        const float uvs[4][2] = { {uv0[0], uv1[1]}, {uv1[0], uv1[1]}, {uv1[0], uv0[1]}, {uv0[0], uv0[1]} };
        for (int k = 0; k < 4; k++) {
            float p[3];
            for (int q = 0; q < 3; q++)
                p[q] = s->pos[q] + lift[q] + ax[q] * corner[k][0] + ay[q] * corner[k][1];
            vset(&v[k], p, col, uvs[k][0], uvs[k][1], alpha);
        }
    }
    (void)fwd;
}
