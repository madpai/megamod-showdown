/* World entity definitions (world_def.h): a bounded parser for the
 * manifest's "world_entities" section and the checks Open Asset Lab also
 * makes. The rest of the manifest is skipped value by value, strings and
 * nesting respected, so a key that merely appears inside another value is
 * never mistaken for the section. */
#include "world_def.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const KIND[HTA_WDEF_KIND_COUNT] = { "", "interactable", "relay", "mover", "trigger", "teleport" };
static const char *const EVENT[HTA_WEV_COUNT] = { "", "used", "fired", "entered" };
static const char *const INPUT[HTA_WIN_COUNT] = { "", "activate", "open", "close", "toggle", "teleport" };

const char *hta_wdef_kind_name(uint8_t k) { return k < HTA_WDEF_KIND_COUNT ? KIND[k] : "?"; }
const char *hta_wdef_event_name(uint8_t e) { return e < HTA_WEV_COUNT ? EVENT[e] : "?"; }
const char *hta_wdef_input_name(uint8_t i) { return i < HTA_WIN_COUNT ? INPUT[i] : "?"; }
uint8_t hta_wdef_input_from_name(const char *name)
{
    for (int i = 1; name && i < HTA_WIN_COUNT; i++) if (!strcmp(INPUT[i], name)) return (uint8_t)i;
    return HTA_WIN_NONE;
}

bool hta_wdef_emits(uint8_t kind, uint8_t event)
{
    return (kind == HTA_WDEF_INTERACTABLE && event == HTA_WEV_USED) ||
           (kind == HTA_WDEF_RELAY && event == HTA_WEV_FIRED) ||
           (kind == HTA_WDEF_TRIGGER && event == HTA_WEV_ENTERED);
}

bool hta_wdef_accepts(uint8_t kind, uint8_t input)
{
    return (kind == HTA_WDEF_RELAY && input == HTA_WIN_ACTIVATE) ||
           (kind == HTA_WDEF_MOVER && (input == HTA_WIN_OPEN || input == HTA_WIN_CLOSE || input == HTA_WIN_TOGGLE)) ||
           (kind == HTA_WDEF_TELEPORT && input == HTA_WIN_TELEPORT);
}

static bool fail(char *err, size_t n, const char *fmt, const char *a, const char *b)
{
    if (err && n) snprintf(err, n, fmt, a ? a : "", b ? b : "");
    return false;
}

static bool segment(const char *s, size_t len, size_t max)
{
    if (!len || len > max || s[0] < 'a' || s[0] > 'z') return false;
    for (size_t i = 1; i < len; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) return false;
    return true;
}

static bool failv(char *err, size_t n, const char *fmt, ...)
{
    if (err && n) { va_list a; va_start(a, fmt); vsnprintf(err, n, fmt, a); va_end(a); }
    return false;
}

/* namespace:<type>/name, lowercase ASCII segments. */
static bool content_id(const char *id, const char *type)
{
    const char *colon = strchr(id, ':'), *slash = colon ? strchr(colon, '/') : NULL;
    size_t len = strlen(id), tl = strlen(type);
    if (!colon || !slash || len > HTA_WDEF_ID_MAX || strchr(colon + 1, ':') || strchr(slash + 1, '/')) return false;
    return segment(id, (size_t)(colon - id), 40) &&
           (size_t)(slash - colon - 1) == tl && !memcmp(colon + 1, type, tl) &&
           segment(slash + 1, strlen(slash + 1), 48);
}

static bool placed_id(const char *id) { return content_id(id, "entity"); }
static bool mover_id(const char *id) { return content_id(id, "mover"); }
static bool script_id(const char *id) { return content_id(id, "script"); }

static const char *const CALLBACK[] = { "on_used", "on_ability" };   /* bit k: HTA_WCB 1 << k */

const char *hta_wscript_callback_name(uint32_t cb)
{
    for (uint32_t k = 0; k < sizeof(CALLBACK) / sizeof(CALLBACK[0]); k++) if (cb == (1u << k)) return CALLBACK[k];
    return "?";
}

static bool same_namespace(const char *a, const char *b)
{
    const char *ca = strchr(a, ':'), *cb = strchr(b, ':');
    return ca && cb && ca - a == cb - b && !memcmp(a, b, (size_t)(ca - a));
}

/* ---- a bounded JSON reader -------------------------------------------- */

typedef struct {
    const uint8_t *p, *end;
    bool bad;
} rd;

#define MAX_DEPTH 64

static void ws(rd *r) { while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r')) r->p++; }
static bool eat(rd *r, char c) { ws(r); if (r->p < r->end && *r->p == c) { r->p++; return true; } return false; }
static bool peek(rd *r, char c) { ws(r); return r->p < r->end && *r->p == c; }

/* A string into `out` (NUL-terminated, at most cap-1 bytes; longer is an
 * error), or skipped when out is NULL. Escapes are accepted and, when
 * kept, must decode to printable ASCII. */
static bool str(rd *r, char *out, size_t cap)
{
    size_t n = 0;
    if (!eat(r, '"')) return false;
    while (r->p < r->end) {
        uint8_t c = *r->p++;
        if (c == '"') { if (out) out[n] = 0; return true; }
        if (c < 0x20) return false;
        if (c == '\\') {
            if (r->p >= r->end) return false;
            uint8_t e = *r->p++;
            if (e == 'u') {
                unsigned v = 0;
                for (int k = 0; k < 4; k++) {
                    if (r->p >= r->end) return false;
                    uint8_t h = *r->p++;
                    v = v * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 :
                                  h >= 'A' && h <= 'F' ? h - 'A' + 10 : 99);
                    if (v > 0xFFFF) return false;
                }
                c = v >= 0x20 && v < 0x7F ? (uint8_t)v : 0;
            } else {
                const char *m = strchr("\"\\/bfnrt", e);
                if (!m || !e) return false;
                c = (uint8_t)"\"\\/\b\f\n\r\t"[m - "\"\\/bfnrt"];
            }
            if (out && (c < 0x20 || c >= 0x7F)) return false;
        } else if (out && c >= 0x7F) return false;
        if (out) { if (n + 1 >= cap) return false; out[n++] = (char)c; }
    }
    return false;
}

