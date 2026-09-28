/* Package-backed asset resources (asset_res.h, docs/RESOURCES.md). */
#include "asset_res.h"
#include "mjson.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
static bool failv(char *err, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#endif
static bool failv(char *err, size_t n, const char *fmt, ...)
{
    if (err && n) { va_list a; va_start(a, fmt); vsnprintf(err, n, fmt, a); va_end(a); }
    return false;
}

bool hta_asset_type(uint8_t t)
{
    return t == HTA_RT_TEXTURE || t == HTA_RT_MATERIAL || t == HTA_RT_MODEL || t == HTA_RT_SOUND;
}

uint32_t hta_asset_count(const hta_asset_table *t, uint8_t type)
{
    if (!t) return 0;
    switch (type) {
    case HTA_RT_TEXTURE: return t->texture_count;
    case HTA_RT_MATERIAL: return t->material_count;
    case HTA_RT_MODEL: return t->model_count;
    case HTA_RT_SOUND: return t->sound_count;
    default: return 0;
    }
}

static const char *id_at(const hta_asset_table *t, uint8_t type, uint32_t i)
{
    switch (type) {
    case HTA_RT_TEXTURE: return t->texture[i].id;
    case HTA_RT_MATERIAL: return t->material[i].id;
    case HTA_RT_MODEL: return t->model[i].id;
    case HTA_RT_SOUND: return t->sound[i].id;
    default: return "";
    }
}

int32_t hta_asset_find(const hta_asset_table *t, uint8_t type, const char *id)
{
    uint32_t n = hta_asset_count(t, type);
    for (uint32_t i = 0; id && i < n; i++) if (!strcmp(id_at(t, type, i), id)) return (int32_t)i;
    return -1;
}

static void model_free(hta_asset_model *m)
{
    free(m->mesh.vertices); free(m->mesh.indices); free(m->mesh.submeshes);
    free(m->mesh.textures);   /* borrowed pixels: only the array is ours */
    free(m->mesh.tri_material);
    memset(&m->mesh, 0, sizeof(m->mesh));
}

void hta_asset_table_free(hta_asset_table *t)
{
    if (!t) return;
    for (uint32_t i = 0; t->texture && i < t->texture_count; i++) free(t->texture[i].rgba);
    for (uint32_t i = 0; t->model && i < t->model_count; i++) model_free(&t->model[i]);
    for (uint32_t i = 0; t->sound && i < t->sound_count; i++) free(t->sound[i].samples);
    free(t->texture); free(t->material); free(t->model); free(t->sound);
    memset(t, 0, sizeof(*t));
}

/* ---- member paths ---------------------------------------------------------- */

static const char *shown(uint8_t c, char b[8])
{
    if (c >= 0x21 && c < 0x7F) snprintf(b, 8, "'%c'", c);
    else if (c == ' ') snprintf(b, 8, "a space");
    else snprintf(b, 8, "\\x%02x", c);
    return b;
}

bool hta_asset_member_valid(const char *p, char *why, size_t n)
{
    char b[8];
    if (why && n) why[0] = 0;
    if (!p || !p[0]) return failv(why, n, "empty member path");
    size_t len = strnlen(p, HTA_ASSET_MEMBER_MAX + 1);
    if (len > HTA_ASSET_MEMBER_MAX) return failv(why, n, "longer than %u bytes", HTA_ASSET_MEMBER_MAX);
    if (p[0] == '/') return failv(why, n, "starts with '/' (member paths are package-relative)");
    if (p[len - 1] == '/') return failv(why, n, "ends with '/'");
    unsigned segs = 0;
    for (size_t at = 0; at < len; ) {
        const char *slash = memchr(p + at, '/', len - at);
        size_t sl = slash ? (size_t)(slash - (p + at)) : len - at;
        bool last = !slash;
        if (++segs > HTA_ASSET_MEMBER_SEGS) return failv(why, n, "more than %u segments", HTA_ASSET_MEMBER_SEGS);
        if (!sl) return failv(why, n, "has an empty segment");
        if (sl == 2 && p[at] == '.' && p[at + 1] == '.') return failv(why, n, "has '..' (no parent references)");
        if (sl == 1 && p[at] == '.') return failv(why, n, "has '.' as a segment");
        size_t dots = 0, dot_at = 0;
        for (size_t i = 0; i < sl; i++) {
            uint8_t c = (uint8_t)p[at + i];
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') continue;
            if (c == '.') { dots++; dot_at = i; continue; }
            if (c >= 'A' && c <= 'Z') return failv(why, n, "has capital %s (paths are lowercase; nothing is folded)", shown(c, b));
            if (c == '\\') return failv(why, n, "has '\\' (the separator is '/')");
            return failv(why, n, "has %s, not [a-z0-9_./]", shown(c, b));
        }
        if (!last && dots) return failv(why, n, "has '.' in a directory segment (one extension, on the last segment)");
        if (last && (dots != 1 || dot_at == 0 || dot_at + 1 == sl))
            return failv(why, n, "needs exactly one extension on its last segment (name.ext)");
        if (last) break;
        at += sl + 1;
    }
    return true;
}

/* ---- parsing ----------------------------------------------------------------- */

typedef struct {
    char     path[HTA_ASSET_MEMBER_MAX + 1];
    uint32_t size, at;       /* offset in the payload */
    int32_t  used_by;        /* descriptor slot, -1 unused */
} member;

typedef struct {
    member   m[HTA_ASSET_MAX_MEMBERS];
    uint32_t count;
    /* per descriptor: its member path, in declaration order across types */
    char     want[4 * HTA_ASSET_MAX_PER_TYPE][HTA_ASSET_MEMBER_MAX + 1];
    uint8_t  want_type[4 * HTA_ASSET_MAX_PER_TYPE];
    uint16_t want_index[4 * HTA_ASSET_MAX_PER_TYPE];
    uint32_t want_count;
    bool extended_material;
} pending;

#define KEY_MAX 16
#define STR_MAX 256

static bool whole(double d, double lo, double hi) { return d == floor(d) && d >= lo && d <= hi; }

static const char *list_name(uint8_t t)
{
    return t == HTA_RT_TEXTURE ? "textures" : t == HTA_RT_MATERIAL ? "materials" : t == HTA_RT_MODEL ? "models" : "sounds";
}

/* A descriptor's ID: well formed, of the list's type, not in a reserved
 * namespace, after the previous one (canonical order, each once). */
static bool check_id(const char *pkg, uint8_t type, const char *id, const char *prev, char *err, size_t n)
{
    char why[160];
    hta_rid r;
    int rc = hta_rid_parse(id, &r, why, sizeof(why));
    if (rc != HTA_RID_OK) return failv(err, n, "%s: assets.%s: '%s': %s", pkg, list_name(type), id, why);
    if (r.type != type)
        return failv(err, n, "%s: assets.%s: %s is a %s ID, expected a %s", pkg, list_name(type), id,
                     hta_rtype_get(r.type)->noun, hta_rtype_get(type)->noun);
    if (hta_rid_reserved_namespace(id, r.ns_len))
        return failv(err, n, "%s: %s: namespace '%.*s' is reserved for built-in content", pkg, id, r.ns_len, id);
    if (prev && prev[0] && strcmp(prev, id) >= 0)
        return failv(err, n, !strcmp(prev, id) ? "%s: %s is declared twice" : "%s: assets.%s is not in canonical (byte) order at %s",
                     pkg, !strcmp(prev, id) ? id : list_name(type), id);
    return true;
}

static bool want_member(pending *pe, uint8_t type, uint16_t index, const char *path)
{
    if (pe->want_count >= 4 * HTA_ASSET_MAX_PER_TYPE) return false;
    snprintf(pe->want[pe->want_count], sizeof(pe->want[0]), "%s", path);
    pe->want_type[pe->want_count] = type;
    pe->want_index[pe->want_count] = index;
    pe->want_count++;
    return true;
}

static bool parse_members(hta_mj *r, const char *pkg, pending *pe, char *err, size_t n)
{
    if (!hta_mj_eat(r, '[')) return failv(err, n, "%s: assets.members is not a list", pkg);
    if (hta_mj_eat(r, ']')) return true;
    do {
        if (pe->count >= HTA_ASSET_MAX_MEMBERS) return failv(err, n, "%s: more than %u members", pkg, HTA_ASSET_MAX_MEMBERS);
        member *m = &pe->m[pe->count];
        memset(m, 0, sizeof(*m));
        m->used_by = -1;
        char key[KEY_MAX], path[STR_MAX], why[128];
        bool have_path = false, have_size = false;
        if (!hta_mj_eat(r, '{')) return failv(err, n, "%s: a member is not an object", pkg);
        if (!hta_mj_eat(r, '}')) do {
            if (!hta_mj_str(r, key, sizeof(key)) || !hta_mj_eat(r, ':')) return failv(err, n, "%s: malformed member", pkg);
            if (!strcmp(key, "path") && !have_path) {
                if (!hta_mj_str(r, path, sizeof(path))) return failv(err, n, "%s: a member path is not a string (or over %d bytes)", pkg, STR_MAX - 1);
                if (!hta_asset_member_valid(path, why, sizeof(why)))
                    return failv(err, n, "%s: member path '%s': %s", pkg, path, why);
                memcpy(m->path, path, strlen(path) + 1);
                have_path = true;
            } else if (!strcmp(key, "size") && !have_size) {
                double d;
                if (!hta_mj_num(r, &d) || !whole(d, 1, HTA_ASSET_MAX_PAYLOAD))
                    return failv(err, n, "%s: a member size is not a whole number of bytes in 1..%u", pkg, HTA_ASSET_MAX_PAYLOAD);
                m->size = (uint32_t)d;
                have_size = true;
            } else return failv(err, n, "%s: unknown or repeated member field '%s'", pkg, key);
        } while (hta_mj_eat(r, ','));
        if (!hta_mj_eat(r, '}') || !have_path || !have_size) return failv(err, n, "%s: a member needs path and size", pkg);
        if (pe->count && strcmp(pe->m[pe->count - 1].path, m->path) >= 0)
            return failv(err, n, !strcmp(pe->m[pe->count - 1].path, m->path) ? "%s: member %s is listed twice" :
                         "%s: assets.members is not in canonical (byte) order at %s", pkg, m->path);
        pe->count++;
    } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, ']')) return failv(err, n, "%s: malformed assets.members", pkg);
    return true;
}

