/* Prefabs (prefab.h, docs/PREFABS.md): the "prefabs" member of a library,
 * parsed and checked into a compiled table, and the placement transform.
 * References are resolved later, from the provider's point of view, when
 * the package set loads (package.c); expansion into a world's entities is
 * world_def.c's. */
#include "prefab.h"
#include "mjson.h"
#include "world_def.h"
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

/* ---- local IDs ---------------------------------------------------------------- */

static const char *shown(uint8_t c, char buf[8])
{
    if (c >= 0x21 && c < 0x7F) snprintf(buf, 8, "'%c'", c);
    else if (c == ' ') snprintf(buf, 8, "a space");
    else snprintf(buf, 8, "\\x%02x", c);
    return buf;
}

bool hta_prefab_local_valid(const char *id, const char *what, char *why, size_t n)
{
    char b[8];
    if (why && n) why[0] = 0;
    if (!id || !id[0]) return failv(why, n, "empty %s", what);
    size_t len = strnlen(id, HTA_PREFAB_LOCAL_MAX + 1);
    if (len > HTA_PREFAB_LOCAL_MAX) return failv(why, n, "%s longer than %u bytes", what, HTA_PREFAB_LOCAL_MAX);
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)id[i];
        bool ok = (c >= 'a' && c <= 'z') || (i && ((c >= '0' && c <= '9') || c == '_'));
        if (!ok) {
            if (c >= 'A' && c <= 'Z') return failv(why, n, "%s has capital %s (IDs are lowercase; nothing is folded)", what, shown(c, b));
            if (!i) return failv(why, n, "%s starts with %s, not a lowercase letter", what, shown(c, b));
            return failv(why, n, "%s has %s, not [a-z0-9_]", what, shown(c, b));
        }
        if (c == '_' && id[i + 1] == '_')
            return failv(why, n, "%s has '__' (reserved: a child's entity is <instance>__<child>)", what);
    }
    if (id[len - 1] == '_') return failv(why, n, "%s ends with '_'", what);
    return true;
}

/* ---- transforms ------------------------------------------------------------------ */

bool hta_yaw_axis_aligned(double deg) { return isfinite(deg) && fmod(deg, 90.0) == 0.0; }

void hta_yaw_cossin(double deg, float *c, float *s)
{
    if (!isfinite(deg)) { *c = 1.0f; *s = 0.0f; return; }
    double r = fmod(deg, 360.0);           /* exact */
    if (r < 0.0) r += 360.0;
    if (r == 0.0) { *c = 1.0f; *s = 0.0f; return; }
    if (r == 90.0) { *c = 0.0f; *s = 1.0f; return; }
    if (r == 180.0) { *c = -1.0f; *s = 0.0f; return; }
    if (r == 270.0) { *c = 0.0f; *s = -1.0f; return; }
    double rad = r * (3.14159265358979323846 / 180.0);
    *c = (float)cos(rad);
    *s = (float)sin(rad);
}

void hta_xform_vector(float scale, float c, float s, const float v[3], float out[3])
{
    float x = c * v[0] - s * v[1], y = s * v[0] + c * v[1];
    out[0] = scale * x; out[1] = scale * y; out[2] = scale * v[2];
}

void hta_xform_point(const hta_xform *x, float c, float s, const float p[3], float out[3])
{
    float v[3];
    hta_xform_vector(x->scale, c, s, p, v);
    for (int k = 0; k < 3; k++) out[k] = x->pos[k] + v[k];
}

/* ---- the member ------------------------------------------------------------------- */

void hta_prefab_table_free(hta_prefab_table *t)
{
    if (!t) return;
    free(t->prefab);
    memset(t, 0, sizeof(*t));
}

int32_t hta_prefab_find(const hta_prefab_table *t, const char *id)
{
    for (uint32_t i = 0; t && id && i < t->count; i++) if (!strcmp(t->prefab[i].id, id)) return (int32_t)i;
    return -1;
}