static bool num(rd *r, double *out)
{
    ws(r);
    char buf[40]; size_t n = 0;
    while (r->p < r->end && n + 1 < sizeof(buf) && strchr("+-.0123456789eE", *r->p) && *r->p) buf[n++] = (char)*r->p++;
    if (!n || (r->p < r->end && strchr("+-.0123456789eE", *r->p) && *r->p)) return false;
    buf[n] = 0;
    char *e;
    double v = strtod(buf, &e);
    if (*e || !isfinite(v)) return false;
    *out = v;
    return true;
}

/* Any value, skipped. Iterative over nesting, bounded depth. */
static bool skip(rd *r)
{
    int depth = 0;
    do {
        ws(r);
        if (r->p >= r->end) return false;
        uint8_t c = *r->p;
        if (c == '"') { if (!str(r, NULL, 0)) return false; }
        else if (c == '{' || c == '[') {
            if (++depth > MAX_DEPTH) return false;
            r->p++;
            if (eat(r, c == '{' ? '}' : ']')) { depth--; goto next; }
            if (c == '{') { if (!str(r, NULL, 0) || !eat(r, ':')) return false; }
            continue;
        } else if (c == '-' || (c >= '0' && c <= '9')) { double d; if (!num(r, &d)) return false; }
        else if ((size_t)(r->end - r->p) >= 4 && !memcmp(r->p, "true", 4)) r->p += 4;
        else if ((size_t)(r->end - r->p) >= 5 && !memcmp(r->p, "false", 5)) r->p += 5;
        else if ((size_t)(r->end - r->p) >= 4 && !memcmp(r->p, "null", 4)) r->p += 4;
        else return false;
next:
        /* After a value: a comma continues the container, a close ends it. */
        while (depth > 0) {
            ws(r);
            if (r->p >= r->end) return false;
            if (*r->p == ',') {
                r->p++;
                /* In an object the next member starts with its key. The
                 * container kind is not tracked; a key is a string then ':'. */
                const uint8_t *save = r->p;
                if (peek(r, '"') && str(r, NULL, 0) && eat(r, ':')) break;
                r->p = save;
                break;
            }
            if (*r->p == '}' || *r->p == ']') { r->p++; depth--; continue; }
            return false;
        }
    } while (depth > 0);
    return true;
}

static bool vec3(rd *r, float out[3])
{
    if (!eat(r, '[')) return false;
    for (int k = 0; k < 3; k++) {
        double d;
        if ((k && !eat(r, ',')) || !num(r, &d) || fabs(d) > HTA_WDEF_WORLD_LIMIT) return false;
        out[k] = (float)d;
    }
    return eat(r, ']');
}

static bool fnum(rd *r, float *out)
{
    double d;
    if (!num(r, &d) || fabs(d) > 1e6) return false;
    *out = (float)d;
    return true;
}

static int lookup(const char *const *names, int n, const char *s)
{
    for (int i = 1; i < n; i++) if (!strcmp(names[i], s)) return i;
    return 0;
}

/* What is read before it can be checked or resolved: per-link target
 * IDs, and per entity its definition reference or (schema 1) its inline
 * mover parameters. Keys arrive sorted ("entities" before "schema"), so
 * nothing is interpreted until the whole section has been read. */
typedef struct {
    char  target[HTA_WDEF_MAX_LINKS][HTA_WDEF_ID_MAX + 1];
    char  def[HTA_WDEF_MAX_ENTITIES][HTA_WDEF_ID_MAX + 1];
    float move[HTA_WDEF_MAX_ENTITIES][3], speed[HTA_WDEF_MAX_ENTITIES];
    uint8_t has_def[HTA_WDEF_MAX_ENTITIES], has_bounds[HTA_WDEF_MAX_ENTITIES],
            has_move[HTA_WDEF_MAX_ENTITIES], has_speed[HTA_WDEF_MAX_ENTITIES],
            has_pos[HTA_WDEF_MAX_ENTITIES], has_script[HTA_WDEF_MAX_ENTITIES];
    char  script[HTA_WDEF_MAX_ENTITIES][HTA_WDEF_ID_MAX + 1];
    char  ability[HTA_WDEF_ID_MAX + 1];
    char  api[HTA_WDEF_MAX_SCRIPTS][24];
    bool  has_ability;
} pending;

static bool parse_link(rd *r, hta_wdef_link *l, char *target, const char *owner, char *err, size_t n)
{
    char key[16], val[HTA_WDEF_ID_MAX + 1];
    bool have[3] = { 0 };
    if (!eat(r, '{')) return fail(err, n, "%s: malformed link", owner, NULL);
    if (!eat(r, '}')) do {
        if (!str(r, key, sizeof(key)) || !eat(r, ':') || !str(r, val, sizeof(val)))
            return fail(err, n, "%s: malformed link", owner, NULL);
        if (!strcmp(key, "event")) {
            l->event = (uint8_t)lookup(EVENT, HTA_WEV_COUNT, val); have[0] = true;
            if (!l->event) return fail(err, n, "%s: unknown event '%s'", owner, val);
        } else if (!strcmp(key, "input")) {
            l->input = (uint8_t)lookup(INPUT, HTA_WIN_COUNT, val); have[1] = true;
            if (!l->input) return fail(err, n, "%s: unknown input '%s'", owner, val);
        } else if (!strcmp(key, "target")) {
            memcpy(target, val, sizeof(val)); have[2] = true;
        } else return fail(err, n, "%s: unknown link field '%s'", owner, key);
    } while (eat(r, ','));
    if (!eat(r, '}') || !have[0] || !have[1] || !have[2]) return fail(err, n, "%s: incomplete link", owner, NULL);
    return true;
}

