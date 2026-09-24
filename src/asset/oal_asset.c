#include "oal_asset.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define FILE_MAX (128u * 1024u * 1024u)

static bool fail(char *err, size_t n, const char *s) { if (err && n) snprintf(err, n, "%s", s); return false; }

typedef struct { const uint8_t *d; size_t size, at; bool bad; } rd;
static const uint8_t *take(rd *r, size_t n)
{
    if (r->bad || n > r->size - r->at) { r->bad = true; return NULL; }
    const uint8_t *p = r->d + r->at; r->at += n; return p;
}
static uint32_t u32(rd *r) { const uint8_t *p = take(r, 4); uint32_t v = 0; if (p) memcpy(&v, p, 4); return v; }
static float f32(rd *r) { const uint8_t *p = take(r, 4); float v = 0; if (p) memcpy(&v, p, 4); if (!isfinite(v)) r->bad = true; return v; }
static void name(rd *r, char *out, size_t n, size_t field)
{
    const uint8_t *p = take(r, field);
    if (!p) { out[0] = 0; return; }
    size_t k = 0;
    for (; k + 1 < n && k < field && p[k]; k++) out[k] = (char)((p[k] >= 32 && p[k] < 127) ? p[k] : '?');
    out[k] = 0;
}

/* The manifest is compact JSON with sorted keys; only a handful of scalars
 * are wanted, found as "key":value at the top level or in "stats". */
static const char *json_find(const char *j, size_t n, const char *key)
{
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    size_t pl = strlen(pat);
    for (size_t i = 0; i + pl <= n; i++)
        if (!memcmp(j + i, pat, pl) && (i == 0 || j[i - 1] == '{' || j[i - 1] == ',')) return j + i + pl;
    return NULL;
}
static void json_str(const char *j, size_t n, const char *key, char *out, size_t on)
{
    const char *v = json_find(j, n, key);
    out[0] = 0;
    if (!v || *v != '"') return;
    size_t k = 0;
    for (v++; v < j + n && *v != '"' && k + 1 < on; v++) out[k++] = *v;
    out[k] = 0;
}
static float json_num(const char *j, size_t n, const char *key)
{
    const char *v = json_find(j, n, key);
    if (!v) return 0.0f;
    char buf[32]; size_t k = 0;
    while (v < j + n && k + 1 < sizeof(buf) && (isdigit((unsigned char)*v) || strchr("+-.eE", *v))) buf[k++] = *v++;
    buf[k] = 0;
    float f = strtof(buf, NULL);
    return isfinite(f) ? f : 0.0f;
}

static void model_free(hta_oal_model *m)
{
    hta_bsp_free(&m->mesh);
    free(m->vbone); free(m->vweight); free(m->bone_name); free(m->parent); free(m->inv_bind);
    free(m->bind); free(m->att);
    for (uint32_t c = 0; c < m->clip_count; c++) free(m->clips ? m->clips[c].keys : NULL);
    free(m->clips);
    memset(m, 0, sizeof(*m));
}

void hta_oal_free(hta_oal_asset *a)
{
    if (!a) return;
    for (uint32_t i = 0; i < HTA_OAL_MAX_MODELS; i++) model_free(&a->models[i]);
    for (uint32_t i = 0; i < HTA_OAL_MAX_SOUNDS; i++) free(a->sounds[i].samples);
    memset(a, 0, sizeof(*a));
}