/* One descriptor object of `type`, into the table (grown by one). */
static bool parse_descriptor(hta_mj *r, const char *pkg, uint8_t type, hta_asset_table *t, pending *pe,
                             char *err, size_t n)
{
    char key[KEY_MAX], id[STR_MAX] = "", mem[STR_MAX] = "", fmt[STR_MAX] = "", tex[STR_MAX] = "", draw[STR_MAX] = "";
    double w = -1, h = -1, rate = -1, ch = -1, frames = -1, emissive = 0, roughness = 0.75;
    char slots[HTA_ASSET_MAX_SLOTS][HTA_RID_MAX + 1];
    uint32_t slot_count = 0;
    bool have_slots = false;
    uint32_t seen = 0;
    static const char *const KEYS[] = { "id", "member", "format", "width", "height", "texture", "draw", "materials",
                                        "rate", "channels", "frames", "emissive", "roughness" };
    if (!hta_mj_eat(r, '{')) return failv(err, n, "%s: an entry of assets.%s is not an object", pkg, list_name(type));
    if (!hta_mj_eat(r, '}')) do {
        if (!hta_mj_str(r, key, sizeof(key)) || !hta_mj_eat(r, ':'))
            return failv(err, n, "%s: malformed entry in assets.%s", pkg, list_name(type));
        uint32_t k = 0;
        while (k < sizeof(KEYS) / sizeof(KEYS[0]) && strcmp(KEYS[k], key)) k++;
        if (k == sizeof(KEYS) / sizeof(KEYS[0]) || (seen >> k) & 1u)
            return failv(err, n, "%s: %s%sunknown or repeated field '%s' in assets.%s", pkg, id, id[0] ? ": " : "", key,
                         list_name(type));
        seen |= 1u << k;
        bool ok = true;
        switch (k) {
        case 0: ok = hta_mj_str(r, id, sizeof(id)); break;
        case 1: ok = hta_mj_str(r, mem, sizeof(mem)); break;
        case 2: ok = hta_mj_str(r, fmt, sizeof(fmt)); break;
        case 3: ok = hta_mj_num(r, &w); break;
        case 4: ok = hta_mj_num(r, &h); break;
        case 5: ok = hta_mj_str(r, tex, sizeof(tex)); break;
        case 6: ok = hta_mj_str(r, draw, sizeof(draw)); break;
        case 7:
            have_slots = true;
            ok = hta_mj_eat(r, '[');
            if (ok && !hta_mj_eat(r, ']')) {
                do {
                    char v[STR_MAX];
                    if (slot_count >= HTA_ASSET_MAX_SLOTS)
                        return failv(err, n, "%s: %s: more than %u material slots", pkg, id[0] ? id : "a model", HTA_ASSET_MAX_SLOTS);
                    if (!hta_mj_str(r, v, sizeof(v)) || strlen(v) > HTA_RID_MAX) { ok = false; break; }
                    memcpy(slots[slot_count++], v, strlen(v) + 1);
                } while (hta_mj_eat(r, ','));
                ok = ok && hta_mj_eat(r, ']');
            }
            break;
        case 8: ok = hta_mj_num(r, &rate); break;
        case 9: ok = hta_mj_num(r, &ch); break;
        case 10: ok = hta_mj_num(r, &frames); break;
        case 11: ok = hta_mj_num(r, &emissive); break;
        default: ok = hta_mj_num(r, &roughness); break;
        }
        if (!ok) return failv(err, n, "%s: %s%smalformed '%s' in assets.%s", pkg, id, id[0] ? ": " : "", key, list_name(type));
    } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, '}')) return failv(err, n, "%s: malformed entry in assets.%s", pkg, list_name(type));
    if (!id[0]) return failv(err, n, "%s: an entry of assets.%s has no id", pkg, list_name(type));
    if (strlen(id) > HTA_RID_MAX) return failv(err, n, "%s: assets.%s: an ID is longer than %u bytes", pkg, list_name(type), HTA_RID_MAX);
    uint32_t count = hta_asset_count(t, type);
    if (count >= HTA_ASSET_MAX_PER_TYPE)
        return failv(err, n, "%s: more than %u %s", pkg, HTA_ASSET_MAX_PER_TYPE, list_name(type));
    if (!check_id(pkg, type, id, count ? id_at(t, type, count - 1) : NULL, err, n)) return false;
    /* Which fields each type has: exactly these. */
    uint32_t need = type == HTA_RT_TEXTURE ? 0x1Fu : type == HTA_RT_MATERIAL ? 0x61u :
                    type == HTA_RT_MODEL ? 0x87u : 0x707u;
    uint32_t allowed = need | (type == HTA_RT_MATERIAL ? (3u << 11) : 0u);
    if ((seen & need) != need || (seen & ~allowed)) {
        for (uint32_t k = 0; k < sizeof(KEYS) / sizeof(KEYS[0]); k++) {
            if (((seen >> k) & 1u) && !((allowed >> k) & 1u))
                return failv(err, n, "%s: %s: a %s has no field '%s'", pkg, id, hta_rtype_get(type)->noun, KEYS[k]);
            if (!((seen >> k) & 1u) && ((need >> k) & 1u))
                return failv(err, n, "%s: %s: a %s needs '%s'", pkg, id, hta_rtype_get(type)->noun, KEYS[k]);
        }
    }
    char why[128];
    if ((need & 2u) && !hta_asset_member_valid(mem, why, sizeof(why)))
        return failv(err, n, "%s: %s: member path '%s': %s", pkg, id, mem, why);
    if (type == HTA_RT_TEXTURE) {
        if (strcmp(fmt, "rgba8")) return failv(err, n, "%s: %s: unsupported texture format '%s' (this engine reads rgba8)", pkg, id, fmt);
        if (!whole(w, 1, HTA_ASSET_TEX_MAX) || !whole(h, 1, HTA_ASSET_TEX_MAX))
            return failv(err, n, "%s: %s: width and height must be whole numbers in 1..%u", pkg, id, HTA_ASSET_TEX_MAX);
        hta_asset_texture *x = realloc(t->texture, (count + 1) * sizeof(*x));
        if (!x) return failv(err, n, "out of memory");
        t->texture = x;
        memset(&x[count], 0, sizeof(x[count]));
        memcpy(x[count].id, id, strlen(id) + 1);
        x[count].width = (uint32_t)w; x[count].height = (uint32_t)h;
        t->texture_count++;
    } else if (type == HTA_RT_MATERIAL) {
        if (!isfinite(emissive) || emissive < 0 || emissive > 4 || !isfinite(roughness) || roughness < 0 || roughness > 1)
            return failv(err, n, "%s: %s: emissive must be 0..4 and roughness 0..1", pkg, id);
        uint8_t d = !strcmp(draw, "opaque") ? HTA_ASSET_DRAW_OPAQUE : !strcmp(draw, "alpha") ? HTA_ASSET_DRAW_ALPHA : 0xFF;
        if (d == 0xFF) return failv(err, n, "%s: %s: unknown draw '%s' (opaque or alpha)", pkg, id, draw);
        if (strlen(tex) > HTA_RID_MAX) return failv(err, n, "%s: %s: texture reference longer than %u bytes", pkg, id, HTA_RID_MAX);
        hta_asset_material *x = realloc(t->material, (count + 1) * sizeof(*x));
        if (!x) return failv(err, n, "out of memory");
        t->material = x;
        memset(&x[count], 0, sizeof(x[count]));
        memcpy(x[count].id, id, strlen(id) + 1);
        memcpy(x[count].texture_ref, tex, strlen(tex) + 1);
        x[count].draw = d;
        x[count].texture = HTA_ASSET_NONE;
        x[count].emissive = (float)emissive;
        x[count].roughness = (float)roughness;
        x[count].extended = (seen & (3u << 11)) != 0;
        pe->extended_material |= x[count].extended;
        t->material_count++;
    } else if (type == HTA_RT_MODEL) {
        if (strcmp(fmt, "mesh1")) return failv(err, n, "%s: %s: unsupported model format '%s' (this engine reads mesh1)", pkg, id, fmt);
        if (!have_slots || !slot_count) return failv(err, n, "%s: %s: a model needs at least one material", pkg, id);
        hta_asset_model *x = realloc(t->model, (count + 1) * sizeof(*x));
        if (!x) return failv(err, n, "out of memory");
        t->model = x;
        memset(&x[count], 0, sizeof(x[count]));
        memcpy(x[count].id, id, strlen(id) + 1);
        x[count].slot_count = slot_count;
        for (uint32_t s = 0; s < slot_count; s++) {
            memcpy(x[count].slot_ref[s], slots[s], sizeof(slots[s]));
            x[count].slot[s] = HTA_ASSET_NONE;
        }
        t->model_count++;
    } else {
        if (strcmp(fmt, "pcm_s16le")) return failv(err, n, "%s: %s: unsupported sound format '%s' (this engine reads pcm_s16le)", pkg, id, fmt);
        if (!whole(rate, 4000, 96000) || !whole(ch, 1, 2) || !whole(frames, 1, HTA_ASSET_SOUND_MAX_FRAMES))
            return failv(err, n, "%s: %s: rate 4000..96000, channels 1..2 and frames 1..%u are whole numbers", pkg, id,
                         HTA_ASSET_SOUND_MAX_FRAMES);
        hta_asset_sound *x = realloc(t->sound, (count + 1) * sizeof(*x));
        if (!x) return failv(err, n, "out of memory");
        t->sound = x;
        memset(&x[count], 0, sizeof(x[count]));
        memcpy(x[count].id, id, strlen(id) + 1);
        x[count].rate = (uint32_t)rate; x[count].channels = (uint32_t)ch; x[count].frames = (uint32_t)frames;
        t->sound_count++;
    }
    if ((need & 2u) && !want_member(pe, type, (uint16_t)count, mem)) return failv(err, n, "%s: too many members", pkg);
    return true;
}