static bool parse_entity(rd *r, hta_world_defs *d, pending *pend, char *err, size_t n)
{
    if (d->count >= HTA_WDEF_MAX_ENTITIES) return fail(err, n, "more than 64 world entities%s%s", NULL, NULL);
    hta_wdef *e = &d->entity[d->count];
    memset(e, 0, sizeof(*e));
    e->first_link = (uint16_t)d->link_count;
    e->def = HTA_WDEF_NO_DEF;
    uint32_t me = d->count;
    char key[16], kind[16] = "";
    char where[HTA_WDEF_ID_MAX + 16];
    snprintf(where, sizeof(where), "world entity %u", d->count);
    bool have_id = false, have_bounds = false;
    if (!eat(r, '{')) return fail(err, n, "%s: not an object", where, NULL);
    if (!eat(r, '}')) do {
        if (!str(r, key, sizeof(key)) || !eat(r, ':')) return fail(err, n, "%s: malformed field", where, NULL);
        bool ok = true;
        if (!strcmp(key, "id")) {
            ok = str(r, e->id, sizeof(e->id)); have_id = ok;
            if (ok) snprintf(where, sizeof(where), "%s", e->id);
        } else if (!strcmp(key, "kind")) {
            ok = str(r, kind, sizeof(kind));
            e->kind = (uint8_t)lookup(KIND, HTA_WDEF_KIND_COUNT, kind);
            if (ok && !e->kind) return fail(err, n, "%s: unknown kind '%s'", where, kind);
        } else if (!strcmp(key, "position")) { ok = vec3(r, e->pos); pend->has_pos[me] = 1; }
        else if (!strcmp(key, "reach")) ok = fnum(r, &e->reach);
        else if (!strcmp(key, "yaw_degrees")) { ok = fnum(r, &e->yaw); e->yaw *= 3.14159265358979f / 180.0f; }
        else if (!strcmp(key, "move")) { ok = vec3(r, pend->move[me]); pend->has_move[me] = 1; }
        else if (!strcmp(key, "speed")) { ok = fnum(r, &pend->speed[me]); pend->has_speed[me] = 1; }
        else if (!strcmp(key, "definition")) { ok = str(r, pend->def[me], sizeof(pend->def[me])); pend->has_def[me] = 1; }
        else if (!strcmp(key, "script")) { ok = str(r, pend->script[me], sizeof(pend->script[me])); pend->has_script[me] = 1; }
        else if (!strcmp(key, "bounds")) {
            char k2[8]; bool mn = false, mx = false;
            ok = eat(r, '{');
            while (ok && !peek(r, '}')) {
                ok = str(r, k2, sizeof(k2)) && eat(r, ':');
                if (ok && !strcmp(k2, "min")) { ok = vec3(r, e->min); mn = true; }
                else if (ok && !strcmp(k2, "max")) { ok = vec3(r, e->max); mx = true; }
                else ok = false;
                if (ok && !eat(r, ',')) break;
            }
            ok = ok && eat(r, '}') && mn && mx;
            have_bounds = ok;
        } else if (!strcmp(key, "links")) {
            ok = eat(r, '[');
            if (ok && !eat(r, ']')) {
                do {
                    if (e->link_count >= HTA_WDEF_MAX_LINKS_PER)
                        return fail(err, n, "%s: more than 8 links", where, NULL);
                    if (d->link_count >= HTA_WDEF_MAX_LINKS)
                        return fail(err, n, "%s: more than 256 links in the world", where, NULL);
                    hta_wdef_link *l = &d->link[d->link_count];
                    memset(l, 0, sizeof(*l));
                    if (!parse_link(r, l, pend->target[d->link_count], where, err, n)) return false;
                    d->link_count++; e->link_count++;
                } while (eat(r, ','));
                ok = eat(r, ']');
            }
        } else return fail(err, n, "%s: unknown field '%s'", where, key);
        if (!ok) return fail(err, n, "%s: malformed '%s'", where, key);
    } while (eat(r, ','));
    if (!eat(r, '}')) return fail(err, n, "%s: malformed entity", where, NULL);
    if (!have_id) return fail(err, n, "%s: has no id", where, NULL);
    if (!e->kind) return fail(err, n, "%s: has no kind", where, NULL);
    if (e->kind == HTA_WDEF_TRIGGER && !have_bounds)
        return fail(err, n, "%s: needs bounds", where, NULL);
    pend->has_bounds[me] = have_bounds;
    d->count++;
    return true;
}

/* One entry of "mover_definitions" (schema 2). */
static bool parse_mover_def(rd *r, hta_world_defs *d, char *err, size_t n)
{
    if (d->mover_def_count >= HTA_WDEF_MAX_MOVER_DEFS)
        return fail(err, n, "more than 64 mover definitions%s%s", NULL, NULL);
    hta_wmover_def *m = &d->mover_def[d->mover_def_count];
    memset(m, 0, sizeof(*m));
    char key[16], where[HTA_WDEF_ID_MAX + 32];
    snprintf(where, sizeof(where), "mover definition %u", d->mover_def_count);
    bool have[4] = { 0 };
    if (!eat(r, '{')) return fail(err, n, "%s: not an object", where, NULL);
    if (!eat(r, '}')) do {
        if (!str(r, key, sizeof(key)) || !eat(r, ':')) return fail(err, n, "%s: malformed field", where, NULL);
        bool ok;
        if (!strcmp(key, "id")) {
            ok = str(r, m->id, sizeof(m->id)); have[0] = ok;
            if (ok) snprintf(where, sizeof(where), "%s", m->id);
        } else if (!strcmp(key, "size")) { ok = vec3(r, m->size); have[1] = ok; }
        else if (!strcmp(key, "move")) { ok = vec3(r, m->move); have[2] = ok; }
        else if (!strcmp(key, "speed")) { ok = fnum(r, &m->speed); have[3] = ok; }
        else return fail(err, n, "%s: unknown field '%s'", where, key);
        if (!ok) return fail(err, n, "%s: malformed '%s'", where, key);
    } while (eat(r, ','));
    if (!eat(r, '}')) return fail(err, n, "%s: malformed definition", where, NULL);
    if (!have[0]) return fail(err, n, "%s: has no id", where, NULL);
    if (!have[1] || !have[2] || !have[3]) return fail(err, n, "%s: needs size, move and speed", where, NULL);
    d->mover_def_count++;
    return true;
}

