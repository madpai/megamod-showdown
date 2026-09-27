/* Packages: the "package" declaration, library packages and the package
 * graph (package.h, docs/RESOURCES.md). */
#include "package.h"
#include "mjson.h"
#include "world_def.h"
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

const char *hta_package_name(const hta_package *p, char *buf, size_t n)
{
    if (p && p->declared) snprintf(buf, n, "package %s", p->id);
    else if (p && p->content_id[0]) snprintf(buf, n, "%s (a package with no declaration)", p->content_id);
    else snprintf(buf, n, "this package (it has no declaration)");
    return buf;
}

const hta_pkg_rid *hta_package_provides(const hta_package *p, const char *id)
{
    for (uint32_t i = 0; p && id && i < p->provide_count; i++) if (!strcmp(p->provides[i].id, id)) return &p->provides[i];
    return NULL;
}

/* ---- the declaration ------------------------------------------------------ */

#define ENTRY_MAX 256   /* a list entry read before it is checked */

/* A canonical list of resource IDs: each valid, supported, strictly after
 * the one before. `check` refuses a type the list may not hold. */
static bool id_list(hta_mj *r, const char *pkg, const char *what, hta_pkg_rid *out, uint32_t max, uint32_t *count,
                    bool importable_only, char *err, size_t n)
{
    char v[ENTRY_MAX], why[128];
    if (!hta_mj_eat(r, '[')) return failv(err, n, "%s: %s is not a list", pkg, what);
    if (hta_mj_eat(r, ']')) return true;
    do {
        if (!hta_mj_str(r, v, sizeof(v))) return failv(err, n, "%s: %s: an entry is not a string (or over %d bytes)", pkg, what, ENTRY_MAX - 1);
        hta_rid rid;
        int rc = hta_rid_parse(v, &rid, why, sizeof(why));
        if (rc != HTA_RID_OK) return failv(err, n, "%s: %s '%s': %s", pkg, what, v, why);
        const hta_rtype_info *t = hta_rtype_get(rid.type);
        if (hta_rid_reserved_namespace(v, rid.ns_len))
            return failv(err, n, "%s: %s %s: namespace '%.*s' is reserved for built-in content", pkg, what, v, rid.ns_len, v);
        if (t->scope == HTA_RS_PLACEMENT)
            return failv(err, n, "%s: %s %s: %s %s is a placement, never provided or imported", pkg, what, v,
                         strchr("aeiou", t->noun[0]) ? "an" : "a", t->noun);
        if (importable_only && !t->importable)
            return failv(err, n, "%s: %s %s: %s cannot be imported from another package (only importable types: see megamod-resources)",
                         pkg, what, v, t->noun);
        if (*count && strcmp(out[*count - 1].id, v) >= 0)
            return failv(err, n, !strcmp(out[*count - 1].id, v) ? "%s: %s lists %s twice" :
                                 "%s: %s is not in canonical (byte) order at %s", pkg, what, v);
        if (*count >= max) return failv(err, n, "%s: %s has more than %u entries", pkg, what, max);
        memcpy(out[*count].id, v, strlen(v) + 1);
        out[*count].type = rid.type;
        (*count)++;
    } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, ']')) return failv(err, n, "%s: malformed %s", pkg, what);
    return true;
}