static bool parse_list(hta_mj *r, const char *pkg, uint8_t type, hta_asset_table *t, pending *pe, char *err, size_t n)
{
    if (!hta_mj_eat(r, '[')) return failv(err, n, "%s: assets.%s is not a list", pkg, list_name(type));
    if (hta_mj_eat(r, ']')) return true;
    do { if (!parse_descriptor(r, pkg, type, t, pe, err, n)) return false; } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, ']')) return failv(err, n, "%s: malformed assets.%s", pkg, list_name(type));
    return true;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* mesh1: "MSH1", u32 vertices, indices, groups; the vertex records (the
 * OALMAP's 40 bytes: position, normal, uv, lightmap uv), u32 indices, then
 * groups of u32 first, count, slot. Exactly that many bytes. */
static bool decode_mesh(hta_asset_model *m, const uint8_t *p, uint32_t size, const char *pkg, const char *path,
                        char *err, size_t n)
{
#define BAD(...) do { char w_[160]; snprintf(w_, sizeof(w_), __VA_ARGS__); \
                      return failv(err, n, "%s: %s: member %s: %s", pkg, m->id, path, w_); } while (0)
    if (size < 16 || memcmp(p, HTA_ASSET_MESH_MAGIC, 4)) BAD("not a mesh1 payload");
    uint32_t vc = rd32(p + 4), ic = rd32(p + 8), gc = rd32(p + 12);
    if (!vc || vc > HTA_ASSET_MESH_MAX_VERTS || ic < 3 || ic > HTA_ASSET_MESH_MAX_INDICES || ic % 3 || !gc ||
        gc > HTA_ASSET_MESH_MAX_GROUPS)
        BAD("counts out of range (%u vertices, %u indices, %u groups)", vc, ic, gc);
    uint64_t need = 16ull + (uint64_t)vc * 40u + (uint64_t)ic * 4u + (uint64_t)gc * 12u;
    if (need != size) BAD("is %u bytes, but its counts need %llu", size, (unsigned long long)need);
    hta_bsp_mesh *mesh = &m->mesh;
    mesh->vertices = calloc(vc, sizeof(hta_vertex));
    mesh->indices = malloc((size_t)ic * sizeof(uint32_t));
    mesh->submeshes = calloc(gc, sizeof(hta_submesh));
    if (!mesh->vertices || !mesh->indices || !mesh->submeshes) return failv(err, n, "out of memory");
    mesh->vertex_count = vc; mesh->index_count = ic; mesh->submesh_count = gc;
    const uint8_t *at = p + 16;
    for (int k = 0; k < 3; k++) { mesh->bounds_min[k] = 1e30f; mesh->bounds_max[k] = -1e30f; }
    for (uint32_t i = 0; i < vc; i++, at += 40) {
        float *dst = (float *)&mesh->vertices[i];
        for (int j = 0; j < 10; j++) {
            dst[j] = rdf(at + j * 4);
            if (!isfinite(dst[j]) || fabsf(dst[j]) > 4096.0f) BAD("vertex %u is not finite or beyond 4096", i);
        }
        for (int k = 0; k < 3; k++) {
            if (dst[k] < mesh->bounds_min[k]) mesh->bounds_min[k] = dst[k];
            if (dst[k] > mesh->bounds_max[k]) mesh->bounds_max[k] = dst[k];
        }
    }
    for (uint32_t i = 0; i < ic; i++, at += 4) {
        uint32_t v = rd32(at);
        if (v >= vc) BAD("index %u names vertex %u of %u", i, v, vc);
        mesh->indices[i] = v;
    }
    uint32_t end = 0;
    for (uint32_t i = 0; i < gc; i++, at += 12) {
        uint32_t first = rd32(at), count = rd32(at + 4), slot = rd32(at + 8);
        if (first != end || !count || count % 3 || count > ic - first) BAD("group %u does not follow on from the one before", i);
        if (slot >= m->slot_count) BAD("group %u draws with material slot %u, but the model has %u", i, slot, m->slot_count);
        hta_submesh *s = &mesh->submeshes[i];
        hta_submesh_init(s);
        s->first_index = first; s->index_count = count; s->albedo_tex = slot;
        s->scene_lit = true;
        end = first + count;
    }
    if (end != ic) BAD("its groups cover %u of %u indices", end, ic);
    return true;
#undef BAD
}

