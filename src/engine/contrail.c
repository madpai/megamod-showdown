#include "contrail.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Contrail (324 bytes, reconciles with Invader's contrail.json). */
#define CONT_RATE          4u
#define CONT_REPEATS_U    28u
#define CONT_REPEATS_V    32u
#define CONT_BITMAP       48u     /* TagDependency */
#define CONT_FIRST_SEQ    64u
#define CONT_BLEND       174u     /* FramebufferBlendFunction: 3 is add */
#define CONT_STATES      312u     /* TagReflexive */
/* ContrailPointState (104). Bounds are two floats; we take the middle. */
#define STATE_SIZE       104u
#define STATE_DURATION     0u
#define STATE_TRANSITION   8u
#define STATE_WIDTH       64u
#define STATE_COLOR_LOW   68u     /* ColorARGB: a r g b */
#define STATE_COLOR_HIGH  84u
/* Object attachments: 72 bytes each, the attached tag's id at +12. */
#define OBJ_ATTACHMENTS  320u
#define ATTACH_SIZE       72u

/* The assault rifle's tracer point lives 0.01 s, less than a frame: taken
 * literally, no two points ever coexist and there is nothing to draw
 * between them. Halo draws it as a streak, so a point lives at least this
 * long, the tag's own states stretched to fit. Ours. */
#define HTA_CONT_MIN_LIFE 0.06f

/* ...but a tracer head runs at 300 wu/s, so 0.06 s of points is an 18 wu
 * (55 m) line, and a rifle's fifteen rounds a second joined into solid
 * yellow beams (owner playtest, 2026-09-23). Halo's own streak is its
 * round's travel in a frame or two: a tracer's ribbon is cut this far
 * behind its head. Ours. */
#define HTA_TRACER_TAIL 1.2f

static float rdf(const hta_cache *c, uint32_t off) { float f = 0; hta_rd_f32(c, off, &f); return isfinite(f) ? f : 0.0f; }

void hta_contrails_init(hta_contrails *c)
{
    if (c) memset(c, 0, sizeof(*c));
}

void hta_contrails_free(hta_contrails *c)
{
    if (!c) return;
    hta_bsp_free(&c->mesh);
    memset(c, 0, sizeof(*c));
}