static bool parse_require(hta_mj *r, hta_package *p, const char *pkg, char *err, size_t n)
{
    if (p->require_count >= HTA_PKG_MAX_REQUIRES)
        return failv(err, n, "%s: requires more than %u packages", pkg, HTA_PKG_MAX_REQUIRES);
    hta_pkg_require *q = &p->requires[p->require_count];
    memset(q, 0, sizeof(*q));
    q->first = (uint16_t)p->import_count;
    char key[16], v[ENTRY_MAX], why[128];
    bool have_pkg = false, have_res = false;
    if (!hta_mj_eat(r, '{')) return failv(err, n, "%s: a requirement is not an object", pkg);
    if (!hta_mj_eat(r, '}')) do {
        if (!hta_mj_str(r, key, sizeof(key)) || !hta_mj_eat(r, ':')) return failv(err, n, "%s: malformed requirement", pkg);
        if (!strcmp(key, "package")) {
            if (have_pkg || !hta_mj_str(r, v, sizeof(v))) return failv(err, n, "%s: malformed requirement package", pkg);
            if (!hta_package_id_valid(v, why, sizeof(why))) return failv(err, n, "%s: requires '%s': %s", pkg, v, why);
            memcpy(q->package, v, strlen(v) + 1);
            have_pkg = true;
        } else if (!strcmp(key, "resources")) {
            if (have_res) return failv(err, n, "%s: a requirement lists resources twice", pkg);
            char what[HTA_PKG_ID_MAX + 32];
            snprintf(what, sizeof(what), "requires %s resources", have_pkg ? q->package : "(a package)");
            uint32_t before = p->import_count, got = 0;
            if (!id_list(r, pkg, what, p->imports + before, HTA_PKG_MAX_IMPORTS - before, &got, true, err, n))
                return false;
            q->count = (uint16_t)got;
            p->import_count = before + got;
            have_res = true;
        } else return failv(err, n, "%s: unknown requirement field '%s'", pkg, key);
    } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, '}') || !have_pkg || !have_res)
        return failv(err, n, "%s: a requirement needs package and resources", pkg);
    if (p->require_count && strcmp(p->requires[p->require_count - 1].package, q->package) >= 0)
        return failv(err, n, !strcmp(p->requires[p->require_count - 1].package, q->package) ?
                     "%s: requires package %s twice" : "%s: requires is not in canonical (package ID) order at %s", pkg, q->package);
    p->require_count++;
    return true;
}

static bool parse_decl(hta_mj *r, hta_package *p, char *err, size_t n)
{
    char key[16], v[ENTRY_MAX], why[128], pkg[HTA_PKG_ID_MAX + 16] = "package";
    bool have[4] = { 0 };   /* id schema provides requires */
    if (!hta_mj_eat(r, '{')) return failv(err, n, "package: not an object");
    if (!hta_mj_eat(r, '}')) do {
        if (!hta_mj_str(r, key, sizeof(key)) || !hta_mj_eat(r, ':')) return failv(err, n, "%s: malformed", pkg);
        if (!strcmp(key, "id")) {
            if (have[0] || !hta_mj_str(r, v, sizeof(v))) return failv(err, n, "%s: malformed id", pkg);
            if (!hta_package_id_valid(v, why, sizeof(why))) return failv(err, n, "package '%s': %s", v, why);
            memcpy(p->id, v, strlen(v) + 1);
            snprintf(pkg, sizeof(pkg), "package %s", p->id);
            have[0] = true;
        } else if (!strcmp(key, "schema")) {
            double s;
            if (have[1] || !hta_mj_num(r, &s)) return failv(err, n, "%s: malformed schema", pkg);
            if (s != (double)HTA_PKG_SCHEMA)
                return failv(err, n, "%s: unsupported package schema %g (this engine has %u)", pkg, s, HTA_PKG_SCHEMA);
            have[1] = true;
        } else if (!strcmp(key, "provides")) {
            if (have[2]) return failv(err, n, "%s: provides twice", pkg);
            if (!id_list(r, pkg, "provides", p->provides, HTA_PKG_MAX_PROVIDES, &p->provide_count, false, err, n))
                return false;
            have[2] = true;
        } else if (!strcmp(key, "requires")) {
            if (have[3] || !hta_mj_eat(r, '[')) return failv(err, n, "%s: requires is not a list", pkg);
            if (!hta_mj_eat(r, ']')) {
                do { if (!parse_require(r, p, pkg, err, n)) return false; } while (hta_mj_eat(r, ','));
                if (!hta_mj_eat(r, ']')) return failv(err, n, "%s: malformed requires", pkg);
            }
            have[3] = true;
        } else return failv(err, n, "%s: unknown field '%s' (schema %u has id, provides, requires, schema)",
                            pkg, key, HTA_PKG_SCHEMA);
    } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, '}')) return failv(err, n, "%s: malformed", pkg);
    if (!have[0] || !have[1] || !have[2] || !have[3])
        return failv(err, n, "%s: needs id, schema, provides and requires", pkg);
    for (uint32_t i = 0; i < p->require_count; i++)
        if (!strcmp(p->requires[i].package, p->id)) return failv(err, n, "%s: requires itself", pkg);
    return true;
}