static bool read_model(rd *r, hta_oal_model *m, char *err, size_t errlen)
{
    uint32_t vc = u32(r), ic = u32(r), gc = u32(r), tc = u32(r), bc = u32(r), ac = u32(r), cc = u32(r);
    u32(r);
    if (r->bad || !vc || vc > 1000000u || !ic || ic > 3000000u || ic % 3 || !gc || gc > 1024u ||
        !tc || tc > 256u || !bc || bc > HTA_OAL_MAX_BONES || ac > 256u || cc > 64u)
        return fail(err, errlen, "model counts out of range");
    hta_bsp_mesh *mesh = &m->mesh;
    mesh->vertices = calloc(vc, sizeof(hta_vertex));
    m->vbone = calloc(vc, sizeof(*m->vbone)); m->vweight = calloc(vc, sizeof(*m->vweight));
    mesh->indices = malloc((size_t)ic * 4u);
    mesh->submeshes = calloc(gc, sizeof(hta_submesh));
    mesh->textures = calloc(tc, sizeof(hta_bsp_texture));
    if (!mesh->vertices || !m->vbone || !m->vweight || !mesh->indices || !mesh->submeshes || !mesh->textures)
        return fail(err, errlen, "out of memory");
    mesh->vertex_count = vc; mesh->index_count = ic; mesh->submesh_count = gc; mesh->texture_count = tc;
    for (int k = 0; k < 3; k++) { mesh->bounds_min[k] = 1e30f; mesh->bounds_max[k] = -1e30f; }
    for (uint32_t i = 0; i < vc; i++) {
        hta_vertex *v = &mesh->vertices[i];
        for (int k = 0; k < 3; k++) v->pos[k] = f32(r);
        for (int k = 0; k < 3; k++) v->normal[k] = f32(r);
        v->uv[0] = f32(r); v->uv[1] = f32(r);
        const uint8_t *b = take(r, 4);
        if (b) memcpy(m->vbone[i], b, 4);
        for (int k = 0; k < 3; k++) m->vweight[i][k] = f32(r);
        for (int k = 0; k < 3; k++) {
            if (m->vbone[i][k] >= bc) r->bad = true;
            if (v->pos[k] < mesh->bounds_min[k]) mesh->bounds_min[k] = v->pos[k];
            if (v->pos[k] > mesh->bounds_max[k]) mesh->bounds_max[k] = v->pos[k];
        }
    }
    for (uint32_t i = 0; i < ic; i++) { mesh->indices[i] = u32(r); if (mesh->indices[i] >= vc) r->bad = true; }
    uint32_t end = 0;
    for (uint32_t i = 0; i < gc; i++) {
        uint32_t first = u32(r), count = u32(r), tex = u32(r);
        uint32_t flags = u32(r);
        if (first != end || !count || count % 3 || count > ic - first || tex >= tc) r->bad = true;
        hta_submesh *s = &mesh->submeshes[i];
        hta_submesh_init(s);
        s->first_index = first; s->index_count = count; s->albedo_tex = tex;
        s->scene_lit = true;   /* no lightmap: lit by the scene like Halo's bodies */
        /* Bit 1: drawn blended by its texture's alpha (hair cards, lace,
         * glasses), as an OALMAP group's is. */
        if (flags & 2u) s->draw_mode = HTA_DRAW_ALPHA;
        end = first + count;
    }
    if (end != ic) r->bad = true;
    for (uint32_t i = 0; i < tc && !r->bad; i++) {
        uint32_t w = u32(r), h = u32(r), n = u32(r);
        if (!w || !h || w > 4096 || h > 4096 || (uint64_t)w * h * 4u != n) { r->bad = true; break; }
        const uint8_t *px = take(r, n);
        if (!px) break;
        mesh->textures[i].width = w; mesh->textures[i].height = h;
        mesh->textures[i].rgba = malloc(n);
        if (!mesh->textures[i].rgba) return fail(err, errlen, "out of memory");
        memcpy(mesh->textures[i].rgba, px, n);
    }
    m->bone_count = bc;
    m->bone_name = calloc(bc, sizeof(*m->bone_name)); m->parent = calloc(bc, sizeof(int32_t));
    m->inv_bind = calloc(bc, sizeof(*m->inv_bind)); m->bind = calloc(bc, sizeof(hta_oal_key));
    if (!m->bone_name || !m->parent || !m->inv_bind || !m->bind) return fail(err, errlen, "out of memory");
    for (uint32_t i = 0; i < bc; i++) {
        name(r, m->bone_name[i], 64, 64);
        m->parent[i] = (int32_t)u32(r);
        if (m->parent[i] >= (int32_t)i || m->parent[i] < -1) r->bad = true;
        for (int k = 0; k < 12; k++) m->inv_bind[i][k] = f32(r);
        for (int k = 0; k < 3; k++) m->bind[i].p[k] = f32(r);
        for (int k = 0; k < 4; k++) m->bind[i].q[k] = f32(r);
    }
    m->att_count = ac;
    if (ac && !(m->att = calloc(ac, sizeof(hta_oal_attachment)))) return fail(err, errlen, "out of memory");
    for (uint32_t i = 0; i < ac; i++) {
        name(r, m->att[i].name, 64, 64);
        m->att[i].bone = (int32_t)u32(r);
        if (m->att[i].bone < 0 || m->att[i].bone >= (int32_t)bc) r->bad = true;
        for (int k = 0; k < 12; k++) m->att[i].local[k] = f32(r);
    }
    m->clip_count = cc;
    if (cc && !(m->clips = calloc(cc, sizeof(hta_oal_clip)))) return fail(err, errlen, "out of memory");
    for (uint32_t c = 0; c < cc && !r->bad; c++) {
        hta_oal_clip *cl = &m->clips[c];
        name(r, cl->role, sizeof(cl->role), 16);
        cl->fps = f32(r); cl->frames = u32(r); cl->loop = u32(r) != 0;
        if (!(cl->fps > 0.0f) || cl->fps > 1000.0f || !cl->frames || cl->frames > 10000u) { r->bad = true; break; }
        size_t n = (size_t)cl->frames * bc;
        cl->keys = malloc(n * sizeof(hta_oal_key));
        if (!cl->keys) return fail(err, errlen, "out of memory");
        for (size_t k = 0; k < n; k++) {
            for (int q = 0; q < 4; q++) cl->keys[k].q[q] = f32(r);
            for (int q = 0; q < 3; q++) cl->keys[k].p[q] = f32(r);
        }
    }
    if (r->bad) return fail(err, errlen, "truncated or invalid model");
    return true;
}