uint32_t hta_contrails_add(hta_contrails *c, const hta_cache *cache,
                           const hta_resource_map *bitmaps, uint32_t tag)
{
    if (!c || !cache || !tag) return HTA_CONT_NONE;
    for (uint32_t i = 0; i < c->type_count; i++)
        if (c->type[i].tag_id == tag) return i;
    if (c->loaded) return HTA_CONT_NONE;   /* the mesh is sized: no new types */
    if (c->type_count >= HTA_CONT_TYPES) return HTA_CONT_NONE;
    int32_t ti = hta_cache_find_tag_by_id(cache, tag);
    hta_tag_entry t;
    uint32_t off;
    if (ti < 0 || !hta_cache_tag(cache, (uint32_t)ti, &t) ||
        t.primary_class != HTA_FOURCC('c','o','n','t') ||
        !hta_cache_ptr_to_offset(cache, t.tag_data_ptr, &off)) return HTA_CONT_NONE;
    hta_contrail_type *ty = &c->type[c->type_count];
    memset(ty, 0, sizeof(*ty));
    ty->tag_id = tag;
    ty->rate = rdf(cache, off + CONT_RATE);
    if (!(ty->rate > 0.0f)) ty->rate = 30.0f;
    ty->repeats_u = rdf(cache, off + CONT_REPEATS_U);
    ty->repeats_v = rdf(cache, off + CONT_REPEATS_V);
    if (!(ty->repeats_u > 0.0f)) ty->repeats_u = 1.0f;
    if (!(ty->repeats_v > 0.0f)) ty->repeats_v = 1.0f;
    uint16_t blend = 0;
    hta_rd_u16(cache, off + CONT_BLEND, &blend);
    ty->additive = blend == 3;
    uint32_t n = 0, ptr = 0, arr = 0;
    if (!hta_read_reflexive(cache, off + CONT_STATES, &n, &ptr) || !n ||
        !hta_cache_ptr_to_offset(cache, ptr, &arr)) return HTA_CONT_NONE;
    if (n > HTA_CONT_STATES) n = HTA_CONT_STATES;
    for (uint32_t s = 0; s < n; s++) {
        uint32_t q = arr + s * STATE_SIZE;
        hta_contrail_state *st = &ty->state[s];
        st->duration = 0.5f * (rdf(cache, q + STATE_DURATION) + rdf(cache, q + STATE_DURATION + 4));
        st->transition = 0.5f * (rdf(cache, q + STATE_TRANSITION) + rdf(cache, q + STATE_TRANSITION + 4));
        st->width = rdf(cache, q + STATE_WIDTH);
        float lo[4], hi[4];
        for (int k = 0; k < 4; k++) {
            lo[k] = rdf(cache, q + STATE_COLOR_LOW + (uint32_t)k * 4u);
            hi[k] = rdf(cache, q + STATE_COLOR_HIGH + (uint32_t)k * 4u);
        }
        /* ARGB in the tag; the middle of the two bounds. */
        st->color[3] = 0.5f * (lo[0] + hi[0]);
        for (int k = 0; k < 3; k++) st->color[k] = 0.5f * (lo[k + 1] + hi[k + 1]);
    }
    ty->state_count = n;
    float life = 0.0f;
    for (uint32_t s = 0; s < n; s++) {
        life += ty->state[s].duration;
        if (s + 1 < n) life += ty->state[s].transition;
    }
    if (life < HTA_CONT_MIN_LIFE) {
        /* Stretch every state alike, so the colours keep their order. */
        float k = life > 1e-5f ? HTA_CONT_MIN_LIFE / life : 0.0f;
        for (uint32_t s = 0; s < n; s++) {
            if (k > 0.0f) { ty->state[s].duration *= k; ty->state[s].transition *= k; }
            else if (s + 1 < n) ty->state[s].transition = HTA_CONT_MIN_LIFE / (float)(n - 1);
        }
        life = HTA_CONT_MIN_LIFE;
    }
    ty->life = life;

    uint32_t bitmap = 0;
    hta_rd_u32(cache, off + CONT_BITMAP + 12u, &bitmap);
    if (!bitmap || bitmap == 0xFFFFFFFFu) return HTA_CONT_NONE;
    if (!c->mesh.textures) {
        c->mesh.textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
        if (!c->mesh.textures) return HTA_CONT_NONE;
    }
    ty->tex = hta_mesh_intern_bitmap(&c->mesh, cache, bitmaps, bitmap, 0);
    if (ty->tex == ~0u) return HTA_CONT_NONE;
    uint16_t seq = 0;
    hta_rd_u16(cache, off + CONT_FIRST_SEQ, &seq);
    ty->sprite.u0 = ty->sprite.v0 = 0.0f;
    ty->sprite.u1 = ty->sprite.v1 = 1.0f;
    hta_bitmap_sprite sp;
    if ((int16_t)seq >= 0 && hta_bitmap_sprite_at(cache, bitmap, seq, &sp) &&
        sp.bitmap_index == 0 && sp.u1 > sp.u0 && sp.v1 > sp.v0)
        ty->sprite = sp;
    /* Art with no alpha reads additively, as the particles found. */
    if (!ty->additive) {
        const hta_bsp_texture *bt = &c->mesh.textures[ty->tex];
        bool has_alpha = false;
        if (bt->rgba)
            for (size_t q = 0; q < (size_t)bt->width * bt->height && !has_alpha; q++)
                if (bt->rgba[q * 4u + 3u] < 250u) has_alpha = true;
        if (!has_alpha) ty->additive = true;
    }
    return c->type_count++;
}

uint32_t hta_contrails_for_projectile(hta_contrails *c, const hta_cache *cache,
                                      const hta_resource_map *bitmaps, uint32_t proj)
{
    if (!c || !cache || !proj) return HTA_CONT_NONE;
    int32_t ti = hta_cache_find_tag_by_id(cache, proj);
    hta_tag_entry t;
    uint32_t off, n = 0, ptr = 0, arr = 0;
    if (ti < 0 || !hta_cache_tag(cache, (uint32_t)ti, &t) ||
        !hta_cache_ptr_to_offset(cache, t.tag_data_ptr, &off) ||
        !hta_read_reflexive(cache, off + OBJ_ATTACHMENTS, &n, &ptr) || !n ||
        !hta_cache_ptr_to_offset(cache, ptr, &arr)) return HTA_CONT_NONE;
    for (uint32_t i = 0; i < n && i < 16u; i++) {
        uint32_t cls = 0, id = 0;
        hta_rd_u32(cache, arr + i * ATTACH_SIZE, &cls);
        hta_rd_u32(cache, arr + i * ATTACH_SIZE + 12u, &id);
        if (cls != HTA_FOURCC('c','o','n','t') || !id || id == 0xFFFFFFFFu) continue;
        uint32_t r = hta_contrails_add(c, cache, bitmaps, id);
        if (r != HTA_CONT_NONE) return r;
    }
    return HTA_CONT_NONE;
}