/* A child's fields: what each kind takes, and needs. */
enum {
    F_POS = 1u << 0, F_REACH = 1u << 1, F_SCRIPT = 1u << 2, F_SIZE = 1u << 3, F_MOVE = 1u << 4,
    F_SPEED = 1u << 5, F_YAW = 1u << 6, F_SOUND = 1u << 7, F_MODEL = 1u << 8, F_BOUNDS = 1u << 9,
};
static const struct { const char *name; uint32_t bit; } FIELDS[] = {
    { "position", F_POS }, { "reach", F_REACH }, { "script", F_SCRIPT }, { "size", F_SIZE }, { "move", F_MOVE },
    { "speed", F_SPEED }, { "yaw_degrees", F_YAW }, { "sound", F_SOUND }, { "model", F_MODEL }, { "bounds", F_BOUNDS },
};
static const uint32_t TAKES[HTA_WDEF_KIND_COUNT] = {
    [HTA_WDEF_INTERACTABLE] = F_POS | F_REACH | F_SCRIPT,
    [HTA_WDEF_RELAY] = 0,
    [HTA_WDEF_MOVER] = F_POS | F_SIZE | F_MOVE | F_SPEED | F_YAW | F_SOUND | F_MODEL,
    [HTA_WDEF_TRIGGER] = F_BOUNDS,
    [HTA_WDEF_TELEPORT] = F_POS | F_YAW,
    [HTA_WDEF_PROP] = F_POS | F_MODEL | F_YAW,
};
static const uint32_t NEEDS[HTA_WDEF_KIND_COUNT] = {
    [HTA_WDEF_INTERACTABLE] = F_POS | F_REACH,
    [HTA_WDEF_MOVER] = F_POS | F_SIZE | F_MOVE | F_SPEED,
    [HTA_WDEF_TRIGGER] = F_BOUNDS,
    [HTA_WDEF_TELEPORT] = F_POS,
    [HTA_WDEF_PROP] = F_POS | F_MODEL,
};

static const char *article(const char *noun) { return strchr("aeiou", noun[0]) ? "an" : "a"; }

static uint8_t kind_of(const char *s)
{
    for (uint8_t k = 1; k < HTA_WDEF_KIND_COUNT; k++) if (!strcmp(hta_wdef_kind_name(k), s)) return k;
    return 0;
}

static uint8_t event_of(const char *s)
{
    for (uint8_t e = 1; e < HTA_WEV_COUNT; e++) if (!strcmp(hta_wdef_event_name(e), s)) return e;
    return 0;
}

static bool vec3(hta_mj *r, float out[3], float lim)
{
    if (!hta_mj_eat(r, '[')) return false;
    for (int k = 0; k < 3; k++) {
        double d;
        if ((k && !hta_mj_eat(r, ',')) || !hta_mj_num(r, &d) || !(fabs(d) <= lim)) return false;
        out[k] = (float)d;
    }
    return hta_mj_eat(r, ']');
}

static bool fnum(hta_mj *r, float *out, float lim)
{
    double d;
    if (!hta_mj_num(r, &d) || !(fabs(d) <= lim)) return false;
    *out = (float)d;
    return true;
}

static void peek_string(const hta_mj *r0, const char *want, char *out, size_t cap);

/* Link targets as written, until every sibling is known. */
typedef struct { char target[HTA_PREFAB_MAX_LINKS][HTA_PREFAB_LOCAL_MAX + 1]; } link_names;

static bool parse_link(hta_mj *r, hta_prefab *p, hta_prefab_child *c, link_names *ln, const char *who, char *err, size_t n)
{
    if (c->link_count >= HTA_PREFAB_MAX_LINKS_PER)
        return failv(err, n, "%s: more than %u links", who, HTA_PREFAB_MAX_LINKS_PER);
    if (p->link_count >= HTA_PREFAB_MAX_LINKS)
        return failv(err, n, "%s: more than %u links in the prefab", who, HTA_PREFAB_MAX_LINKS);
    hta_prefab_link *l = &p->link[p->link_count];
    memset(l, 0, sizeof(*l));
    char key[16], val[64];
    bool have[3] = { 0 };
    if (!hta_mj_eat(r, '{')) return failv(err, n, "%s: malformed link", who);
    if (!hta_mj_eat(r, '}')) do {
        if (!hta_mj_str(r, key, sizeof(key)) || !hta_mj_eat(r, ':') || !hta_mj_str(r, val, sizeof(val)))
            return failv(err, n, "%s: malformed link", who);
        if (!strcmp(key, "event")) {
            l->event = event_of(val); have[0] = true;
            if (!l->event) return failv(err, n, "%s: unknown event '%s'", who, val);
        } else if (!strcmp(key, "input")) {
            l->input = hta_wdef_input_from_name(val); have[1] = true;
            if (!l->input) return failv(err, n, "%s: unknown input '%s'", who, val);
        } else if (!strcmp(key, "target")) {
            if (strlen(val) > HTA_PREFAB_LOCAL_MAX) return failv(err, n, "%s references missing child '%s'", who, val);
            memcpy(ln->target[p->link_count], val, strlen(val) + 1);
            have[2] = true;
        } else return failv(err, n, "%s: unknown link field '%s'", who, key);
    } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, '}') || !have[0] || !have[1] || !have[2])
        return failv(err, n, "%s: a link needs event, input and target", who);
    p->link_count++;
    c->link_count++;
    return true;
}