bool hta_package_parse(const uint8_t *manifest, size_t len, uint8_t kind, hta_package *out, char *err, size_t n)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    if (err && n) err[0] = 0;
    if (!manifest) return failv(err, n, "no manifest");
    hta_mj r = { manifest, manifest + len, false, true };
    char key[64];
    if (!hta_mj_eat(&r, '{')) return failv(err, n, "manifest is not an object");
    if (hta_mj_eat(&r, '}')) return true;
    do {
        const uint8_t *at = r.p;
        if (!hta_mj_str(&r, key, sizeof(key))) { r.p = at; if (!hta_mj_str(&r, NULL, 0)) return failv(err, n, "malformed manifest"); key[0] = 0; }
        if (!hta_mj_eat(&r, ':')) return failv(err, n, "malformed manifest");
        if (!strcmp(key, "package")) {
            if (out->declared) return failv(err, n, "package appears twice");
            if (!parse_decl(&r, out, err, n)) { uint8_t k = out->kind; memset(out, 0, sizeof(*out)); out->kind = k; return false; }
            out->declared = true;
        } else if (!strcmp(key, "id")) {
            /* The world's own resource ID (N2/X1). Kept only when it is a
             * plain string that fits; checked against provides later. */
            const uint8_t *v = r.p;
            if (!hta_mj_str(&r, out->content_id, sizeof(out->content_id))) {
                out->content_id[0] = 0;
                r.p = v;
                if (!hta_mj_skip(&r)) return failv(err, n, "malformed manifest");
            }
        } else if (!hta_mj_skip(&r)) return failv(err, n, "malformed manifest");
    } while (hta_mj_eat(&r, ','));
    if (!hta_mj_eat(&r, '}')) return failv(err, n, "malformed manifest");
    hta_mj_ws(&r);
    if (r.p != r.end) return failv(err, n, "trailing bytes after the manifest");
    return true;
}

/* ---- libraries ------------------------------------------------------------- */

bool hta_oalasset_manifest(const uint8_t *data, size_t size, const uint8_t **manifest, size_t *len)
{
    if (!data || size < 32 || memcmp(data, "OALA", 4)) return false;
    uint32_t version = (uint32_t)data[4] | (uint32_t)data[5] << 8 | (uint32_t)data[6] << 16 | (uint32_t)data[7] << 24;
    uint32_t ml = (uint32_t)data[8] | (uint32_t)data[9] << 8 | (uint32_t)data[10] << 16 | (uint32_t)data[11] << 24;
    if (version != 1 || ml > 4u * 1024u * 1024u || ml > size - 32) return false;
    *manifest = data + 32;
    *len = ml;
    return true;
}

/* A top-level string member, or "". */
static void top_string(const uint8_t *m, size_t len, const char *want, char *out, size_t cap)
{
    hta_mj r = { m, m + len, false, false };
    char key[64];
    out[0] = 0;
    if (!hta_mj_eat(&r, '{') || hta_mj_eat(&r, '}')) return;
    do {
        const uint8_t *at = r.p;
        if (!hta_mj_str(&r, key, sizeof(key))) { r.p = at; if (!hta_mj_str(&r, NULL, 0)) return; key[0] = 0; }
        if (!hta_mj_eat(&r, ':')) return;
        const uint8_t *v = r.p;
        if (!strcmp(key, want) && hta_mj_str(&r, out, cap)) return;
        out[0] = 0;
        r.p = v;
        if (!hta_mj_skip(&r)) { out[0] = 0; return; }
    } while (hta_mj_eat(&r, ','));
}