bool hta_oal_load_memory(const uint8_t *data, size_t size, hta_oal_asset *out, char *err, size_t errlen)
{
    if (!data || !out) return fail(err, errlen, "invalid arguments");
    memset(out, 0, sizeof(*out));
    if (size < 32 || size > FILE_MAX || memcmp(data, "OALA", 4)) return fail(err, errlen, "not an OALASSET");
    rd r = { data, size, 4, false };
    uint32_t version = u32(&r), ml = u32(&r), mc = u32(&r);
    if (version != 1) return fail(err, errlen, "unsupported OALASSET version");
    r.at = 32;
    if (mc > HTA_OAL_MAX_MODELS || ml > 4u * 1024u * 1024u) return fail(err, errlen, "asset counts out of range");
    const char *j = (const char *)take(&r, ml);
    if (!j) return fail(err, errlen, "truncated manifest");
    json_str(j, ml, "kind", out->kind, sizeof(out->kind));
    json_str(j, ml, "name", out->name, sizeof(out->name));
    json_str(j, ml, "display_name", out->display, sizeof(out->display));
    if (!out->display[0]) snprintf(out->display, sizeof(out->display), "%s", out->name);
    json_str(j, ml, "base", out->base, sizeof(out->base));
    out->rounds_per_second = json_num(j, ml, "rounds_per_second");
    out->damage_scale = json_num(j, ml, "damage_scale");
    out->spread_scale = json_num(j, ml, "spread_scale");
    out->magazine = (int)json_num(j, ml, "magazine");
    out->reserve = (int)json_num(j, ml, "reserve");
    out->reload_rounds = (int)json_num(j, ml, "reload_rounds");
    out->reload_seconds = json_num(j, ml, "reload_seconds");
    json_str(j, ml, "crosshair", out->crosshair, sizeof(out->crosshair));
    out->crosshair_size = json_num(j, ml, "crosshair_size");
    {
        /* "loadout":["primary","secondary"] */
        const char *v = json_find(j, ml, "loadout");
        for (int k = 0; v && k < 2; k++) {
            while (v < j + ml && *v != '"' && *v != ']') v++;
            if (v >= j + ml || *v != '"') break;
            size_t n = 0;
            for (v++; v < j + ml && *v != '"' && n + 1 < sizeof(out->loadout[k]); v++) out->loadout[k][n++] = *v;
            out->loadout[k][n] = 0;
            if (v < j + ml) v++;
        }
    }
    {
        const char *v = json_find(j, ml, "view_model_mirrored");
        out->view_mirrored = v && !strncmp(v, "true", 4);
    }
    if (strcmp(out->kind, "character") && strcmp(out->kind, "weapon") && strcmp(out->kind, "sounds"))
        return fail(err, errlen, "unknown asset kind");
    /* A character and a weapon have models; a sound pack (the hit and kill
     * dings) has none. */
    if (!strcmp(out->kind, "sounds") ? mc != 0 : mc == 0) return fail(err, errlen, "asset counts out of range");
    if (!strcmp(out->kind, "weapon") && (mc != 2 || !out->base[0])) return fail(err, errlen, "a weapon needs a world and a view model and a base");
    out->model_count = mc;
    for (uint32_t i = 0; i < mc; i++)
        if (!read_model(&r, &out->models[i], err, errlen)) { hta_oal_free(out); return false; }
    if (out->view_mirrored && mc > 1) {
        /* Mirror the view model across its Y (left/right) once: bind pose,
         * bones, clips, and the winding, so it faces out again. Skinning
         * a mirrored skeleton gives the mirrored mesh. */
        hta_oal_model *vm = &out->models[1];
        for (uint32_t i = 0; i < vm->mesh.vertex_count; i++) {
            vm->mesh.vertices[i].pos[1] = -vm->mesh.vertices[i].pos[1];
            vm->mesh.vertices[i].normal[1] = -vm->mesh.vertices[i].normal[1];
        }
        for (uint32_t i = 0; i + 2 < vm->mesh.index_count; i += 3) {
            uint32_t t = vm->mesh.indices[i + 1];
            vm->mesh.indices[i + 1] = vm->mesh.indices[i + 2];
            vm->mesh.indices[i + 2] = t;
        }
        /* M' = S M S with S = diag(1,-1,1): negate the Y row and Y column
         * of every rotation, and the Y of every translation; a quaternion
         * (x,y,z,w) mirrors to (-x,y,-z,w). */
        for (uint32_t b = 0; b < vm->bone_count; b++) {
            float *m = vm->inv_bind[b];
            m[1] = -m[1]; m[4] = -m[4]; m[6] = -m[6]; m[9] = -m[9]; m[7] = -m[7];
            vm->bind[b].q[0] = -vm->bind[b].q[0]; vm->bind[b].q[2] = -vm->bind[b].q[2];
            vm->bind[b].p[1] = -vm->bind[b].p[1];
        }
        for (uint32_t c = 0; c < vm->clip_count; c++)
            for (size_t k = 0; k < (size_t)vm->clips[c].frames * vm->bone_count; k++) {
                hta_oal_key *key = &vm->clips[c].keys[k];
                key->q[0] = -key->q[0]; key->q[2] = -key->q[2]; key->p[1] = -key->p[1];
            }
        for (uint32_t a = 0; a < vm->att_count; a++) {
            float *m = vm->att[a].local;
            m[1] = -m[1]; m[4] = -m[4]; m[6] = -m[6]; m[9] = -m[9]; m[7] = -m[7];
        }
    }
    if (!strcmp(out->kind, "character")) {
        /* The team mask: one more 1x1 texture, blue = HTA_OAL_TEAM_TINT. */
        hta_bsp_mesh *mesh = &out->models[0].mesh;
        hta_bsp_texture *t = realloc(mesh->textures, (mesh->texture_count + 1) * sizeof(*t));
        uint8_t *px = malloc(4);
        if (!t || !px) { free(px); if (t) mesh->textures = t; hta_oal_free(out); return fail(err, errlen, "out of memory"); }
        mesh->textures = t;
        px[0] = px[1] = 0; px[2] = HTA_OAL_TEAM_TINT; px[3] = 255;
        memset(&t[mesh->texture_count], 0, sizeof(*t));
        t[mesh->texture_count].width = t[mesh->texture_count].height = 1;
        t[mesh->texture_count].rgba = px;
        for (uint32_t s = 0; s < mesh->submesh_count; s++) {
            mesh->submeshes[s].multi_tex = mesh->texture_count;
            mesh->submeshes[s].change_color = 1;
        }
        mesh->texture_count++;
    }
    while (r.at < size && out->sound_count < HTA_OAL_MAX_SOUNDS) {
        hta_oal_sound *s = &out->sounds[out->sound_count];
        name(&r, s->role, sizeof(s->role), 16);
        s->rate = u32(&r); s->channels = u32(&r); s->frames = u32(&r);
        if (r.bad || s->rate < 4000 || s->rate > 96000 || !s->channels || s->channels > 2 || s->frames > 48000u * 60u) {
            hta_oal_free(out); return fail(err, errlen, "invalid sound");
        }
        size_t n = (size_t)s->frames * s->channels * 2u;
        const uint8_t *p = take(&r, n);
        if (!p || !(s->samples = malloc(n))) { hta_oal_free(out); return fail(err, errlen, "truncated sound"); }
        memcpy(s->samples, p, n);
        out->sound_count++;
    }
    if (r.at != size) { hta_oal_free(out); return fail(err, errlen, "unexpected trailing bytes"); }
    out->loaded = true;
    return true;
}