/* A script's source: a JSON string decoded into the pool. Printable ASCII,
 * tab, CR and LF only (a script is ASCII text); bounded per script and in
 * all. */
#define SCRIPT_MAX_BYTES (32u * 1024u)
static bool source(rd *r, hta_world_defs *d, hta_wscript_def *sc)
{
    if (!eat(r, '"')) return false;
    sc->at = d->pool_used; sc->len = 0;
    while (r->p < r->end) {
        uint8_t c = *r->p++;
        if (c == '"') return true;
        if (c < 0x20 || c >= 0x7F) return false;
        if (c == '\\') {
            if (r->p >= r->end) return false;
            uint8_t e = *r->p++;
            if (e == 'u') {
                unsigned v = 0;
                for (int k = 0; k < 4; k++) {
                    if (r->p >= r->end) return false;
                    uint8_t h = *r->p++;
                    int x = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                    if (x < 0) return false;
                    v = v * 16 + (unsigned)x;
                }
                c = (uint8_t)v;
                if (v >= 0x7F || (v < 0x20 && v != '\t' && v != '\n' && v != '\r')) return false;
            } else {
                const char *m = strchr("\"\\/bfnrt", e);
                if (!m || !e || e == 'b' || e == 'f') return false;
                c = (uint8_t)"\"\\/\b\f\n\r\t"[m - "\"\\/bfnrt"];
            }
        }
        if (sc->len >= SCRIPT_MAX_BYTES || d->pool_used >= HTA_WDEF_SCRIPT_POOL) return false;
        d->pool[d->pool_used++] = (char)c;
        sc->len++;
    }
    return false;
}

/* One entry of "scripts" (schema 3). */
static bool parse_script(rd *r, hta_world_defs *d, pending *pend, char *err, size_t n)
{
    if (d->script_count >= HTA_WDEF_MAX_SCRIPTS) return fail(err, n, "more than 16 scripts%s%s", NULL, NULL);
    uint32_t me = d->script_count;
    hta_wscript_def *sc = &d->script[me];
    memset(sc, 0, sizeof(*sc));
    char key[16], where[HTA_WDEF_ID_MAX + 32];
    snprintf(where, sizeof(where), "script %u", me);
    bool have[4] = { 0 };
    if (!eat(r, '{')) return fail(err, n, "%s: not an object", where, NULL);
    if (!eat(r, '}')) do {
        if (!str(r, key, sizeof(key)) || !eat(r, ':')) return fail(err, n, "%s: malformed field", where, NULL);
        bool ok = true;
        if (!strcmp(key, "id")) {
            ok = str(r, sc->id, sizeof(sc->id)); have[0] = ok;
            if (ok) snprintf(where, sizeof(where), "%s", sc->id);
        } else if (!strcmp(key, "api")) { ok = str(r, pend->api[me], sizeof(pend->api[me])); have[1] = ok; }
        else if (!strcmp(key, "callbacks")) {
            ok = eat(r, '[');
            if (ok && !eat(r, ']')) {
                do {
                    char cb[24];
                    if (!str(r, cb, sizeof(cb))) return fail(err, n, "%s: malformed callbacks", where, NULL);
                    uint32_t k = 0;
                    while (k < sizeof(CALLBACK) / sizeof(CALLBACK[0]) && strcmp(CALLBACK[k], cb)) k++;
                    if (k == sizeof(CALLBACK) / sizeof(CALLBACK[0]))
                        return failv(err, n, "%s: unknown callback '%s' (megamod.v1 has on_used, on_ability)", where, cb);
                    sc->callbacks |= 1u << k;
                } while (eat(r, ','));
                ok = eat(r, ']');
            }
            have[2] = ok;
        } else if (!strcmp(key, "source")) {
            ok = source(r, d, sc); have[3] = ok;
            if (!ok) return failv(err, n, "%s: source is not ASCII text, or over 32 KB (64 KB for all scripts)", where);
        } else return fail(err, n, "%s: unknown field '%s'", where, key);
        if (!ok) return fail(err, n, "%s: malformed '%s'", where, key);
    } while (eat(r, ','));
    if (!eat(r, '}')) return fail(err, n, "%s: malformed script", where, NULL);
    if (!have[0]) return fail(err, n, "%s: has no id", where, NULL);
    if (!have[1] || !have[2] || !have[3]) return fail(err, n, "%s: needs api, callbacks and source", where, NULL);
    d->script_count++;
    return true;
}

/* A reference to a script, resolved once: its index, or a refusal that
 * names the referrer, the reference and why. */
static bool script_ref(const hta_world_defs *d, const char *who, const char *ref, uint32_t cb,
                       uint16_t *out, char *err, size_t n)
{
    int32_t k = hta_world_defs_find_script(d, ref);
    if (k < 0) {
        if (hta_world_defs_find(d, ref) >= 0 || hta_world_defs_find_mover(d, ref) >= 0)
            return failv(err, n, "%s: script %s is not a script, expected namespace:script/name", who, ref);
        if (!script_id(ref))
            return failv(err, n, "%s: '%s' is not a script ID (namespace:script/name)", who, ref);
        return failv(err, n, "%s references missing script %s", who, ref);
    }
    if (!(d->script[k].callbacks & cb))
        return failv(err, n, "%s: script %s does not declare %s", who, ref, hta_wscript_callback_name(cb));
    *out = (uint16_t)(k + 1);
    return true;
}