uint32_t hta_contrails_add_energy(hta_contrails *c, const float color[3], float width, float life)
{
    if (!c || c->loaded || c->type_count>=HTA_CONT_TYPES || c->mesh.texture_count>=256u) return HTA_CONT_NONE;
    if (!c->mesh.textures) {
        c->mesh.textures=calloc(256,sizeof(hta_bsp_texture));
        if (!c->mesh.textures) return HTA_CONT_NONE;
    }
    uint8_t *pixels=malloc(32*4);
    if(!pixels) return HTA_CONT_NONE;
    for(int i=0;i<32;i++) {
        float x=fabsf((i+0.5f)/16.0f-1.0f);
        for(int k=0;k<3;k++) pixels[i*4+k]=(uint8_t)(255.0f*expf(-x*x*5.0f));
        pixels[i*4+3]=255;
    }
    uint32_t tex=c->mesh.texture_count++;
    c->mesh.textures[tex].width=1; c->mesh.textures[tex].height=32;
    c->mesh.textures[tex].rgba=pixels;
    hta_contrail_type *ty=&c->type[c->type_count]; memset(ty,0,sizeof(*ty));
    ty->tex=tex; ty->additive=true; ty->rate=30; ty->life=life;
    ty->repeats_u=ty->repeats_v=1; ty->sprite.u1=ty->sprite.v1=1;
    ty->state_count=2; ty->state[0].duration=life*.45f; ty->state[0].transition=life*.55f;
    ty->state[0].width=width; ty->state[1].width=width*1.5f;
    for(int k=0;k<3;k++) ty->state[0].color[k]=ty->state[1].color[k]=color[k];
    ty->state[0].color[3]=1; ty->state[1].color[3]=0;
    return c->type_count++;
}

uint32_t hta_contrails_add_laser(hta_contrails *c)
{
    const float red[3]={1.0f,.025f,.008f};
    return hta_contrails_add_energy(c,red,.22f,.38f);
}

bool hta_contrails_build(hta_contrails *c, char *err, size_t errlen)
{
    if (!c || !c->type_count) {
        if (err) snprintf(err, errlen, "no contrails");
        return false;
    }
    uint32_t quads = c->type_count * HTA_CONT_TRAILS * HTA_CONT_POINTS;
    c->mesh.vertices = (hta_vertex *)calloc((size_t)quads * 4u, sizeof(hta_vertex));
    c->mesh.indices = (uint32_t *)calloc((size_t)quads * 6u, sizeof(uint32_t));
    c->mesh.submeshes = (hta_submesh *)calloc(c->type_count, sizeof(hta_submesh));
    if (!c->mesh.vertices || !c->mesh.indices || !c->mesh.submeshes) {
        if (err) snprintf(err, errlen, "out of memory");
        return false;
    }
    for (uint32_t q = 0; q < quads; q++) {
        uint32_t b = q * 4u, *ix = &c->mesh.indices[q * 6u];
        ix[0] = b; ix[1] = b + 1; ix[2] = b + 2; ix[3] = b; ix[4] = b + 2; ix[5] = b + 3;
    }
    uint32_t per_type = HTA_CONT_TRAILS * HTA_CONT_POINTS;
    for (uint32_t t = 0; t < c->type_count; t++) {
        hta_submesh *sm = &c->mesh.submeshes[t];
        hta_submesh_init(sm);
        sm->first_index = t * per_type * 6u;
        sm->index_count = per_type * 6u;
        sm->albedo_tex = c->type[t].tex;
        sm->draw_mode = c->type[t].additive ? HTA_DRAW_ADD : HTA_DRAW_ALPHA;
    }
    c->mesh.vertex_count = quads * 4u;
    c->mesh.index_count = quads * 6u;
    c->mesh.submesh_count = c->type_count;
    c->loaded = true;
    if (err) snprintf(err, errlen, "%u contrail type(s), %u quads", c->type_count, quads);
    return true;
}