/* What a library adds to a world key: FNV-1a 64 over "OALL", the schema,
 * then its played members (package, scripts) as key/value bytes, exactly
 * as stored, in manifest order. */
static const char *const LIBRARY_PLAYED[] = { "package", "scripts" };
const char *hta_library_key_played(uint32_t i)
{
    return i < sizeof(LIBRARY_PLAYED) / sizeof(LIBRARY_PLAYED[0]) ? LIBRARY_PLAYED[i] : NULL;
}
typedef struct { uint64_t h; } fnv;
static void fbytes(fnv *f, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { f->h ^= b[i]; f->h *= 1099511628211ull; }
}
static void fu32(fnv *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fbytes(f, b, 4);
}
static void played(void *ctx, const char *key, const uint8_t *value, size_t len)
{
    for (size_t i = 0; i < sizeof(LIBRARY_PLAYED) / sizeof(LIBRARY_PLAYED[0]); i++)
        if (!strcmp(key, LIBRARY_PLAYED[i])) {
            fnv *f = ctx;
            fu32(f, (uint32_t)strlen(key)); fbytes(f, key, strlen(key));
            fu32(f, (uint32_t)len); fbytes(f, value, len);
        }
}

bool hta_library_load(const uint8_t *data, size_t size, hta_pkg_dep *out, char *err, size_t n)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    const uint8_t *m;
    size_t ml;
    if (!hta_oalasset_manifest(data, size, &m, &ml)) return failv(err, n, "not an OALASSET v1");
    const uint8_t *h = data;
    uint32_t mc = (uint32_t)h[12] | (uint32_t)h[13] << 8 | (uint32_t)h[14] << 16 | (uint32_t)h[15] << 24;
    uint32_t sc = (uint32_t)h[16] | (uint32_t)h[17] << 8 | (uint32_t)h[18] << 16 | (uint32_t)h[19] << 24;
    char kind[24];
    top_string(m, ml, "kind", kind, sizeof(kind));
    if (strcmp(kind, "library")) return failv(err, n, "not a library package (kind '%s')", kind);
    if (mc || sc || size != 32 + ml) return failv(err, n, "a library carries a manifest only");
    if (!hta_package_parse(m, ml, HTA_PKG_LIBRARY, &out->decl, err, n)) return false;
    char name[HTA_PKG_ID_MAX + 16];
    if (!out->decl.declared) return failv(err, n, "a library must declare its package");
    hta_package_name(&out->decl, name, sizeof(name));
    out->scripts = calloc(1, sizeof(*out->scripts));
    if (!out->scripts) return failv(err, n, "out of memory");
    char why[240];
    if (!hta_world_defs_parse_library(m, ml, out->scripts, why, sizeof(why))) {
        free(out->scripts); out->scripts = NULL;
        return failv(err, n, "%s: %s", name, why);
    }
    /* provides == its scripts, exactly. */
    for (uint32_t i = 0; i < out->decl.provide_count; i++) {
        const hta_pkg_rid *p = &out->decl.provides[i];
        if (p->type != HTA_RT_SCRIPT || hta_world_defs_find_script(out->scripts, p->id) < 0) {
            free(out->scripts); out->scripts = NULL;
            return failv(err, n, "%s lists %s in provides, but has no such script", name, p->id);
        }
    }
    for (uint32_t i = 0; i < out->scripts->script_count; i++)
        if (!hta_package_provides(&out->decl, out->scripts->script[i].id)) {
            failv(err, n, "%s has script %s but does not list it in provides", name, out->scripts->script[i].id);
            free(out->scripts); out->scripts = NULL;
            return false;
        }
    fnv f = { 14695981039346656037ull };
    fbytes(&f, "OALL", 4); fu32(&f, HTA_PKG_SCHEMA);
    if (!hta_manifest_members(m, ml, played, &f)) {
        free(out->scripts); out->scripts = NULL;
        return failv(err, n, "%s: malformed manifest", name);
    }
    out->digest = f.h ? f.h : 1u;
    return true;
}