static bool parse_child(hta_mj *r, hta_prefab *p, link_names *ln, char *err, size_t n)
{
    if (p->child_count >= HTA_PREFAB_MAX_CHILDREN)
        return failv(err, n, "prefab %s has more than %u children", p->id, HTA_PREFAB_MAX_CHILDREN);
    hta_prefab_child *c = &p->child[p->child_count];
    memset(c, 0, sizeof(*c));
    c->first_link = (uint16_t)p->link_count;
    char who[HTA_RID_MAX + 96], key[24], kind[24] = "", idv[64] = "", why[128];
    snprintf(who, sizeof(who), "prefab %s child %u", p->id, p->child_count);
    {
        char pre[64];
        peek_string(r, "id", pre, sizeof(pre));
        if (pre[0] && strlen(pre) <= HTA_PREFAB_LOCAL_MAX) snprintf(who, sizeof(who), "prefab %s child '%s'", p->id, pre);
    }
    uint32_t got = 0;
    bool have_id = false, have_kind = false, have_links = false, nested = false;
    if (!hta_mj_eat(r, '{')) return failv(err, n, "%s: not an object", who);
    if (!hta_mj_eat(r, '}')) do {
        if (!hta_mj_str(r, key, sizeof(key)) || !hta_mj_eat(r, ':')) return failv(err, n, "%s: malformed field", who);
        bool ok = true;
        uint32_t bit = 0;
        for (size_t f = 0; f < sizeof(FIELDS) / sizeof(FIELDS[0]); f++) if (!strcmp(key, FIELDS[f].name)) bit = FIELDS[f].bit;
        if (bit & got) return failv(err, n, "%s: '%s' appears twice", who, key);
        got |= bit;
        if (!strcmp(key, "id")) {
            if (have_id || !hta_mj_str(r, idv, sizeof(idv))) return failv(err, n, "%s: malformed id", who);
            if (!hta_prefab_local_valid(idv, "local child id", why, sizeof(why)))
                return failv(err, n, "prefab %s child '%s': %s", p->id, idv, why);
            memcpy(c->id, idv, strlen(idv) + 1);
            snprintf(who, sizeof(who), "prefab %s child '%s'", p->id, c->id);
            have_id = true;
        } else if (!strcmp(key, "kind")) {
            if (have_kind || !hta_mj_str(r, kind, sizeof(kind))) return failv(err, n, "%s: malformed kind", who);
            have_kind = true;
            if (!strcmp(kind, "prefab")) nested = true;
            else if (!(c->kind = kind_of(kind)))
                return failv(err, n, "%s: unknown kind '%s' (a prefab child is interactable, relay, mover, trigger, teleport or prop)",
                             who, kind);
        } else if (!strcmp(key, "links")) {
            if (have_links || !hta_mj_eat(r, '[')) return failv(err, n, "%s: malformed links", who);
            have_links = true;
            if (!hta_mj_eat(r, ']')) {
                do { if (!parse_link(r, p, c, ln, who, err, n)) return false; } while (hta_mj_eat(r, ','));
                if (!hta_mj_eat(r, ']')) return failv(err, n, "%s: malformed links", who);
            }
        } else if (!strcmp(key, "prefab")) {
            nested = true;
            if (!hta_mj_skip(r)) return failv(err, n, "%s: malformed prefab", who);
        } else if (bit == F_POS) ok = vec3(r, c->pos, HTA_PREFAB_MAX_LOCAL);
        else if (bit == F_REACH) ok = fnum(r, &c->reach, 1e6f);
        else if (bit == F_YAW) ok = fnum(r, &c->yaw_deg, 1e6f);
        else if (bit == F_SIZE) ok = vec3(r, c->size, 1e6f);
        else if (bit == F_MOVE) ok = vec3(r, c->move, 1e6f);
        else if (bit == F_SPEED) ok = fnum(r, &c->speed, 1e6f);
        else if (bit == F_SCRIPT) ok = hta_mj_str(r, c->script_ref, sizeof(c->script_ref));
        else if (bit == F_SOUND) ok = hta_mj_str(r, c->sound_ref, sizeof(c->sound_ref));
        else if (bit == F_MODEL) ok = hta_mj_str(r, c->model_ref, sizeof(c->model_ref));
        else if (bit == F_BOUNDS) {
            char k2[8];
            bool mn = false, mx = false;
            ok = hta_mj_eat(r, '{');
            if (ok && !hta_mj_eat(r, '}')) {
                do {
                    ok = hta_mj_str(r, k2, sizeof(k2)) && hta_mj_eat(r, ':');
                    if (ok && !strcmp(k2, "min") && !mn) { ok = vec3(r, c->min, HTA_PREFAB_MAX_LOCAL); mn = true; }
                    else if (ok && !strcmp(k2, "max") && !mx) { ok = vec3(r, c->max, HTA_PREFAB_MAX_LOCAL); mx = true; }
                    else ok = false;
                } while (ok && hta_mj_eat(r, ','));
                ok = ok && hta_mj_eat(r, '}');
            }
            ok = ok && mn && mx;
        } else return failv(err, n, "%s: unknown field '%s'", who, key);
        if (!ok) return failv(err, n, "%s: malformed or out-of-range '%s'", who, key);
    } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, '}')) return failv(err, n, "%s: malformed child", who);
    if (nested)
        return failv(err, n, "prefab %s child '%s' contains a nested prefab reference, which is not supported in prefab schema %u",
                     p->id, have_id ? c->id : "?", HTA_PREFAB_SCHEMA);
    if (!have_id) return failv(err, n, "%s: has no id", who);
    if (!have_kind) return failv(err, n, "%s: has no kind", who);
    if (!have_links) return failv(err, n, "%s: has no links (write [] for none)", who);
    const char *kn = hta_wdef_kind_name(c->kind);
    for (size_t f = 0; f < sizeof(FIELDS) / sizeof(FIELDS[0]); f++) {
        if ((got & FIELDS[f].bit) && !(TAKES[c->kind] & FIELDS[f].bit))
            return failv(err, n, "%s: %s %s does not take '%s'", who, article(kn), kn, FIELDS[f].name);
        if (!(got & FIELDS[f].bit) && (NEEDS[c->kind] & FIELDS[f].bit))
            return failv(err, n, "%s: %s %s needs '%s'", who, article(kn), kn, FIELDS[f].name);
    }
    /* The numbers, in prefab space (an instance's scale is checked when it
     * is placed: world_def.c). */
    if ((got & F_YAW) && !(fabsf(c->yaw_deg) <= HTA_PREFAB_MAX_YAW))
        return failv(err, n, "%s: yaw_degrees out of range (|yaw| <= %g)", who, (double)HTA_PREFAB_MAX_YAW);
    if (c->kind == HTA_WDEF_INTERACTABLE && !(c->reach > 0.0f && c->reach <= HTA_WDEF_MAX_REACH))
        return failv(err, n, "%s: reach out of range (0, %g] wu", who, (double)HTA_WDEF_MAX_REACH);
    if (c->kind == HTA_WDEF_MOVER) {
        for (int k = 0; k < 3; k++)
            if (!(c->size[k] >= 0.01f && c->size[k] <= HTA_PREFAB_MAX_LOCAL))
                return failv(err, n, "%s: mover size must be at least 0.01 wu and at most %g", who, (double)HTA_PREFAB_MAX_LOCAL);
        float len = sqrtf(c->move[0] * c->move[0] + c->move[1] * c->move[1] + c->move[2] * c->move[2]);
        if (!(len >= 0.01f && len <= HTA_WDEF_MAX_MOVE)) return failv(err, n, "%s: mover move out of range (0.01 to %g wu)", who, (double)HTA_WDEF_MAX_MOVE);
        if (!(c->speed > 0.0f && c->speed <= HTA_WDEF_MAX_SPEED)) return failv(err, n, "%s: mover speed out of range (0, %g] wu/s", who, (double)HTA_WDEF_MAX_SPEED);
    }
    if (c->kind == HTA_WDEF_TRIGGER)
        for (int k = 0; k < 3; k++)
            if (!(c->max[k] - c->min[k] >= 0.05f)) return failv(err, n, "%s: trigger bounds are empty or thinner than 0.05 wu", who);
    p->child_count++;
    return true;
}