/* A point's width and colour at an age. */
static void sample(const hta_contrail_type *ty, float age, float *width, float rgba[4])
{
    const hta_contrail_state *s = ty->state;
    uint32_t n = ty->state_count;
    for (uint32_t i = 0; i < n; i++) {
        if (age <= s[i].duration || i + 1 == n) {
            *width = s[i].width;
            memcpy(rgba, s[i].color, sizeof(float) * 4u);
            if (i + 1 == n && age > s[i].duration) rgba[3] = 0.0f;
            return;
        }
        age -= s[i].duration;
        if (age <= s[i].transition && s[i].transition > 0.0f) {
            float f = age / s[i].transition;
            *width = s[i].width + (s[i + 1].width - s[i].width) * f;
            for (int k = 0; k < 4; k++) rgba[k] = s[i].color[k] + (s[i + 1].color[k] - s[i].color[k]) * f;
            return;
        }
        age -= s[i].transition;
    }
    *width = 0.0f;
    memset(rgba, 0, sizeof(float) * 4u);
}

static hta_contrail *claim(hta_contrails *c, uint32_t type, uint32_t key, bool tracer)
{
    hta_contrail *best = NULL;
    for (uint32_t i = 0; i < HTA_CONT_TRAILS; i++) {
        hta_contrail *tr = &c->trail[type][i];
        if (!tracer && tr->used && !tr->tracer && tr->key == key) return tr;
    }
    /* A free one, or the one closest to finished. */
    for (uint32_t i = 0; i < HTA_CONT_TRAILS; i++) {
        hta_contrail *tr = &c->trail[type][i];
        if (!tr->used) { best = tr; break; }
        if (!tr->fed && (!best || best->fed || tr->count < best->count)) best = tr;
    }
    if (!best) best = &c->trail[type][0];
    memset(best, 0, sizeof(*best));
    best->used = true;
    best->key = key;
    return best;
}

static void push_point(hta_contrail *tr, const float pos[3])
{
    uint32_t n = tr->count < HTA_CONT_POINTS - 1u ? tr->count : HTA_CONT_POINTS - 2u;
    memmove(&tr->pt[1], &tr->pt[0], n * sizeof(tr->pt[0]));
    memcpy(tr->pt[0].pos, pos, sizeof(tr->pt[0].pos));
    tr->pt[0].age = 0.0f;
    tr->count = n + 1u;
}

void hta_contrails_feed(hta_contrails *c, uint32_t type, uint32_t key,
                        const float pos[3], float age)
{
    if (!c || !c->loaded || type >= c->type_count || !pos) return;
    hta_contrail *tr = claim(c, type, key, false);
    float jump = 0.0f;
    if (tr->have_head) {
        float d[3] = { pos[0]-tr->head[0], pos[1]-tr->head[1], pos[2]-tr->head[2] };
        jump = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    }
    /* A reused slot: its age went backwards, or (a client that is only
     * told positions) it jumped further than any round flies in a frame. */
    if (tr->have_head && (age < tr->last_feed_age || jump > 40.0f)) {
        /* The slot was reused by a new round: a new trail. */
        memset(tr, 0, sizeof(*tr));
        tr->used = true;
        tr->key = key;
    }
    if (!tr->have_head) push_point(tr, pos);
    memcpy(tr->head, pos, sizeof(tr->head));
    tr->have_head = true;
    tr->fed = true;
    tr->last_feed_age = age;
    tr->accum = tr->accum;   /* kept */
    tr->tracer = false;
    tr->travelled = -1.0f;   /* marks "fed this frame" */
}

void hta_contrails_tracer(hta_contrails *c, uint32_t type, const float from[3],
                          const float to[3], float speed)
{
    if (!c || !c->loaded || type >= c->type_count || !from || !to) return;
    float d[3] = { to[0]-from[0], to[1]-from[1], to[2]-from[2] };
    float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (len < 0.05f) return;
    hta_contrail *tr = claim(c, type, 0, true);
    tr->tracer = true;
    tr->fed = true;
    memcpy(tr->from, from, sizeof(tr->from));
    for (int k = 0; k < 3; k++) tr->dir[k] = d[k] / len;
    tr->length = len;
    tr->speed = speed > 1.0f ? speed : 300.0f;
    tr->travelled = 0.0f;
    memcpy(tr->head, from, sizeof(tr->head));
    tr->have_head = true;
    push_point(tr, from);
}