static bool resolve_scripts(hta_world_defs *d, const pending *pend, char *err, size_t n)
{
    bool any = d->script_count || pend->has_ability;
    for (uint32_t i = 0; i < d->count; i++) any = any || pend->has_script[i];
    if (!any) return true;
    if (d->schema < 3) return fail(err, n, "world_entities: scripts need schema 3%s%s", NULL, NULL);
    for (uint32_t i = 0; i < d->script_count; i++) {
        const hta_wscript_def *sc = &d->script[i];
        if (!script_id(sc->id))
            return fail(err, n, "'%s': malformed script ID (namespace:script/name)%s", sc->id, NULL);
        for (uint32_t j = 0; j < i; j++)
            if (!strcmp(d->script[j].id, sc->id)) return fail(err, n, "%s: duplicate script ID%s", sc->id, NULL);
        if (strcmp(pend->api[i], HTA_WDEF_SCRIPT_API))
            return failv(err, n, "%s: unsupported script API '%s' (this engine has %s)", sc->id, pend->api[i], HTA_WDEF_SCRIPT_API);
        if (!sc->callbacks) return fail(err, n, "%s: declares no callbacks%s", sc->id, NULL);
        if (d->count && !same_namespace(sc->id, d->entity[0].id))
            return fail(err, n, "%s: not in the world's namespace (%s)", sc->id, d->entity[0].id);
    }
    for (uint32_t i = 0; i < d->count; i++) {
        if (!pend->has_script[i]) continue;
        hta_wdef *e = &d->entity[i];
        if (e->kind != HTA_WDEF_INTERACTABLE)
            return failv(err, n, "%s: only an interactable takes a script (it is a %s)", e->id, hta_wdef_kind_name(e->kind));
        if (!script_ref(d, e->id, pend->script[i], HTA_WCB_ON_USED, &e->script, err, n)) return false;
    }
    if (pend->has_ability && !script_ref(d, "ability_script", pend->ability, HTA_WCB_ON_ABILITY, &d->ability_script, err, n))
        return false;
    return true;
}

/* Movers get their definitions: by reference (schema 2) or, for an X1
 * package, an unnamed one each from their inline parameters. */
static bool resolve_movers(hta_world_defs *d, const pending *pend, char *err, size_t n)
{
    for (uint32_t i = 0; i < d->count; i++) {
        hta_wdef *e = &d->entity[i];
        bool inline_params = pend->has_bounds[i] || pend->has_move[i] || pend->has_speed[i];
        if (e->kind != HTA_WDEF_MOVER) {
            if (pend->has_def[i])
                return failv(err, n, "%s: only a mover takes a definition (it is a %s)", e->id, hta_wdef_kind_name(e->kind));
            if (pend->has_move[i] || pend->has_speed[i])
                return failv(err, n, "%s: only a mover takes move and speed (it is a %s)", e->id, hta_wdef_kind_name(e->kind));
            continue;
        }
        if (d->schema == 1) {
            if (pend->has_def[i])
                return failv(err, n, "%s: mover definitions need world_entities schema 2", e->id);
            if (!pend->has_bounds[i] || !pend->has_move[i] || !pend->has_speed[i])
                return failv(err, n, "%s: a schema 1 mover needs bounds, move and speed", e->id);
            if (d->mover_def_count >= HTA_WDEF_MAX_MOVER_DEFS)
                return failv(err, n, "%s: too many movers", e->id);
            hta_wmover_def *m = &d->mover_def[d->mover_def_count];
            memset(m, 0, sizeof(*m));
            for (int k = 0; k < 3; k++) {
                m->size[k] = e->max[k] - e->min[k];
                m->move[k] = pend->move[i][k];
                e->pos[k] = (e->min[k] + e->max[k]) * 0.5f;
            }
            m->speed = pend->speed[i];
            memset(e->min, 0, sizeof(e->min)); memset(e->max, 0, sizeof(e->max));
            e->def = (uint16_t)d->mover_def_count++;
            continue;
        }
        if (inline_params)
            return failv(err, n, "%s: a mover takes its size, move and speed from its definition (schema 2)", e->id);
        if (!pend->has_def[i]) return failv(err, n, "%s: mover has no definition", e->id);
        if (!pend->has_pos[i]) return failv(err, n, "%s: mover has no position", e->id);
        const char *ref = pend->def[i];
        int32_t k = hta_world_defs_find_mover(d, ref);
        if (k < 0) {
            if (hta_world_defs_find(d, ref) >= 0)
                return failv(err, n, "%s: definition %s is a placed entity, expected a mover definition", e->id, ref);
            if (!mover_id(ref))
                return failv(err, n, "%s: definition '%s' is not a mover definition ID (namespace:mover/name)", e->id, ref);
            return failv(err, n, "%s references missing mover definition %s", e->id, ref);
        }
        e->def = (uint16_t)k;
    }
    return true;
}