/* Siblings: link targets to local indices, events emitted, inputs accepted,
 * no self-link; then no cycle and no chain longer than the world allows.
 * Iterative (explicit stack) over at most HTA_PREFAB_MAX_CHILDREN. */
static bool link_children(hta_prefab *p, const link_names *ln, char *err, size_t n)
{
    for (uint32_t i = 0; i < p->child_count; i++) {
        const hta_prefab_child *c = &p->child[i];
        const char *kn = hta_wdef_kind_name(c->kind);
        for (uint32_t k = 0; k < c->link_count; k++) {
            hta_prefab_link *l = &p->link[c->first_link + k];
            const char *t = ln->target[c->first_link + k];
            int32_t at = -1;
            for (uint32_t j = 0; j < p->child_count; j++) if (!strcmp(p->child[j].id, t)) at = (int32_t)j;
            if (at < 0) return failv(err, n, "prefab %s child '%s' references missing child '%s'", p->id, c->id, t);
            if ((uint32_t)at == i) return failv(err, n, "prefab %s child '%s' links to itself", p->id, c->id);
            if (!hta_wdef_emits(c->kind, l->event))
                return failv(err, n, "prefab %s child '%s': %s %s does not emit '%s'", p->id, c->id, article(kn), kn,
                             hta_wdef_event_name(l->event));
            const char *tk = hta_wdef_kind_name(p->child[at].kind);
            if (!hta_wdef_accepts(p->child[at].kind, l->input))
                return failv(err, n, "prefab %s child '%s' links to child '%s', and %s %s does not accept '%s'", p->id, c->id, t,
                             article(tk), tk, hta_wdef_input_name(l->input));
            l->target = (uint16_t)at;
        }
    }
    /* Longest chain from each child, iteratively; a grey node reached again is a cycle. */
    uint8_t state[HTA_PREFAB_MAX_CHILDREN] = { 0 };
    int depth[HTA_PREFAB_MAX_CHILDREN] = { 0 };
    for (uint32_t root = 0; root < p->child_count; root++) {
        if (state[root]) continue;
        uint32_t stack[HTA_PREFAB_MAX_CHILDREN + 1], next[HTA_PREFAB_MAX_CHILDREN + 1], sp = 0;
        stack[sp] = root; next[sp] = 0; sp++; state[root] = 1;
        while (sp) {
            uint32_t v = stack[sp - 1];
            const hta_prefab_child *c = &p->child[v];
            if (next[sp - 1] < c->link_count) {
                uint32_t w = p->link[c->first_link + next[sp - 1]++].target;
                if (state[w] == 1) {
                    char path[HTA_PREFAB_MAX_CHILDREN * (HTA_PREFAB_LOCAL_MAX + 4) + 32];
                    size_t at = 0;
                    uint32_t from = 0;
                    while (from < sp && stack[from] != w) from++;
                    for (uint32_t i = from; i < sp && at < sizeof(path); i++)
                        at += (size_t)snprintf(path + at, sizeof(path) - at, "%s -> ", p->child[stack[i]].id);
                    if (at < sizeof(path)) snprintf(path + at, sizeof(path) - at, "%s", p->child[w].id);
                    return failv(err, n, "prefab %s: link cycle: %s (links are zero-delay: a cycle would loop in one tick)", p->id, path);
                }
                if (!state[w]) { state[w] = 1; stack[sp] = w; next[sp] = 0; sp++; }
                continue;
            }
            int best = 0;
            for (uint32_t k = 0; k < c->link_count; k++) {
                int d = depth[p->link[c->first_link + k].target] + 1;
                if (d > best) best = d;
            }
            depth[v] = best;
            if (best > (int)HTA_WDEF_MAX_CHAIN)
                return failv(err, n, "prefab %s child '%s': a chain of %d links starts here; at most %u", p->id, c->id, best,
                             HTA_WDEF_MAX_CHAIN);
            state[v] = 2;
            sp--;
        }
    }
    return true;
}