bool hta_oal_load(const char *path, hta_oal_asset *out, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) return fail(err, errlen, "cannot open asset");
    if (fseek(f, 0, SEEK_END)) { fclose(f); return fail(err, errlen, "cannot size asset"); }
    long n = ftell(f);
    if (n < 32 || (unsigned long)n > FILE_MAX) { fclose(f); return fail(err, errlen, "asset size out of range"); }
    rewind(f);
    uint8_t *d = malloc((size_t)n);
    if (!d) { fclose(f); return fail(err, errlen, "out of memory"); }
    bool ok = fread(d, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    ok = ok && hta_oal_load_memory(d, (size_t)n, out, err, errlen);
    free(d);
    return ok;
}

int32_t hta_oal_clip_find(const hta_oal_model *m, const char *role)
{
    for (uint32_t i = 0; m && i < m->clip_count; i++) if (!strcmp(m->clips[i].role, role)) return (int32_t)i;
    return -1;
}
int32_t hta_oal_bone_find(const hta_oal_model *m, const char *n)
{
    for (uint32_t i = 0; m && i < m->bone_count; i++) if (!strcasecmp(m->bone_name[i], n)) return (int32_t)i;
    return -1;
}
int32_t hta_oal_attachment_find(const hta_oal_model *m, const char *n)
{
    for (uint32_t i = 0; m && i < m->att_count; i++) if (!strcasecmp(m->att[i].name, n)) return (int32_t)i;
    return -1;
}
const hta_oal_sound *hta_oal_sound_find(const hta_oal_asset *a, const char *role)
{
    for (uint32_t i = 0; a && i < a->sound_count; i++) if (!strcmp(a->sounds[i].role, role)) return &a->sounds[i];
    return NULL;
}
float hta_oal_clip_length(const hta_oal_model *m, int32_t c)
{
    if (!m || c < 0 || (uint32_t)c >= m->clip_count) return 0.0f;
    return (float)(m->clips[c].frames > 1 ? m->clips[c].frames - 1 : 1) / m->clips[c].fps;
}

