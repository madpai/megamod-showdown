#include "anim.h"
#include "bsp.h"     /* hta_read_reflexive */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(char *err, size_t n, const char *fmt, ...)
{
    if (!err || !n) return;
    va_list ap; va_start(ap, fmt); vsnprintf(err, n, fmt, ap); va_end(ap);
}

/* ------------------------------ transforms ------------------------------ */

void hta_xf_identity(hta_transform *x)
{
    x->q[0] = x->q[1] = x->q[2] = 0.0f; x->q[3] = 1.0f;
    x->t[0] = x->t[1] = x->t[2] = 0.0f;
    x->s = 1.0f;
}

static void qmul(float o[4], const float a[4], const float b[4])
{
    float r[4];
    r[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    r[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    r[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    r[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
    o[0]=r[0]; o[1]=r[1]; o[2]=r[2]; o[3]=r[3];
}

static void qrot(float o[3], const float q[4], const float v[3])
{
    /* o = v + 2w(q x v) + 2(q x (q x v)) */
    float tx = 2.0f*(q[1]*v[2] - q[2]*v[1]);
    float ty = 2.0f*(q[2]*v[0] - q[0]*v[2]);
    float tz = 2.0f*(q[0]*v[1] - q[1]*v[0]);
    float r[3];
    r[0] = v[0] + q[3]*tx + (q[1]*tz - q[2]*ty);
    r[1] = v[1] + q[3]*ty + (q[2]*tx - q[0]*tz);
    r[2] = v[2] + q[3]*tz + (q[0]*ty - q[1]*tx);
    o[0]=r[0]; o[1]=r[1]; o[2]=r[2];
}

static void qnorm(float q[4])
{
    float n = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (n < 1e-8f) { q[0]=q[1]=q[2]=0.0f; q[3]=1.0f; return; }
    n = 1.0f / n;
    q[0]*=n; q[1]*=n; q[2]*=n; q[3]*=n;
}

void hta_xf_mul(hta_transform *o, const hta_transform *a, const hta_transform *b)
{
    hta_transform r;
    qmul(r.q, a->q, b->q);
    float sv[3] = { b->t[0]*a->s, b->t[1]*a->s, b->t[2]*a->s };
    qrot(r.t, a->q, sv);
    r.t[0] += a->t[0]; r.t[1] += a->t[1]; r.t[2] += a->t[2];
    r.s = a->s * b->s;
    *o = r;
}

void hta_xf_inverse(hta_transform *o, const hta_transform *a)
{
    hta_transform r;
    r.q[0] = -a->q[0]; r.q[1] = -a->q[1]; r.q[2] = -a->q[2]; r.q[3] = a->q[3];
    r.s = (fabsf(a->s) > 1e-8f) ? 1.0f / a->s : 1.0f;
    float nt[3] = { -a->t[0]*r.s, -a->t[1]*r.s, -a->t[2]*r.s };
    qrot(r.t, r.q, nt);
    *o = r;
}

void hta_xf_point(float o[3], const hta_transform *x, const float v[3])
{
    float sv[3] = { v[0]*x->s, v[1]*x->s, v[2]*x->s };
    qrot(o, x->q, sv);
    o[0] += x->t[0]; o[1] += x->t[1]; o[2] += x->t[2];
}

void hta_xf_vector(float o[3], const hta_transform *x, const float v[3])
{
    qrot(o, x->q, v);
}

void hta_xf_lerp(hta_transform *o, const hta_transform *a, const hta_transform *b, float t)
{
    float dot = a->q[0]*b->q[0] + a->q[1]*b->q[1] + a->q[2]*b->q[2] + a->q[3]*b->q[3];
    float sign = dot < 0.0f ? -1.0f : 1.0f;  /* shortest arc */
    for (int i = 0; i < 4; i++) o->q[i] = a->q[i] + (sign*b->q[i] - a->q[i]) * t;
    qnorm(o->q);
    for (int i = 0; i < 3; i++) o->t[i] = a->t[i] + (b->t[i] - a->t[i]) * t;
    o->s = a->s + (b->s - a->s) * t;
}

/* -------------------------------- loading -------------------------------- */

static bool read_tagdata(const hta_cache *c, uint32_t off,
                         uint32_t *out_file_off, uint32_t *out_size)
{
    uint32_t size = 0, ptr = 0;
    if (!hta_rd_u32(c, off + 0u, &size)) return false;
    if (!hta_rd_u32(c, off + 12u, &ptr)) return false;
    *out_size = size;
    *out_file_off = 0;
    if (size == 0) return true;
    uint32_t fo;
    if (!hta_cache_ptr_to_offset(c, ptr, &fo)) return false;
    if ((uint64_t)fo + size > c->size) return false;
    *out_file_off = fo;
    return true;
}

static uint32_t flagged_frame_size(const hta_animation *a)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < a->node_count && i < HTA_ANIM_MAX_NODES; i++) {
        uint32_t w = i / 32u, b = i % 32u;
        if ((a->rot_flags[w]   >> b) & 1u) total += HTA_ANIM_ROT_SIZE;
        if ((a->trans_flags[w] >> b) & 1u) total += HTA_ANIM_TRANS_SIZE;
        if ((a->scale_flags[w] >> b) & 1u) total += HTA_ANIM_SCALE_SIZE;
    }
    return total;
}

bool hta_anim_load(hta_anim_graph *g, const hta_cache *c, uint32_t antr_tag_id,
                   char *err, size_t errlen)
{
    if (!g || !c) { fail(err, errlen, "bad arguments"); return false; }
    memset(g, 0, sizeof(*g));
    g->cache = c;
    g->tag_id = antr_tag_id;

    int32_t ti = hta_cache_find_tag_by_id(c, antr_tag_id);
    if (ti < 0) { fail(err, errlen, "antr 0x%08X missing", antr_tag_id); return false; }
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.primary_class != HTA_TAG_ANTR) {
        fail(err, errlen, "tag 0x%08X is not antr", antr_tag_id); return false;
    }
    hta_cache_tag_path(c, &t, g->path, sizeof(g->path));
    uint32_t off;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &off)) {
        fail(err, errlen, "antr tag data unreachable"); return false;
    }

    /* nodes */
    uint32_t n = 0, ptr = 0, arr = 0;
    if (!hta_read_reflexive(c, off + HTA_ANTR_NODES, &n, &ptr) || n == 0) {
        fail(err, errlen, "antr has no nodes"); return false;
    }
    if (n > HTA_ANIM_MAX_NODES) {
        fail(err, errlen, "antr has %u nodes (max %u)", n, HTA_ANIM_MAX_NODES);
        return false;
    }
    if (!hta_cache_ptr_to_offset(c, ptr, &arr)) {
        fail(err, errlen, "antr node array unreachable"); return false;
    }
    g->node_count = n;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t nb = arr + i * HTA_ANTR_NODE_SIZE;
        memset(g->nodes[i].name, 0, sizeof(g->nodes[i].name));
        hta_rd_bytes(c, nb, g->nodes[i].name, 31);
        uint16_t par = 0xFFFFu;
        hta_rd_u16(c, nb + HTA_ANTR_NODE_PARENT, &par);
        int16_t p = (int16_t)par;
        if (p >= (int16_t)n) p = -1;
        g->nodes[i].parent = p;
    }

    /* sound references -- indexed by each animation's `sound` field */
    if (hta_read_reflexive(c, off + HTA_ANTR_SOUND_REFS, &n, &ptr) && n) {
        uint32_t sarr;
        if (hta_cache_ptr_to_offset(c, ptr, &sarr)) {
            if (n > HTA_ANIM_MAX_SOUNDS) n = HTA_ANIM_MAX_SOUNDS;
            g->sound_count = n;
            for (uint32_t i = 0; i < n; i++)
                hta_rd_u32(c, sarr + i * HTA_ANTR_SNDREF_SIZE + 12u, &g->sound_ids[i]);
        }
    }

    /* animations */
    if (!hta_read_reflexive(c, off + HTA_ANTR_ANIMATIONS, &n, &ptr) || n == 0) {
        fail(err, errlen, "antr has no animations"); return false;
    }
    if (n > 512) { fail(err, errlen, "antr has %u animations", n); return false; }
    if (!hta_cache_ptr_to_offset(c, ptr, &arr)) {
        fail(err, errlen, "antr animation array unreachable"); return false;
    }
    g->anims = (hta_animation *)calloc(n, sizeof(hta_animation));
    if (!g->anims) { fail(err, errlen, "oom"); return false; }
    g->anim_count = n;

    for (uint32_t i = 0; i < n; i++) {
        hta_animation *a = &g->anims[i];
        uint32_t ab = arr + i * HTA_ANTR_ANIM_SIZE;
        hta_rd_bytes(c, ab, a->name, 31);
        hta_rd_u16(c, ab + HTA_ANIM_TYPE, &a->type);
        hta_rd_u16(c, ab + HTA_ANIM_FRAME_COUNT, &a->frame_count);
        hta_rd_u16(c, ab + HTA_ANIM_FRAME_SIZE, &a->frame_size);
        hta_rd_u16(c, ab + HTA_ANIM_NODE_COUNT, &a->node_count);
        hta_rd_u16(c, ab + HTA_ANIM_FLAGS, &a->flags);
        uint16_t u;
        hta_rd_u16(c, ab + HTA_ANIM_LOOP_FRAME, &u);  a->loop_frame  = (int16_t)u;
        hta_rd_u16(c, ab + HTA_ANIM_KEY_FRAME, &u);   a->key_frame   = (int16_t)u;
        hta_rd_u16(c, ab + HTA_ANIM_NEXT, &u);        a->next_anim   = (int16_t)u;
        hta_rd_u16(c, ab + HTA_ANIM_SOUND, &u);       a->sound_index = (int16_t)u;
        hta_rd_u16(c, ab + HTA_ANIM_SOUND_FRAME, &u); a->sound_frame = (int16_t)u;
        for (int k = 0; k < 2; k++) {
            hta_rd_u32(c, ab + HTA_ANIM_TRANS_FLAGS + (uint32_t)k*4u, &a->trans_flags[k]);
            hta_rd_u32(c, ab + HTA_ANIM_ROT_FLAGS   + (uint32_t)k*4u, &a->rot_flags[k]);
            hta_rd_u32(c, ab + HTA_ANIM_SCALE_FLAGS + (uint32_t)k*4u, &a->scale_flags[k]);
        }
        read_tagdata(c, ab + HTA_ANIM_DEFAULT_DATA, &a->default_off, &a->default_size);
        read_tagdata(c, ab + HTA_ANIM_FRAME_DATA, &a->frame_off, &a->frame_bytes);

        if (a->node_count > g->node_count) a->node_count = (uint16_t)g->node_count;
        if (a->flags & HTA_ANIM_FLAG_COMPRESSED) continue;  /* left unusable */

        /* The invariant that proves the flag fields are the right way round. */
        uint32_t want = flagged_frame_size(a);
        if (want != a->frame_size ||
            (a->default_size != 0 &&
             a->default_size != a->node_count * HTA_ANIM_NODE_STRIDE - want)) {
            fail(err, errlen,
                 "animation '%s': frame size %u != %u computed from flags",
                 a->name, a->frame_size, want);
            free(g->anims); g->anims = NULL; g->anim_count = 0;
            return false;
        }
    }
    return true;
}