static bool parse_section(rd *r, hta_world_defs *d, char *err, size_t n)
{
    static pending pend;        /* 33 KB: not on a phone's stack */
    memset(&pend, 0, sizeof(pend));
    char key[24];
    bool have_schema = false, have_entities = false, have_defs = false;
    if (!eat(r, '{')) return fail(err, n, "world_entities: not an object%s%s", NULL, NULL);
    if (!eat(r, '}')) do {
        if (!str(r, key, sizeof(key)) || !eat(r, ':')) return fail(err, n, "world_entities: malformed%s%s", NULL, NULL);
        if (!strcmp(key, "schema")) {
            double v;
            if (!num(r, &v) || (v != 1.0 && v != 2.0 && v != 3.0))
                return fail(err, n, "world_entities: unsupported schema%s%s", NULL, NULL);
            d->schema = (uint32_t)v;
            have_schema = true;
        } else if (!strcmp(key, "entities")) {
            if (!eat(r, '[')) return fail(err, n, "world_entities: entities is not a list%s%s", NULL, NULL);
            if (!eat(r, ']')) {
                do { if (!parse_entity(r, d, &pend, err, n)) return false; } while (eat(r, ','));
                if (!eat(r, ']')) return fail(err, n, "world_entities: malformed entity list%s%s", NULL, NULL);
            }
            have_entities = true;
        } else if (!strcmp(key, "mover_definitions")) {
            if (!eat(r, '[')) return fail(err, n, "world_entities: mover_definitions is not a list%s%s", NULL, NULL);
            if (!eat(r, ']')) {
                do { if (!parse_mover_def(r, d, err, n)) return false; } while (eat(r, ','));
                if (!eat(r, ']')) return fail(err, n, "world_entities: malformed mover_definitions%s%s", NULL, NULL);
            }
            have_defs = true;
        } else if (!strcmp(key, "scripts")) {
            if (!eat(r, '[')) return fail(err, n, "world_entities: scripts is not a list%s%s", NULL, NULL);
            if (!eat(r, ']')) {
                do { if (!parse_script(r, d, &pend, err, n)) return false; } while (eat(r, ','));
                if (!eat(r, ']')) return fail(err, n, "world_entities: malformed scripts%s%s", NULL, NULL);
            }
        } else if (!strcmp(key, "ability_script")) {
            if (!str(r, pend.ability, sizeof(pend.ability))) return fail(err, n, "world_entities: malformed ability_script%s%s", NULL, NULL);
            pend.has_ability = true;
        } else return fail(err, n, "world_entities: unknown field '%s'%s", key, NULL);
    } while (eat(r, ','));
    if (!eat(r, '}') || !have_schema || !have_entities)
        return fail(err, n, "world_entities: needs schema and entities%s%s", NULL, NULL);
    if (have_defs && d->schema < 2)
        return fail(err, n, "world_entities: mover_definitions need schema 2%s%s", NULL, NULL);
    /* IDs first, so a duplicate is reported as one, not as a link to it. */
    for (uint32_t i = 0; i < d->mover_def_count; i++) {
        if (!mover_id(d->mover_def[i].id))
            return fail(err, n, "'%s': malformed mover definition ID (namespace:mover/name)%s", d->mover_def[i].id, NULL);
        for (uint32_t j = 0; j < i; j++)
            if (!strcmp(d->mover_def[j].id, d->mover_def[i].id))
                return fail(err, n, "%s: duplicate mover definition ID%s", d->mover_def[i].id, NULL);
    }
    for (uint32_t i = 0; i < d->count; i++) {
        if (!placed_id(d->entity[i].id))
            return fail(err, n, "'%s': malformed placed ID (namespace:entity/name)%s", d->entity[i].id, NULL);
        for (uint32_t j = 0; j < i; j++)
            if (!strcmp(d->entity[j].id, d->entity[i].id))
                return fail(err, n, "%s: duplicate placed ID%s", d->entity[i].id, NULL);
    }
    /* Resolve every link's target ID to its entity, once. */
    for (uint32_t i = 0; i < d->count; i++) {
        const hta_wdef *e = &d->entity[i];
        for (uint32_t k = 0; k < e->link_count; k++) {
            uint32_t li = e->first_link + k;
            int32_t t = hta_world_defs_find(d, pend.target[li]);
            if (t < 0 && hta_world_defs_find_script(d, pend.target[li]) >= 0)
                return fail(err, n, "%s: link target %s is a script, expected a placed entity", e->id, pend.target[li]);
            if (t < 0 && hta_world_defs_find_mover(d, pend.target[li]) >= 0)
                return fail(err, n, "%s: link target %s is a mover definition, expected a placed entity", e->id, pend.target[li]);
            if (t < 0) return fail(err, n, "%s references missing target %s", e->id, pend.target[li]);
            d->link[li].target = (uint16_t)t;
        }
    }
    return resolve_movers(d, &pend, err, n) && resolve_scripts(d, &pend, err, n);
}

bool hta_world_defs_parse(const uint8_t *manifest, size_t len, hta_world_defs *out, char *err, size_t errlen)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (err && errlen) err[0] = 0;
    if (!manifest) return fail(err, errlen, "no manifest%s%s", NULL, NULL);
    rd r = { manifest, manifest + len, false };
    char key[64];
    if (!eat(&r, '{')) return fail(err, errlen, "manifest is not an object%s%s", NULL, NULL);
    if (eat(&r, '}')) return true;
    bool found = false;
    do {
        /* A key longer than any we look for is skipped, not an error. */
        const uint8_t *at = r.p;
        if (!str(&r, key, sizeof(key))) { r.p = at; if (!str(&r, NULL, 0)) return fail(err, errlen, "malformed manifest%s%s", NULL, NULL); key[0] = 0; }
        if (!eat(&r, ':')) return fail(err, errlen, "malformed manifest%s%s", NULL, NULL);
        if (!strcmp(key, "world_entities")) {
            if (found) return fail(err, errlen, "world_entities appears twice%s%s", NULL, NULL);
            if (!parse_section(&r, out, err, errlen)) { memset(out, 0, sizeof(*out)); return false; }
            found = true;
        } else if (!skip(&r)) return fail(err, errlen, "malformed manifest%s%s", NULL, NULL);
    } while (eat(&r, ','));
    if (!eat(&r, '}')) return fail(err, errlen, "malformed manifest%s%s", NULL, NULL);
    ws(&r);
    if (r.p != r.end) return fail(err, errlen, "trailing bytes after the manifest%s%s", NULL, NULL);
    if (!hta_world_defs_check(out, err, errlen)) { memset(out, 0, sizeof(*out)); return false; }
    return true;
}

int32_t hta_world_defs_find(const hta_world_defs *d, const char *id)
{
    for (uint32_t i = 0; d && id && i < d->count; i++) if (!strcmp(d->entity[i].id, id)) return (int32_t)i;
    return -1;
}

int32_t hta_world_defs_find_script(const hta_world_defs *d, const char *id)
{
    for (uint32_t i = 0; d && id && *id && i < d->script_count; i++)
        if (!strcmp(d->script[i].id, id)) return (int32_t)i;
    return -1;
}