/* The string value of `want` in the object at r->p, without consuming it
 * ("" when absent or not a string): canonical key order puts "children"
 * before "id", and every message should name the prefab. */
static void peek_string(const hta_mj *r0, const char *want, char *out, size_t cap)
{
    hta_mj r = *r0;
    char key[24];
    out[0] = 0;
    if (!hta_mj_eat(&r, '{') || hta_mj_eat(&r, '}')) return;
    do {
        if (!hta_mj_str(&r, key, sizeof(key)) || !hta_mj_eat(&r, ':')) return;
        const uint8_t *v = r.p;
        if (!strcmp(key, want) && hta_mj_str(&r, out, cap)) return;
        out[0] = 0;
        r.p = v;
        if (!hta_mj_skip(&r)) return;
    } while (hta_mj_eat(&r, ','));
}

static bool parse_prefab(hta_mj *r, hta_prefab *p, const char *pkg, char *err, size_t n)
{
    static link_names ln;       /* 1.5 KB; parsing is single-threaded load-time work */
    memset(p, 0, sizeof(*p));
    memset(&ln, 0, sizeof(ln));
    char key[16], why[128];
    bool have_id = false, have_children = false;
    peek_string(r, "id", p->id, sizeof(p->id));
    if (!p->id[0]) snprintf(p->id, sizeof(p->id), "?");
    if (!hta_mj_eat(r, '{')) return failv(err, n, "%s: a prefab is not an object", pkg);
    if (!hta_mj_eat(r, '}')) do {
        if (!hta_mj_str(r, key, sizeof(key)) || !hta_mj_eat(r, ':')) return failv(err, n, "%s: malformed prefab", pkg);
        if (!strcmp(key, "id")) {
            if (have_id || !hta_mj_str(r, p->id, sizeof(p->id))) return failv(err, n, "%s: malformed prefab id", pkg);
            hta_rid rid;
            int rc = hta_rid_parse(p->id, &rid, why, sizeof(why));
            if (rc == HTA_RID_OK && rid.type != HTA_RT_PREFAB) {
                const hta_rtype_info *t = hta_rtype_get(rid.type);
                return failv(err, n, "%s: prefab '%s' is %s %s ID, expected namespace:prefab/name", pkg, p->id, article(t->noun), t->noun);
            }
            if (rc != HTA_RID_OK) return failv(err, n, "%s: prefab '%s' is not a resource ID: %s (expected namespace:prefab/name)", pkg, p->id, why);
            if (hta_rid_reserved_namespace(p->id, rid.ns_len))
                return failv(err, n, "%s: %s: namespace '%.*s' is reserved for built-in content", pkg, p->id, rid.ns_len, p->id);
            have_id = true;
        } else if (!strcmp(key, "children")) {
            if (have_children || !hta_mj_eat(r, '[')) return failv(err, n, "%s: prefab %s: malformed children", pkg, p->id);
            have_children = true;
            if (!hta_mj_eat(r, ']')) {
                do { if (!parse_child(r, p, &ln, err, n)) return false; } while (hta_mj_eat(r, ','));
                if (!hta_mj_eat(r, ']')) return failv(err, n, "prefab %s: malformed children", p->id);
            }
        } else return failv(err, n, "%s: prefab %s: unknown field '%s' (a prefab has children, id)", pkg, p->id, key);
    } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, '}')) return failv(err, n, "%s: malformed prefab", pkg);
    if (!have_id || !have_children) return failv(err, n, "%s: a prefab needs children and id", pkg);
    if (!p->child_count) return failv(err, n, "prefab %s has no children", p->id);
    /* Children in canonical (byte) order of local ID, each once: one byte
     * form per prefab, and a fixed expansion order on every peer. */
    for (uint32_t i = 1; i < p->child_count; i++) {
        int c = strcmp(p->child[i - 1].id, p->child[i].id);
        if (!c) return failv(err, n, "prefab %s contains duplicate local child id '%s'", p->id, p->child[i].id);
        if (c > 0) return failv(err, n, "prefab %s: children are not in canonical (byte) order of local id at '%s'", p->id, p->child[i].id);
    }
    return link_children(p, &ln, err, n);
}