/* ---- matrices ---- */

void hta_oal_mul(const float a[12], const float b[12], float o[12])
{
    float t[12];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) {
            float v = a[r*4+0]*b[0*4+c] + a[r*4+1]*b[1*4+c] + a[r*4+2]*b[2*4+c];
            if (c == 3) v += a[r*4+3];
            t[r*4+c] = v;
        }
    memcpy(o, t, sizeof(t));
}
void hta_oal_invert(const float a[12], float o[12])
{
    float t[12];
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) t[r*4+c] = a[c*4+r];
    for (int r = 0; r < 3; r++) t[r*4+3] = -(t[r*4+0]*a[3] + t[r*4+1]*a[7] + t[r*4+2]*a[11]);
    memcpy(o, t, sizeof(t));
}
void hta_oal_to_mat4(const float a[12], float o[16])
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 3; r++) o[c*4+r] = a[r*4+c];
        o[c*4+3] = c == 3 ? 1.0f : 0.0f;
    }
}
static void key_matrix(const float q[4], const float p[3], float m[12])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float n = sqrtf(x*x + y*y + z*z + w*w);
    if (n > 1e-8f) { x /= n; y /= n; z /= n; w /= n; }
    m[0] = 1 - 2*(y*y + z*z); m[1] = 2*(x*y - w*z);     m[2] = 2*(x*z + w*y);     m[3] = p[0];
    m[4] = 2*(x*y + w*z);     m[5] = 1 - 2*(x*x + z*z); m[6] = 2*(y*z - w*x);     m[7] = p[1];
    m[8] = 2*(x*z - w*y);     m[9] = 2*(y*z + w*x);     m[10] = 1 - 2*(x*x + y*y); m[11] = p[2];
}