/* ---- the graph --------------------------------------------------------------- */

void hta_pkg_set_free(hta_pkg_set *set)
{
    if (!set) return;
    for (uint32_t i = 0; i < set->dep_count; i++) free(set->dep[i].scripts);
    memset(set, 0, sizeof(*set));
}

static const hta_package *node_decl(const hta_pkg_set *s, uint32_t node)
{
    return node ? &s->dep[node - 1].decl : &s->root;
}

static int32_t node_of(const hta_pkg_set *s, const char *id)
{
    if (s->root.declared && !strcmp(s->root.id, id)) return 0;
    for (uint32_t i = 0; i < s->dep_count; i++) if (!strcmp(s->dep[i].decl.id, id)) return (int32_t)i + 1;
    return -1;
}

/* "a -> b -> c" from the stack's nodes, then `last`. */
static void path(const hta_pkg_set *s, const uint32_t *stack, uint32_t from, uint32_t sp, const char *last,
                 char *out, size_t n)
{
    size_t at = 0;
    out[0] = 0;
    for (uint32_t i = from; i < sp && at < n; i++) {
        const hta_package *d = node_decl(s, stack[i]);
        int w = snprintf(out + at, n - at, "%s -> ", d->declared ? d->id : "(the world)");
        if (w < 0) return;
        at += (size_t)w;
    }
    if (at < n) snprintf(out + at, n - at, "%s", last);
}

/* The message is written first: it may point into the set. */
#define REFUSE(...) do { failv(err, n, __VA_ARGS__); hta_pkg_set_free(set); return false; } while (0)