static uint64_t fnv(const uint8_t *p, size_t len)
{
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h ? h : 1u;
}

/* Each descriptor's member: present, unshared, the right size, decoded. */
static bool bind_members(hta_asset_table *t, pending *pe, const uint8_t *payload, size_t plen, const char *pkg,
                         char *err, size_t n)
{
    uint64_t at = 0;
    for (uint32_t i = 0; i < pe->count; i++) {
        pe->m[i].at = (uint32_t)at;            /* < HTA_ASSET_MAX_PAYLOAD: fits */
        at += pe->m[i].size;
        if (at > HTA_ASSET_MAX_PAYLOAD)
            return failv(err, n, "%s: its members add up to more than %u bytes", pkg, HTA_ASSET_MAX_PAYLOAD);
    }
    if (at != plen)
        return failv(err, n, "%s: its members add up to %llu bytes, but the package carries %zu after the manifest", pkg,
                     (unsigned long long)at, plen);
    /* Every descriptor's member first: present and unshared... */
    uint32_t mi[4 * HTA_ASSET_MAX_PER_TYPE];
    for (uint32_t w = 0; w < pe->want_count; w++) {
        const char *id = id_at(t, pe->want_type[w], pe->want_index[w]), *path = pe->want[w];
        member *m = NULL;
        for (uint32_t i = 0; i < pe->count && !m; i++) if (!strcmp(pe->m[i].path, path)) { m = &pe->m[i]; mi[w] = i; }
        if (!m) return failv(err, n, "%s declares package member %s, but that member is missing", id, path);
        if (m->used_by >= 0)
            return failv(err, n, "%s: member %s backs both %s and %s (one member, one resource)", pkg, path,
                         id_at(t, pe->want_type[m->used_by], pe->want_index[m->used_by]), id);
        m->used_by = (int32_t)w;
    }
    for (uint32_t i = 0; i < pe->count; i++)
        if (pe->m[i].used_by < 0) return failv(err, n, "%s: member %s is not used by any resource", pkg, pe->m[i].path);
    /* ...then each decoded. */
    for (uint32_t w = 0; w < pe->want_count; w++) {
        uint8_t type = pe->want_type[w];
        uint16_t k = pe->want_index[w];
        const char *id = id_at(t, type, k), *path = pe->want[w];
        const member *m = &pe->m[mi[w]];
        const uint8_t *p = payload + m->at;
        if (type == HTA_RT_TEXTURE) {
            hta_asset_texture *x = &t->texture[k];
            uint64_t need = (uint64_t)x->width * x->height * 4u;
            if (need != m->size)
                return failv(err, n, "%s: %ux%u rgba8 is %llu bytes, but member %s holds %u", id, x->width, x->height,
                             (unsigned long long)need, path, m->size);
            if (!(x->rgba = malloc(m->size))) return failv(err, n, "out of memory");
            memcpy(x->rgba, p, m->size);
        } else if (type == HTA_RT_SOUND) {
            hta_asset_sound *x = &t->sound[k];
            uint64_t need = (uint64_t)x->frames * x->channels * 2u;
            if (need != m->size)
                return failv(err, n, "%s: %u frames of %u-channel pcm_s16le are %llu bytes, but member %s holds %u", id,
                             x->frames, x->channels, (unsigned long long)need, path, m->size);
            if (!(x->samples = malloc(m->size))) return failv(err, n, "out of memory");
            for (uint32_t s = 0; s < m->size / 2; s++) x->samples[s] = (int16_t)(uint16_t)(p[s * 2] | p[s * 2 + 1] << 8);
            x->digest = fnv(p, m->size);
        } else if (type == HTA_RT_MODEL) {
            if (!decode_mesh(&t->model[k], p, m->size, pkg, path, err, n)) return false;
        }
    }
    t->payload_bytes = (uint32_t)plen;
    return true;
}