void hta_oal_pose(const hta_oal_model *m, int32_t clip, float t, float (*world)[12])
{
    const hta_oal_clip *cl = (clip >= 0 && (uint32_t)clip < m->clip_count) ? &m->clips[clip] : NULL;
    uint32_t f0 = 0, f1 = 0;
    float s = 0.0f;
    if (cl) {
        float fr = t * cl->fps;
        float last = (float)(cl->frames - 1);
        if (cl->loop && cl->frames > 1) { fr = fmodf(fr, last); if (fr < 0) fr += last; }
        else if (fr > last) fr = last;
        if (fr < 0) fr = 0;
        f0 = (uint32_t)fr; f1 = f0 + 1 < cl->frames ? f0 + 1 : f0; s = fr - (float)f0;
    }
    for (uint32_t b = 0; b < m->bone_count; b++) {
        float q[4], p[3];
        if (cl) {
            const hta_oal_key *a = &cl->keys[(size_t)f0 * m->bone_count + b];
            const hta_oal_key *c = &cl->keys[(size_t)f1 * m->bone_count + b];
            float d = a->q[0]*c->q[0] + a->q[1]*c->q[1] + a->q[2]*c->q[2] + a->q[3]*c->q[3];
            float sg = d < 0 ? -1.0f : 1.0f;
            for (int k = 0; k < 4; k++) q[k] = a->q[k] + (sg * c->q[k] - a->q[k]) * s;
            for (int k = 0; k < 3; k++) p[k] = a->p[k] + (c->p[k] - a->p[k]) * s;
        } else {
            memcpy(q, m->bind[b].q, sizeof(q)); memcpy(p, m->bind[b].p, sizeof(p));
        }
        float local[12];
        key_matrix(q, p, local);
        if (m->parent[b] >= 0) hta_oal_mul(world[m->parent[b]], local, world[b]);
        else memcpy(world[b], local, sizeof(local));
    }
}

void hta_oal_skin(const hta_oal_model *m, const float (*world)[12], const float root[12], hta_vertex *out)
{
    static float skin[HTA_OAL_MAX_BONES][12];
    for (uint32_t b = 0; b < m->bone_count; b++) {
        float t[12];
        hta_oal_mul(world[b], m->inv_bind[b], t);
        hta_oal_mul(root, t, skin[b]);
    }
    for (uint32_t i = 0; i < m->mesh.vertex_count; i++) {
        const hta_vertex *v = &m->mesh.vertices[i];
        float mm[12] = { 0 };
        for (int k = 0; k < 3; k++) {
            float w = m->vweight[i][k];
            if (w <= 0.0f) continue;
            const float *s = skin[m->vbone[i][k]];
            for (int e = 0; e < 12; e++) mm[e] += w * s[e];
        }
        hta_vertex *o = &out[i];
        for (int r = 0; r < 3; r++) {
            o->pos[r] = mm[r*4]*v->pos[0] + mm[r*4+1]*v->pos[1] + mm[r*4+2]*v->pos[2] + mm[r*4+3];
            o->normal[r] = mm[r*4]*v->normal[0] + mm[r*4+1]*v->normal[1] + mm[r*4+2]*v->normal[2];
        }
        float nl = sqrtf(o->normal[0]*o->normal[0] + o->normal[1]*o->normal[1] + o->normal[2]*o->normal[2]);
        if (nl > 1e-8f) for (int k = 0; k < 3; k++) o->normal[k] /= nl;
        o->uv[0] = v->uv[0]; o->uv[1] = v->uv[1];
        o->lm_uv[0] = o->lm_uv[1] = 0.0f;
    }
}