int32_t hta_world_defs_find_mover(const hta_world_defs *d, const char *id)
{
    for (uint32_t i = 0; d && id && *id && i < d->mover_def_count; i++)
        if (!strcmp(d->mover_def[i].id, id)) return (int32_t)i;
    return -1;
}

const hta_wmover_def *hta_wdef_mover(const hta_world_defs *d, uint32_t entity)
{
    if (!d || entity >= d->count || d->entity[entity].kind != HTA_WDEF_MOVER ||
        d->entity[entity].def >= d->mover_def_count) return NULL;
    return &d->mover_def[d->entity[entity].def];
}

void hta_wdef_mover_box(const hta_world_defs *d, uint32_t entity, float min[3], float max[3])
{
    const hta_wmover_def *m = hta_wdef_mover(d, entity);
    for (int k = 0; k < 3; k++) {
        float c = m ? d->entity[entity].pos[k] : 0.0f, h = m ? m->size[k] * 0.5f : 0.0f;
        min[k] = c - h; max[k] = c + h;
    }
}

bool hta_manifest_members(const uint8_t *manifest, size_t len, hta_manifest_member_fn fn, void *ctx)
{
    if (!manifest) return false;
    rd r = { manifest, manifest + len, false };
    char key[64];
    if (!eat(&r, '{')) return false;
    if (eat(&r, '}')) { ws(&r); return r.p == r.end; }
    do {
        const uint8_t *at = r.p;
        if (!str(&r, key, sizeof(key))) { r.p = at; if (!str(&r, NULL, 0)) return false; key[0] = 0; }
        if (!eat(&r, ':')) return false;
        ws(&r);
        const uint8_t *v = r.p;
        if (!skip(&r)) return false;
        if (fn) fn(ctx, key, v, (size_t)(r.p - v));
    } while (eat(&r, ','));
    if (!eat(&r, '}')) return false;
    ws(&r);
    return r.p == r.end;
}

/* ---- the rules -------------------------------------------------------- */

static bool finite3(const float v[3], float lim)
{
    for (int k = 0; k < 3; k++) if (!isfinite(v[k]) || fabsf(v[k]) > lim) return false;
    return true;
}

/* Longest chain of links from `i` (0 = none), or -1 on a cycle. */
static int chain(const hta_world_defs *d, uint32_t i, uint8_t *mark, int8_t *memo)
{
    if (mark[i] == 1) return -1;
    if (mark[i] == 2) return memo[i];
    mark[i] = 1;
    int best = 0;
    const hta_wdef *e = &d->entity[i];
    for (uint32_t k = 0; k < e->link_count; k++) {
        int c = chain(d, d->link[e->first_link + k].target, mark, memo);
        if (c < 0) return -1;
        if (c + 1 > best) best = c + 1;
    }
    mark[i] = 2;
    memo[i] = (int8_t)(best > 100 ? 100 : best);
    return best;
}