static bool parse_assets(hta_mj *r, const char *pkg, hta_asset_table *t, pending *pe, char *err, size_t n)
{
    static const char *const KEYS[] = { "materials", "members", "models", "schema", "sounds", "textures" };
    uint32_t seen = 0;
    uint32_t schema = 0;
    char key[KEY_MAX];
    if (!hta_mj_eat(r, '{')) return failv(err, n, "%s: assets is not an object", pkg);
    if (!hta_mj_eat(r, '}')) do {
        if (!hta_mj_str(r, key, sizeof(key)) || !hta_mj_eat(r, ':')) return failv(err, n, "%s: malformed assets", pkg);
        uint32_t k = 0;
        while (k < 6 && strcmp(KEYS[k], key)) k++;
        if (k == 6 || (seen >> k) & 1u)
            return failv(err, n, "%s: unknown or repeated field '%s' in assets (schema %u has materials, members, models, schema, sounds, textures)",
                         pkg, key, HTA_ASSET_SCHEMA);
        seen |= 1u << k;
        bool ok;
        if (k == 3) {
            double s;
            if (!hta_mj_num(r, &s)) return failv(err, n, "%s: malformed assets schema", pkg);
            if (s != 1.0 && s != 2.0)
                return failv(err, n, "%s: unsupported assets schema %g (this engine has %u)", pkg, s, HTA_ASSET_SCHEMA);
            schema = (uint32_t)s;
            ok = true;
        } else if (k == 1) ok = parse_members(r, pkg, pe, err, n);
        else ok = parse_list(r, pkg, k == 0 ? HTA_RT_MATERIAL : k == 2 ? HTA_RT_MODEL : k == 4 ? HTA_RT_SOUND : HTA_RT_TEXTURE,
                             t, pe, err, n);
        if (!ok) return false;
    } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, '}')) return failv(err, n, "%s: malformed assets", pkg);
    if (seen != 0x3Fu) return failv(err, n, "%s: assets needs materials, members, models, schema, sounds and textures", pkg);
    if (pe->extended_material && schema < 2) return failv(err, n, "%s: emissive and roughness need assets schema 2", pkg);
    return true;
}