bool hta_pkg_set_load(hta_pkg_set *set, const hta_package *root, const hta_pkg_source *src, char *err, size_t n)
{
    if (!set || !root) return false;
    memset(set, 0, sizeof(*set));
    set->root = *root;
    if (err && n) err[0] = 0;
    uint32_t stack[HTA_PKG_MAX_DEPTH + 2], next[HTA_PKG_MAX_DEPTH + 2], sp = 0;
    uint8_t gray[HTA_PKG_MAX_SET] = { 0 };
    char name[HTA_PKG_ID_MAX + 64], trail[HTA_PKG_MAX_SET * (HTA_PKG_ID_MAX + 4) + 8];
    stack[sp] = 0; next[sp] = 0; sp++; gray[0] = 1;
    while (sp) {
        uint32_t node = stack[sp - 1];
        const hta_package *d = node_decl(set, node);
        if (next[sp - 1] >= d->require_count) { gray[node] = 0; sp--; continue; }
        const hta_pkg_require *q = &d->requires[next[sp - 1]++];
        hta_package_name(d, name, sizeof(name));
        int32_t found = node_of(set, q->package);
        if (found >= 0) {
            if (gray[found]) {
                uint32_t from = 0;
                while (from < sp && stack[from] != (uint32_t)found) from++;
                path(set, stack, from, sp, q->package, trail, sizeof(trail));
                REFUSE("package cycle: %s (package requirements must not loop)", trail);
            }
            continue;
        }
        if (sp > HTA_PKG_MAX_DEPTH) {
            path(set, stack, 0, sp, q->package, trail, sizeof(trail));
            REFUSE("package requirements deeper than %u: %s", HTA_PKG_MAX_DEPTH, trail);
        }
        if (set->dep_count >= HTA_PKG_MAX_SET - 1) {
            REFUSE("%s: more than %u packages in one set (at %s)", name, HTA_PKG_MAX_SET, q->package);
        }
        const uint8_t *data = NULL;
        size_t size = 0;
        void *handle = NULL;
        char where[HTA_PKG_ID_MAX + 64];
        snprintf(where, sizeof(where), "%s/%s.oalasset", HTA_PKG_LIBRARY_DIR, q->package);
        if (!src || !src->open) {
            REFUSE("%s requires package %s, but no package source was given to load it from", name, q->package);
        }
        if (!src->open(src->ctx, q->package, &data, &size, &handle, where, sizeof(where))) {
            REFUSE("%s requires package %s, but it is not present (looked for %s)", name, q->package, where);
        }
        hta_pkg_dep *dep = &set->dep[set->dep_count];
        char why[300];
        bool ok = hta_library_load(data, size, dep, why, sizeof(why));
        if (src->close) src->close(src->ctx, handle);
        if (!ok) {
            REFUSE("%s requires package %s: %s: %s", name, q->package, where, why);
        }
        if (strcmp(dep->decl.id, q->package)) {
            char got[HTA_PKG_ID_MAX + 1];
            memcpy(got, dep->decl.id, sizeof(got));
            free(dep->scripts);
            REFUSE("%s requires package %s, but %s declares package %s", name, q->package, where, got);
        }
        set->dep_count++;
        uint32_t me = set->dep_count;
        gray[me] = 1;
        stack[sp] = me; next[sp] = 0; sp++;
    }
    /* Every import is provided, by the package it is taken from. */
    for (uint32_t k = 0; k <= set->dep_count; k++) {
        const hta_package *d = node_decl(set, k);
        hta_package_name(d, name, sizeof(name));
        for (uint32_t i = 0; i < d->require_count; i++) {
            const hta_pkg_require *q = &d->requires[i];
            const hta_package *p = node_decl(set, (uint32_t)node_of(set, q->package));
            for (uint32_t j = 0; j < q->count; j++) {
                const hta_pkg_rid *want = &d->imports[q->first + j];
                if (!hta_package_provides(p, want->id)) {
                    REFUSE("%s requires %s from package %s, but package %s does not provide it",
                                 name, want->id, q->package, q->package);
                }
            }
        }
    }
    for (uint32_t i = 0; i < set->root.require_count; i++)
        set->dep[node_of(set, set->root.requires[i].package) - 1].direct = true;
    /* Canonical order: by package ID, whatever order they were found in. */
    for (uint32_t i = 1; i < set->dep_count; i++)
        for (uint32_t j = i; j > 0 && strcmp(set->dep[j - 1].decl.id, set->dep[j].decl.id) > 0; j--) {
            hta_pkg_dep t = set->dep[j]; set->dep[j] = set->dep[j - 1]; set->dep[j - 1] = t;
        }
    return true;
}

bool hta_pkg_set_resources(const hta_pkg_set *set, hta_res_set *rs, hta_res_import *imports, char *err, size_t n)
{
    rs->provider_count = set->dep_count + 1;
    snprintf(rs->provider[0], sizeof(rs->provider[0]), "%s", set->root.declared ? set->root.id : "");
    for (uint32_t k = 0; k < set->dep_count; k++) {
        const hta_pkg_dep *d = &set->dep[k];
        snprintf(rs->provider[k + 1], sizeof(rs->provider[k + 1]), "%s", d->decl.id);
        for (uint32_t i = 0; i < d->decl.provide_count; i++) {
            int32_t at = hta_world_defs_find_script(d->scripts, d->decl.provides[i].id);
            if (at < 0 || !hta_res_add(rs, d->decl.provides[i].id, d->decl.provides[i].type, (uint8_t)(k + 1),
                                       (uint16_t)at, err, n)) return at < 0 ? failv(err, n, "library inconsistent") : false;
        }
    }
    rs->import_count = 0;
    rs->required_mask = 0;
    for (uint32_t i = 0; i < set->root.require_count; i++) {
        const hta_pkg_require *q = &set->root.requires[i];
        int32_t p = node_of(set, q->package);
        if (p <= 0) return failv(err, n, "requirement %s not loaded", q->package);
        rs->required_mask |= 1u << p;
        for (uint32_t j = 0; j < q->count; j++) {
            imports[rs->import_count].id = set->root.imports[q->first + j].id;
            imports[rs->import_count].provider = (uint8_t)p;
            rs->import_count++;
        }
    }
    rs->imports = imports;
    return true;
}