void hta_anim_free(hta_anim_graph *g)
{
    if (!g) return;
    free(g->anims);
    memset(g, 0, sizeof(*g));
}

static int ci_eq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

int32_t hta_anim_find(const hta_anim_graph *g, const char *needle)
{
    if (!g || !needle || !*needle) return -1;
    for (uint32_t i = 0; i < g->anim_count; i++) {
        const char *h = g->anims[i].name;
        for (; *h; h++) {
            const char *a = h, *b = needle;
            while (*b && *a && ci_eq(*a, *b)) { a++; b++; }
            if (!*b) return (int32_t)i;
        }
    }
    return -1;
}

int32_t hta_anim_node_index(const hta_anim_graph *g, const char *name)
{
    if (!g || !name) return -1;
    for (uint32_t i = 0; i < g->node_count; i++)
        if (strncmp(g->nodes[i].name, name, 31) == 0) return (int32_t)i;
    return -1;
}

/* -------------------------------- sampling ------------------------------- */

/* Reads one node's components for `frame`, advancing the two cursors. */
static void read_node(const hta_cache *c, const hta_animation *a, uint32_t node,
                      uint32_t *dcur, uint32_t *fcur, hta_transform *out)
{
    uint32_t w = node / 32u, b = node % 32u;
    int16_t q[4] = {0,0,0,0};
    uint32_t src;

    if ((a->rot_flags[w] >> b) & 1u) { src = *fcur; *fcur += HTA_ANIM_ROT_SIZE; }
    else                             { src = *dcur; *dcur += HTA_ANIM_ROT_SIZE; }
    for (int i = 0; i < 4; i++) {
        uint16_t v = 0;
        hta_rd_u16(c, src + (uint32_t)i*2u, &v);
        q[i] = (int16_t)v;
    }
    /* Halo stores node rotations parent-relative in the opposite sense to the
     * child-to-parent convention hta_xf_mul composes in, so conjugate on the
     * way in. With the raw values the AR's barrel (its local +X) comes out
     * pointing (-0.10, +0.81, +0.58) -- up and to the left; conjugated it is
     * (+1.000, +0.006, +0.008), straight down the view, and the gun lands on
     * the right. See docs/HANDOFF.md. */
    out->q[0] = -(float)q[0] / 32767.0f;
    out->q[1] = -(float)q[1] / 32767.0f;
    out->q[2] = -(float)q[2] / 32767.0f;
    out->q[3] =  (float)q[3] / 32767.0f;
    qnorm(out->q);

    if ((a->trans_flags[w] >> b) & 1u) { src = *fcur; *fcur += HTA_ANIM_TRANS_SIZE; }
    else                               { src = *dcur; *dcur += HTA_ANIM_TRANS_SIZE; }
    for (int i = 0; i < 3; i++) hta_rd_f32(c, src + (uint32_t)i*4u, &out->t[i]);

    if ((a->scale_flags[w] >> b) & 1u) { src = *fcur; *fcur += HTA_ANIM_SCALE_SIZE; }
    else                               { src = *dcur; *dcur += HTA_ANIM_SCALE_SIZE; }
    hta_rd_f32(c, src, &out->s);
    if (!(out->s > 1e-6f)) out->s = 1.0f;  /* also catches NaN */
}