void hta_contrails_beam_key(hta_contrails *c, uint32_t type, uint32_t key,
                            const float from[3], const float to[3])
{
    if (!c || !c->loaded || type >= c->type_count || !from || !to) return;
    hta_contrail *tr = claim(c, type, key, false);
    tr->tracer = false;
    tr->fed = false;
    tr->travelled = 0.0f;
    tr->count = 2;
    memcpy(tr->pt[0].pos, from, sizeof(tr->pt[0].pos));
    memcpy(tr->pt[1].pos, to, sizeof(tr->pt[1].pos));
    tr->pt[0].age = tr->pt[1].age = 0.0f;
}

void hta_contrails_beam(hta_contrails *c, uint32_t type, const float from[3], const float to[3])
{
    hta_contrails_beam_key(c, type, 0x80000000u, from, to);
}

void hta_contrails_ring(hta_contrails *c, uint32_t type, const float at[3], float radius)
{
    if (!c || !c->loaded || type>=c->type_count) return;
    hta_contrail *tr=claim(c,type,0,true);
    tr->tracer=false; tr->fed=false; tr->travelled=0; tr->count=HTA_CONT_POINTS;
    for(uint32_t i=0;i<HTA_CONT_POINTS;i++) {
        float angle=6.2831853f*(float)i/(HTA_CONT_POINTS-1);
        tr->pt[i].pos[0]=at[0]+cosf(angle)*radius;
        tr->pt[i].pos[1]=at[1]+sinf(angle)*radius;
        tr->pt[i].pos[2]=at[2]; tr->pt[i].age=0;
    }
}

uint32_t hta_contrails_live(const hta_contrails *c)
{
    uint32_t n = 0;
    for (uint32_t t = 0; c && t < c->type_count; t++)
        for (uint32_t i = 0; i < HTA_CONT_TRAILS; i++) n += c->trail[t][i].used;
    return n;
}

static void collapse(hta_vertex *v, uint32_t quads)
{
    memset(v, 0, (size_t)quads * 4u * sizeof(*v));
}