bool hta_asset_parse(const uint8_t *manifest, size_t len, const uint8_t *payload, size_t plen, const char *pkg,
                     hta_asset_table *out, bool *declared, char *err, size_t n)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (declared) *declared = false;
    if (err && n) err[0] = 0;
    if (!pkg) pkg = "package";
    if (!manifest) return failv(err, n, "no manifest");
    hta_mj r = { manifest, manifest + len, false, false };
    char key[64];
    bool found = false;
    pending *pe = calloc(1, sizeof(*pe));
    if (!pe) return failv(err, n, "out of memory");
    bool ok = false;
    if (!hta_mj_eat(&r, '{')) { failv(err, n, "manifest is not an object"); goto done; }
    if (!hta_mj_eat(&r, '}')) {
        do {
            const uint8_t *at = r.p;
            if (!hta_mj_str(&r, key, sizeof(key))) { r.p = at; if (!hta_mj_str(&r, NULL, 0)) { failv(err, n, "malformed manifest"); goto done; } key[0] = 0; }
            if (!hta_mj_eat(&r, ':')) { failv(err, n, "malformed manifest"); goto done; }
            if (!strcmp(key, "assets")) {
                if (found) { failv(err, n, "%s: assets appears twice", pkg); goto done; }
                found = true;
                if (!parse_assets(&r, pkg, out, pe, err, n)) goto done;
            } else if (!hta_mj_skip(&r)) { failv(err, n, "malformed manifest"); goto done; }
        } while (hta_mj_eat(&r, ','));
        if (!hta_mj_eat(&r, '}')) { failv(err, n, "malformed manifest"); goto done; }
    }
    if (!found) {
        if (plen) { failv(err, n, "%s: %zu bytes follow the manifest, but it declares no assets", pkg, plen); goto done; }
        ok = true;
        goto done;
    }
    if (declared) *declared = true;
    if (!bind_members(out, pe, payload, plen, pkg, err, n)) goto done;
    ok = true;