static bool sample_frame(const hta_anim_graph *g, const hta_animation *a,
                         uint32_t frame, hta_transform *out)
{
    uint32_t dcur = a->default_off;
    uint32_t fcur = a->frame_off + frame * a->frame_size;
    if (a->frame_size &&
        (uint64_t)fcur + a->frame_size > (uint64_t)a->frame_off + a->frame_bytes)
        return false;
    for (uint32_t i = 0; i < a->node_count; i++)
        read_node(g->cache, a, i, &dcur, &fcur, &out[i]);
    for (uint32_t i = a->node_count; i < g->node_count; i++)
        hta_xf_identity(&out[i]);
    return true;
}

bool hta_anim_animates(const hta_anim_graph *g, uint32_t anim, uint32_t node)
{
    if (!g || anim >= g->anim_count || node >= g->node_count) return false;
    const hta_animation *a = &g->anims[anim];
    if (node >= a->node_count) return false;
    uint32_t w = node / 32u, b = node % 32u;
    return ((a->rot_flags[w] >> b) & 1u) ||
           ((a->trans_flags[w] >> b) & 1u) ||
           ((a->scale_flags[w] >> b) & 1u);
}

bool hta_anim_sample(const hta_anim_graph *g, uint32_t anim, float frame,
                     hta_transform *out)
{
    if (!g || !out || anim >= g->anim_count) return false;
    const hta_animation *a = &g->anims[anim];
    if (a->flags & HTA_ANIM_FLAG_COMPRESSED) return false;
    if (a->frame_count == 0 || a->node_count == 0) return false;

    if (frame < 0.0f) frame = 0.0f;
    float maxf = (float)(a->frame_count - 1);
    if (frame > maxf) frame = maxf;
    uint32_t f0 = (uint32_t)frame;
    float mix = frame - (float)f0;
    if (f0 >= a->frame_count) f0 = a->frame_count - 1u;

    if (!sample_frame(g, a, f0, out)) return false;
    if (mix > 1e-4f && f0 + 1u < a->frame_count) {
        hta_transform nxt[HTA_ANIM_MAX_NODES];
        if (sample_frame(g, a, f0 + 1u, nxt))
            for (uint32_t i = 0; i < g->node_count; i++)
                hta_xf_lerp(&out[i], &out[i], &nxt[i], mix);
    }
    return true;
}

void hta_anim_world(const hta_anim_graph *g, const hta_transform *local,
                    hta_transform *out)
{
    if (!g || !local || !out) return;
    uint8_t done[HTA_ANIM_MAX_NODES];
    memset(done, 0, sizeof(done));
    /* Trial graphs list parents before children, but don't rely on it. */
    for (uint32_t pass = 0; pass < g->node_count; pass++) {
        uint32_t progress = 0;
        for (uint32_t i = 0; i < g->node_count; i++) {
            if (done[i]) continue;
            int16_t p = g->nodes[i].parent;
            if (p < 0 || (uint32_t)p == i) { out[i] = local[i]; done[i] = 1; progress++; }
            else if (done[p]) { hta_xf_mul(&out[i], &out[p], &local[i]); done[i] = 1; progress++; }
        }
        if (!progress) break;
    }
    for (uint32_t i = 0; i < g->node_count; i++)
        if (!done[i]) out[i] = local[i];  /* cycle in the hierarchy */
}