bool hta_world_defs_check(const hta_world_defs *d, char *err, size_t n)
{
    if (!d || d->count > HTA_WDEF_MAX_ENTITIES || d->link_count > HTA_WDEF_MAX_LINKS)
        return fail(err, n, "world entities over their limits%s%s", NULL, NULL);
    if (d->mover_def_count > HTA_WDEF_MAX_MOVER_DEFS)
        return fail(err, n, "mover definitions over their limit%s%s", NULL, NULL);
    for (uint32_t i = 0; i < d->mover_def_count; i++) {
        const hta_wmover_def *m = &d->mover_def[i];
        /* Unnamed: an X1 inline mover's own. Named: namespace:mover/name,
         * in the world's namespace, unique. */
        if (!memchr(m->id, 0, sizeof(m->id)) || (m->id[0] && !mover_id(m->id)) ||
            (!m->id[0] && d->schema >= 2))
            return fail(err, n, "'%s': malformed mover definition ID (namespace:mover/name)%s", m->id, NULL);
        if (m->id[0] && d->count && !same_namespace(m->id, d->entity[0].id))
            return fail(err, n, "%s: not in the world's namespace (%s)", m->id, d->entity[0].id);
        for (uint32_t j = 0; j < i && m->id[0]; j++)
            if (!strcmp(d->mover_def[j].id, m->id)) return fail(err, n, "%s: duplicate mover definition ID%s", m->id, NULL);
        const char *name = m->id[0] ? m->id : "(inline mover)";
        for (int k = 0; k < 3; k++)
            if (!isfinite(m->size[k]) || !(m->size[k] >= 0.01f && m->size[k] <= HTA_WDEF_WORLD_LIMIT))
                return fail(err, n, "%s: mover size must be finite, at least 0.01 wu%s", name, NULL);
        float len = sqrtf(m->move[0] * m->move[0] + m->move[1] * m->move[1] + m->move[2] * m->move[2]);
        if (!finite3(m->move, HTA_WDEF_MAX_MOVE) || !(len >= 0.01f && len <= HTA_WDEF_MAX_MOVE))
            return fail(err, n, "%s: mover move out of range%s", name, NULL);
        if (!(m->speed > 0.0f && m->speed <= HTA_WDEF_MAX_SPEED))
            return fail(err, n, "%s: mover speed out of range%s", name, NULL);
    }
    if (d->script_count > HTA_WDEF_MAX_SCRIPTS || d->pool_used > HTA_WDEF_SCRIPT_POOL)
        return fail(err, n, "scripts over their limits%s%s", NULL, NULL);
    for (uint32_t i = 0; i < d->script_count; i++) {
        const hta_wscript_def *sc = &d->script[i];
        if (!memchr(sc->id, 0, sizeof(sc->id)) || !script_id(sc->id))
            return fail(err, n, "'%s': malformed script ID (namespace:script/name)%s", sc->id, NULL);
        if (d->count && !same_namespace(sc->id, d->entity[0].id))
            return fail(err, n, "%s: not in the world's namespace (%s)", sc->id, d->entity[0].id);
        if ((uint64_t)sc->at + sc->len > d->pool_used || !sc->callbacks || (sc->callbacks & ~3u))
            return fail(err, n, "%s: malformed script%s", sc->id, NULL);
    }
    if (d->ability_script &&
        (d->ability_script > d->script_count || !(d->script[d->ability_script - 1].callbacks & HTA_WCB_ON_ABILITY)))
        return fail(err, n, "ability_script: not a script with on_ability%s%s", NULL, NULL);
    char ns[48] = "";
    for (uint32_t i = 0; i < d->count; i++) {
        const hta_wdef *e = &d->entity[i];
        if (e->script &&
            (e->kind != HTA_WDEF_INTERACTABLE || e->script > d->script_count ||
             !(d->script[e->script - 1].callbacks & HTA_WCB_ON_USED)))
            return fail(err, n, "%s: bad script reference%s", e->id, NULL);
        if (!memchr(e->id, 0, sizeof(e->id)) || !placed_id(e->id))
            return fail(err, n, "'%s': malformed placed ID (namespace:entity/name)%s", e->id, NULL);
        size_t nl = (size_t)(strchr(e->id, ':') - e->id);
        if (!i) { memcpy(ns, e->id, nl); ns[nl] = 0; }
        else if (strlen(ns) != nl || memcmp(ns, e->id, nl))
            return fail(err, n, "%s: not in the world's namespace %s", e->id, ns);
        for (uint32_t j = 0; j < i; j++)
            if (!strcmp(d->entity[j].id, e->id)) return fail(err, n, "%s: duplicate placed ID%s", e->id, NULL);
        if (e->kind == HTA_WDEF_NONE || e->kind >= HTA_WDEF_KIND_COUNT)
            return fail(err, n, "%s: unknown kind%s", e->id, NULL);
        if (e->link_count > HTA_WDEF_MAX_LINKS_PER || (uint32_t)e->first_link + e->link_count > d->link_count)
            return fail(err, n, "%s: links out of range%s", e->id, NULL);
        if ((e->kind == HTA_WDEF_INTERACTABLE || e->kind == HTA_WDEF_TELEPORT) && !finite3(e->pos, HTA_WDEF_WORLD_LIMIT))
            return fail(err, n, "%s: position must be finite and inside the world%s", e->id, NULL);
        if (e->kind == HTA_WDEF_INTERACTABLE && !(e->reach > 0.0f && e->reach <= HTA_WDEF_MAX_REACH))
            return fail(err, n, "%s: reach out of range%s", e->id, NULL);
        if (e->kind == HTA_WDEF_TELEPORT && !isfinite(e->yaw)) return fail(err, n, "%s: yaw is not finite%s", e->id, NULL);
        if (e->kind == HTA_WDEF_TRIGGER) {
            if (!finite3(e->min, HTA_WDEF_WORLD_LIMIT) || !finite3(e->max, HTA_WDEF_WORLD_LIMIT))
                return fail(err, n, "%s: bounds must be finite%s", e->id, NULL);
            for (int k = 0; k < 3; k++)
                if (e->max[k] - e->min[k] < 0.05f)
                    return fail(err, n, "%s: bounds are empty%s", e->id, NULL);
        }
        if (e->kind == HTA_WDEF_MOVER) {
            if (e->def >= d->mover_def_count) return fail(err, n, "%s: mover has no definition%s", e->id, NULL);
            float lo[3], hi[3];
            hta_wdef_mover_box(d, i, lo, hi);
            if (!finite3(e->pos, HTA_WDEF_WORLD_LIMIT) || !finite3(lo, HTA_WDEF_WORLD_LIMIT) || !finite3(hi, HTA_WDEF_WORLD_LIMIT))
                return fail(err, n, "%s: mover must be finite and inside the world%s", e->id, NULL);
        } else if (e->def != HTA_WDEF_NO_DEF) return fail(err, n, "%s: only a mover takes a definition%s", e->id, NULL);
        for (uint32_t k = 0; k < e->link_count; k++) {
            const hta_wdef_link *l = &d->link[e->first_link + k];
            if (l->target >= d->count) return fail(err, n, "%s: link target out of range%s", e->id, NULL);
            const hta_wdef *t = &d->entity[l->target];
            if (l->target == i) return fail(err, n, "%s: an entity cannot target itself%s", e->id, NULL);
            if (!hta_wdef_emits(e->kind, l->event))
                return fail(err, n, "%s: does not emit '%s'", e->id, hta_wdef_event_name(l->event));
            if (!hta_wdef_accepts(t->kind, l->input)) {
                char what[HTA_WDEF_ID_MAX + 24];
                snprintf(what, sizeof(what), "%s.%s", t->id, hta_wdef_input_name(l->input));
                return fail(err, n, "%s: target does not accept %s", e->id, what);
            }
        }
    }
    for (uint32_t i = 0; i < d->count; i++) {
        const hta_wdef *e = &d->entity[i];
        if (e->kind != HTA_WDEF_TELEPORT) continue;
        for (uint32_t j = 0; j < d->count; j++) {
            const hta_wdef *t = &d->entity[j];
            if (t->kind == HTA_WDEF_TRIGGER && e->pos[0] >= t->min[0] && e->pos[0] <= t->max[0] &&
                e->pos[1] >= t->min[1] && e->pos[1] <= t->max[1] && e->pos[2] >= t->min[2] && e->pos[2] <= t->max[2])
                return fail(err, n, "%s: destination is inside trigger %s", e->id, t->id);
        }
    }
    uint8_t mark[HTA_WDEF_MAX_ENTITIES] = { 0 };
    int8_t memo[HTA_WDEF_MAX_ENTITIES] = { 0 };
    for (uint32_t i = 0; i < d->count; i++) {
        int c = chain(d, i, mark, memo);
        if (c < 0) return fail(err, n, "link cycle through %s%s", d->entity[i].id, NULL);
        if (c > (int)HTA_WDEF_MAX_CHAIN) return fail(err, n, "%s: chain of links too long%s", d->entity[i].id, NULL);
    }
    return true;
}