static bool parse_member(hta_mj *r, const char *pkg, hta_prefab_table *out, char *err, size_t n)
{
    char key[16];
    bool have_schema = false, have_list = false;
    uint32_t cap = 0;
    if (!hta_mj_eat(r, '{')) return failv(err, n, "%s: prefabs is not an object", pkg);
    if (!hta_mj_eat(r, '}')) do {
        if (!hta_mj_str(r, key, sizeof(key)) || !hta_mj_eat(r, ':')) return failv(err, n, "%s: malformed prefabs", pkg);
        if (!strcmp(key, "schema")) {
            double s;
            if (have_schema || !hta_mj_num(r, &s)) return failv(err, n, "%s: malformed prefab schema", pkg);
            if (s != (double)HTA_PREFAB_SCHEMA)
                return failv(err, n, "%s: unsupported prefab schema %g (this engine has %u)", pkg, s, HTA_PREFAB_SCHEMA);
            have_schema = true;
        } else if (!strcmp(key, "prefabs")) {
            if (have_list || !hta_mj_eat(r, '[')) return failv(err, n, "%s: prefabs.prefabs is not a list", pkg);
            have_list = true;
            if (!hta_mj_eat(r, ']')) {
                do {
                    if (out->count >= HTA_PREFAB_MAX_PER_LIBRARY)
                        return failv(err, n, "%s provides more than %u prefabs", pkg, HTA_PREFAB_MAX_PER_LIBRARY);
                    if (out->count == cap) {
                        uint32_t nc = cap ? cap * 2 : 2;
                        if (nc > HTA_PREFAB_MAX_PER_LIBRARY) nc = HTA_PREFAB_MAX_PER_LIBRARY;
                        hta_prefab *np = realloc(out->prefab, nc * sizeof(*np));
                        if (!np) return failv(err, n, "out of memory");
                        out->prefab = np; cap = nc;
                    }
                    hta_prefab *p = &out->prefab[out->count];
                    if (!parse_prefab(r, p, pkg, err, n)) return false;
                    if (out->count) {
                        int c = strcmp(out->prefab[out->count - 1].id, p->id);
                        if (!c) return failv(err, n, "%s: %s is declared twice", pkg, p->id);
                        if (c > 0) return failv(err, n, "%s: prefabs are not in canonical (byte) order at %s", pkg, p->id);
                    }
                    out->count++;
                } while (hta_mj_eat(r, ','));
                if (!hta_mj_eat(r, ']')) return failv(err, n, "%s: malformed prefabs list", pkg);
            }
        } else return failv(err, n, "%s: prefabs: unknown field '%s' (schema %u has prefabs, schema)", pkg, key, HTA_PREFAB_SCHEMA);
    } while (hta_mj_eat(r, ','));
    if (!hta_mj_eat(r, '}')) return failv(err, n, "%s: malformed prefabs", pkg);
    if (!have_schema || !have_list) return failv(err, n, "%s: prefabs needs prefabs and schema", pkg);
    return true;
}