void hta_contrails_update(hta_contrails *c, const hta_camera *cam, float dt)
{
    if (!c || !c->loaded) return;
    if (!(dt > 0.0f)) dt = 0.0f;
    for (uint32_t t = 0; t < c->type_count; t++) {
        const hta_contrail_type *ty = &c->type[t];
        float interval = 1.0f / ty->rate;
        float spread = ty->life / (float)(HTA_CONT_POINTS - 2u);
        if (interval < spread) interval = spread;
        for (uint32_t i = 0; i < HTA_CONT_TRAILS; i++) {
            hta_contrail *tr = &c->trail[t][i];
            hta_vertex *v = &c->mesh.vertices[((t * HTA_CONT_TRAILS + i) * HTA_CONT_POINTS) * 4u];
            if (!tr->used) { collapse(v, HTA_CONT_POINTS); continue; }
            /* Move a tracer's head; stop a fed trail nobody fed. */
            if (tr->tracer && tr->fed) {
                tr->travelled += tr->speed * dt;
                if (tr->travelled >= tr->length) { tr->travelled = tr->length; tr->fed = false; }
                for (int k = 0; k < 3; k++) tr->head[k] = tr->from[k] + tr->dir[k] * tr->travelled;
                if (!tr->fed) push_point(tr, tr->head);
            } else if (!tr->tracer) {
                if (tr->travelled != -1.0f && tr->fed) {
                    tr->fed = false;
                    push_point(tr, tr->head);   /* the trail ends where it did */
                }
                tr->travelled = 0.0f;
            }
            for (uint32_t k = 0; k < tr->count; k++) tr->pt[k].age += dt;
            while (tr->count && tr->pt[tr->count - 1u].age >= ty->life) tr->count--;
            if (tr->fed) {
                tr->accum += dt;
                if (tr->accum >= interval) { tr->accum = fmodf(tr->accum, interval); push_point(tr, tr->head); }
            } else tr->have_head = false;
            if (!tr->fed && !tr->count) { tr->used = false; collapse(v, HTA_CONT_POINTS); continue; }

            /* The ribbon: head (if any), then the points newest first. */
            float pos[HTA_CONT_POINTS + 1][3], age[HTA_CONT_POINTS + 1];
            uint32_t n = 0;
            if (tr->have_head) { memcpy(pos[n], tr->head, sizeof(pos[n])); age[n++] = 0.0f; }
            for (uint32_t k = 0; k < tr->count && n < HTA_CONT_POINTS + 1u; k++) {
                memcpy(pos[n], tr->pt[k].pos, sizeof(pos[n]));
                age[n++] = tr->pt[k].age;
            }
            float total = 0.0f, along[HTA_CONT_POINTS + 1];
            along[0] = 0.0f;
            for (uint32_t k = 1; k < n; k++) {
                float d[3] = { pos[k][0]-pos[k-1][0], pos[k][1]-pos[k-1][1], pos[k][2]-pos[k-1][2] };
                float seg = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
                if (tr->tracer && total + seg > HTA_TRACER_TAIL) {
                    /* Cut the streak at its tail length. */
                    float f = seg > 1e-6f ? (HTA_TRACER_TAIL - total) / seg : 0.0f;
                    for (int m = 0; m < 3; m++) pos[k][m] = pos[k-1][m] + d[m] * f;
                    age[k] = age[k-1] + (age[k] - age[k-1]) * f;
                    total = HTA_TRACER_TAIL;
                    along[k] = total;
                    n = k + 1u;
                    break;
                }
                total += seg;
                along[k] = total;
            }
            uint32_t q = 0;
            bool whole = ty->sprite.u0 <= 0.0f && ty->sprite.u1 >= 1.0f;
            for (uint32_t k = 0; k + 1 < n && q < HTA_CONT_POINTS; k++) {
                float d[3] = { pos[k+1][0]-pos[k][0], pos[k+1][1]-pos[k][1], pos[k+1][2]-pos[k][2] };
                float mid[3] = { (pos[k][0]+pos[k+1][0])*0.5f, (pos[k][1]+pos[k+1][1])*0.5f,
                                 (pos[k][2]+pos[k+1][2])*0.5f };
                float view[3] = { cam->pos[0]-mid[0], cam->pos[1]-mid[1], cam->pos[2]-mid[2] };
                float side[3] = { d[1]*view[2]-d[2]*view[1], d[2]*view[0]-d[0]*view[2],
                                  d[0]*view[1]-d[1]*view[0] };
                float sl = sqrtf(side[0]*side[0] + side[1]*side[1] + side[2]*side[2]);
                if (sl < 1e-6f) {
                    hta_camera_right(cam,side); sl=1.0f;
                }
                for (int m = 0; m < 3; m++) side[m] /= sl;
                hta_vertex *o = &v[q * 4u];
                for (int e = 0; e < 2; e++) {
                    uint32_t p = k + (uint32_t)e;
                    float w, col[4];
                    sample(ty, age[p], &w, col);
                    float f = total > 1e-4f ? along[p] / total : 0.0f;
                    float u = whole ? f * ty->repeats_u
                                    : ty->sprite.u0 + f * (ty->sprite.u1 - ty->sprite.u0);
                    for (int s2 = 0; s2 < 2; s2++) {
                        hta_vertex *vv = &o[e == 0 ? (s2 ? 3 : 0) : (s2 ? 2 : 1)];
                        float sg = s2 ? 0.5f : -0.5f;
                        for (int m = 0; m < 3; m++) vv->pos[m] = pos[p][m] + side[m] * w * sg;
                        for (int m = 0; m < 3; m++) vv->normal[m] = col[m];
                        vv->lm_uv[0] = col[3];
                        vv->lm_uv[1] = 0.0f;
                        vv->uv[0] = u;
                        vv->uv[1] = whole ? (s2 ? ty->repeats_v : 0.0f)
                                          : (s2 ? ty->sprite.v1 : ty->sprite.v0);
                    }
                }
                q++;
            }
            if (q < HTA_CONT_POINTS) collapse(&v[q * 4u], HTA_CONT_POINTS - q);
        }
    }
}