done:
    free(pe);
    if (!ok) hta_asset_table_free(out);
    return ok;
}

bool hta_asset_model_bind(hta_asset_model *m, const hta_asset_material *materials, uint32_t material_count,
                          const hta_asset_texture *textures, uint32_t texture_count, char *err, size_t n)
{
    free(m->mesh.textures);
    m->mesh.textures = calloc(m->slot_count, sizeof(hta_bsp_texture));
    if (!m->mesh.textures) return failv(err, n, "out of memory");
    m->mesh.texture_count = m->slot_count;
    for (uint32_t s = 0; s < m->slot_count; s++) {
        if (m->slot[s] >= material_count) return failv(err, n, "%s: material slot %u is not linked", m->id, s);
        const hta_asset_material *mat = &materials[m->slot[s]];
        if (mat->texture >= texture_count) return failv(err, n, "%s: material %s is not linked", m->id, mat->id);
        const hta_asset_texture *tx = &textures[mat->texture];
        m->mesh.textures[s].width = tx->width;
        m->mesh.textures[s].height = tx->height;
        m->mesh.textures[s].rgba = tx->rgba;       /* borrowed */
        m->mesh.textures[s].tint = 0xFFFFFFu;
    }
    for (uint32_t i = 0; i < m->mesh.submesh_count; i++) {
        hta_submesh *sm = &m->mesh.submeshes[i];
        sm->draw_mode = materials[m->slot[sm->albedo_tex]].draw == HTA_ASSET_DRAW_ALPHA ? HTA_DRAW_ALPHA : HTA_DRAW_OPAQUE;
        const hta_asset_material *mat = &materials[m->slot[sm->albedo_tex]];
        sm->visual_material = mat->extended;
        sm->emissive = mat->emissive;
        sm->roughness = mat->roughness;
    }
    return true;
}