bool hta_prefab_parse(const uint8_t *manifest, size_t len, const char *pkg, hta_prefab_table *out, bool *declared,
                      char *err, size_t n)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (declared) *declared = false;
    if (err && n) err[0] = 0;
    if (!manifest) return failv(err, n, "no manifest");
    hta_mj r = { manifest, manifest + len, false, false };
    char key[64];
    bool found = false;
    if (!hta_mj_eat(&r, '{')) return failv(err, n, "manifest is not an object");
    if (!hta_mj_eat(&r, '}')) {
        do {
            const uint8_t *at = r.p;
            if (!hta_mj_str(&r, key, sizeof(key))) { r.p = at; if (!hta_mj_str(&r, NULL, 0)) goto bad; key[0] = 0; }
            if (!hta_mj_eat(&r, ':')) goto bad;
            if (!strcmp(key, "prefabs")) {
                if (found) { hta_prefab_table_free(out); return failv(err, n, "%s: prefabs appears twice", pkg); }
                found = true;
                if (!parse_member(&r, pkg, out, err, n)) { hta_prefab_table_free(out); return false; }
            } else if (!hta_mj_skip(&r)) goto bad;
        } while (hta_mj_eat(&r, ','));
        if (!hta_mj_eat(&r, '}')) goto bad;
    }
    hta_mj_ws(&r);
    if (r.p != r.end) goto bad;
    if (declared) *declared = found;
    return true;
bad:
    hta_prefab_table_free(out);
    return failv(err, n, "%s: malformed manifest", pkg);
}
