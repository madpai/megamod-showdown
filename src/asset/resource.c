/* Resource identity (resource.h): the grammar, the type registry, the
 * reference-field table and typed resolution. docs/RESOURCES.md. */
#include "resource.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- the registry -------------------------------------------------------- */

/* One row per type; its position is its hta_rtype. megamod-resources and
 * docs/RESOURCES.md are printed from this table. */
static const hta_rtype_info TYPES[HTA_RT_COUNT] = {
    [HTA_RT_WORLD] = { "world", "world", HTA_RT_SUPPORTED, HTA_RS_PACKAGE, false, false, "N2",
        "a playable world: the content of an OALMAP package, listed in its provides" },
    [HTA_RT_ENTITY] = { "entity", "placed entity", HTA_RT_SUPPORTED, HTA_RS_PLACEMENT, true, false, "X1",
        "a placement inside one world (button, relay, door, trigger, teleport); links and world.entity() name it" },
    [HTA_RT_MOVER] = { "mover", "mover definition", HTA_RT_SUPPORTED, HTA_RS_DEFINITION, true, false, "X2",
        "a reusable mover (size, travel, speed) that placed movers name; inside its world only" },
    [HTA_RT_SCRIPT] = { "script", "script", HTA_RT_SUPPORTED, HTA_RS_DEFINITION, true, true, "X3",
        "host-side Lua source (megamod.v1); a world's own, or imported from a library package (X4)" },
    [HTA_RT_CHARACTER] = { "character", "character", HTA_RT_SUPPORTED, HTA_RS_PACKAGE, false, false, "N2",
        "an imported character (.oalasset); still loaded by file name, its ID is audit-only" },
    [HTA_RT_WEAPON] = { "weapon", "weapon", HTA_RT_SUPPORTED, HTA_RS_PACKAGE, false, false, "N2",
        "an imported weapon (.oalasset); still loaded by file name, its ID is audit-only" },
    [HTA_RT_SOUNDS] = { "sounds", "sound pack", HTA_RT_SUPPORTED, HTA_RS_PACKAGE, false, false, "N2",
        "the UI sound pack: a container of role-named clips (.oalasset kind sounds); loaded by file name, its ID is audit-only -- one addressable clip is a 'sound'" },
    [HTA_RT_MODEL] = { "model", "model", HTA_RT_SUPPORTED, HTA_RS_DEFINITION, true, true, "X5",
        "a static mesh (mesh1) drawn with its material slots; a library provides it, a world's props place it" },
    [HTA_RT_MATERIAL] = { "material", "material", HTA_RT_SUPPORTED, HTA_RS_DEFINITION, true, true, "X5",
        "a surface: one texture and a draw mode (opaque, alpha); a model's slots name it" },
    [HTA_RT_TEXTURE] = { "texture", "texture", HTA_RT_SUPPORTED, HTA_RS_DEFINITION, true, true, "X5",
        "an RGBA8 image in a library package; a material names it" },
    [HTA_RT_SOUND] = { "sound", "sound", HTA_RT_SUPPORTED, HTA_RS_DEFINITION, true, true, "X5",
        "one clip of 16-bit PCM in a library package; a mover definition may name it (not the UI 'sounds' pack)" },
    [HTA_RT_ANIMATION] = { "animation", "animation", HTA_RT_RESERVED, HTA_RS_DEFINITION, false, false, "X4",
        "reserved: a clip for a skeleton" },
    [HTA_RT_PREFAB] = { "prefab", "prefab", HTA_RT_SUPPORTED, HTA_RS_DEFINITION, true, true, "X6",
        "a reusable composition of entity kinds and resources; a library provides it, a world places instances that expand into ordinary entities at load" },
    [HTA_RT_RULESET] = { "ruleset", "ruleset", HTA_RT_RESERVED, HTA_RS_DEFINITION, false, false, "X4",
        "reserved: game rules (team deathmatch...)" },
};

const hta_rtype_info *hta_rtype_get(uint8_t t) { return t > HTA_RT_NONE && t < HTA_RT_COUNT ? &TYPES[t] : NULL; }

uint8_t hta_rtype_find(const char *name, size_t len)
{
    for (uint8_t t = 1; name && t < HTA_RT_COUNT; t++)
        if (strlen(TYPES[t].name) == len && !memcmp(TYPES[t].name, name, len)) return t;
    return HTA_RT_NONE;
}

const char *const HTA_RID_RESERVED_NS[] = { "halo_trial", "megamod" };
const size_t HTA_RID_RESERVED_NS_COUNT = sizeof(HTA_RID_RESERVED_NS) / sizeof(HTA_RID_RESERVED_NS[0]);

bool hta_rid_reserved_namespace(const char *ns, size_t len)
{
    for (size_t i = 0; ns && i < HTA_RID_RESERVED_NS_COUNT; i++)
        if (strlen(HTA_RID_RESERVED_NS[i]) == len && !memcmp(HTA_RID_RESERVED_NS[i], ns, len)) return true;
    return false;
}

/* ---- the grammar --------------------------------------------------------- */

#if defined(__GNUC__)
static bool say(char *why, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#endif
static bool say(char *why, size_t n, const char *fmt, ...)
{
    if (why && n) { va_list a; va_start(a, fmt); vsnprintf(why, n, fmt, a); va_end(a); }
    return false;
}

/* A byte as it may be printed: itself if printable, else \xNN. */
static const char *shown(uint8_t c, char buf[8])
{
    if (c >= 0x21 && c < 0x7F) snprintf(buf, 8, "'%c'", c);
    else if (c == ' ') snprintf(buf, 8, "a space");
    else snprintf(buf, 8, "\\x%02x", c);
    return buf;
}

/* One segment [a-z][a-z0-9_]* of at most `max` bytes, or why not. */
static bool segment(const char *what, const char *s, size_t len, size_t max, char *why, size_t n)
{
    char b[8];
    if (!len) return say(why, n, "empty %s", what);
    if (len > max) return say(why, n, "%s longer than %zu bytes", what, max);
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)s[i];
        bool ok = (c >= 'a' && c <= 'z') || (i && ((c >= '0' && c <= '9') || c == '_'));
        if (ok) continue;
        if (c >= 'A' && c <= 'Z') return say(why, n, "%s has capital %s (IDs are lowercase; nothing is folded)", what, shown(c, b));
        if (!i) return say(why, n, "%s starts with %s, not a lowercase letter", what, shown(c, b));
        if (c == ':' || c == '/') return say(why, n, "%s has %s (':' and '/' each separate once)", what, shown(c, b));
        return say(why, n, "%s has %s, not [a-z0-9_]", what, shown(c, b));
    }
    return true;
}

int hta_rid_parse(const char *text, hta_rid *out, char *why, size_t n)
{
    hta_rid r = { 0 };
    r.text = text;
    if (out) *out = r;
    if (why && n) why[0] = 0;
    if (!text || !text[0]) { say(why, n, "empty ID"); return HTA_RID_MALFORMED; }
    size_t len = strnlen(text, HTA_RID_MAX + 1);
    if (len > HTA_RID_MAX) { say(why, n, "longer than %u bytes", HTA_RID_MAX); return HTA_RID_MALFORMED; }
    const char *colon = memchr(text, ':', len);
    if (!colon) { say(why, n, "no ':' (namespace:type/name)"); return HTA_RID_MALFORMED; }
    const char *slash = memchr(colon, '/', len - (size_t)(colon - text));
    if (!slash) { say(why, n, "no '/' after the type (namespace:type/name)"); return HTA_RID_MALFORMED; }
    size_t nl = (size_t)(colon - text), tl = (size_t)(slash - colon - 1), ml = len - (size_t)(slash - text) - 1;
    if (!segment("namespace", text, nl, HTA_RID_NS_MAX, why, n) ||
        !segment("type", colon + 1, tl, HTA_RID_TYPE_MAX, why, n) ||
        !segment("name", slash + 1, ml, HTA_RID_NAME_MAX, why, n)) return HTA_RID_MALFORMED;
    r.ns_len = (uint8_t)nl; r.type_len = (uint8_t)tl; r.name_len = (uint8_t)ml;
    r.type = hta_rtype_find(colon + 1, tl);
    if (out) *out = r;
    if (!r.type) { say(why, n, "unknown resource type '%.*s'", (int)tl, colon + 1); return HTA_RID_UNKNOWN_TYPE; }
    if (TYPES[r.type].status == HTA_RT_RESERVED) {
        say(why, n, "resource type '%s' is reserved, not loadable by this engine", TYPES[r.type].name);
        return HTA_RID_RESERVED_TYPE;
    }
    return HTA_RID_OK;
}

bool hta_rid_is(const char *text, uint8_t type)
{
    hta_rid r;
    return hta_rid_parse(text, &r, NULL, 0) == HTA_RID_OK && r.type == type;
}

bool hta_rid_same_namespace(const char *a, const char *b)
{
    const char *ca = a ? strchr(a, ':') : NULL, *cb = b ? strchr(b, ':') : NULL;
    return ca && cb && ca - a == cb - b && !memcmp(a, b, (size_t)(ca - a));
}

bool hta_package_id_valid(const char *id, char *why, size_t n)
{
    if (why && n) why[0] = 0;
    if (!id || !id[0]) return say(why, n, "empty package ID");
    size_t len = strnlen(id, HTA_PKG_ID_MAX + 1);
    if (len > HTA_PKG_ID_MAX) return say(why, n, "package ID longer than %u bytes", HTA_PKG_ID_MAX);
    unsigned segs = 0;
    for (size_t at = 0; at <= len; ) {
        const char *dot = memchr(id + at, '.', len - at);
        size_t sl = dot ? (size_t)(dot - (id + at)) : len - at;
        if (++segs > HTA_PKG_SEG_MAX) return say(why, n, "package ID has more than %u segments", HTA_PKG_SEG_MAX);
        if (!segment("package ID segment", id + at, sl, HTA_PKG_ID_MAX, why, n)) return false;
        at += sl + 1;
        if (!dot) break;
        if (at == len) return say(why, n, "package ID ends with '.'");
    }
    return true;
}

/* ---- reference fields ---------------------------------------------------- */

static const hta_ref_info REFS[HTA_REF_FIELD_COUNT] = {
    [HTA_REF_LINK_TARGET] = { "world_entities.entities[].links[].target", "link target", HTA_RT_ENTITY, HTA_REF_SELF, "X1",
        "the placement a link's input goes to; in the same world" },
    [HTA_REF_MOVER_DEF] = { "world_entities.entities[].definition", "definition", HTA_RT_MOVER, HTA_REF_SELF, "X2",
        "a placed mover's definition; in the same world" },
    [HTA_REF_SCRIPT] = { "world_entities.entities[].script", "script", HTA_RT_SCRIPT, HTA_REF_IMPORT, "X3",
        "an interactable's script (declares on_used); the world's own or imported (X4)" },
    [HTA_REF_ABILITY_SCRIPT] = { "world_entities.ability_script", "ability script", HTA_RT_SCRIPT, HTA_REF_IMPORT, "X3",
        "the script answering a player's ability press (declares on_ability); the world's own or imported (X4)" },
    [HTA_REF_LUA_ENTITY] = { "world.entity(id)", "world.entity", HTA_RT_ENTITY, HTA_REF_SELF, "X3",
        "a script asks for a placed entity's handle while it loads; in the world being played" },
    [HTA_REF_REQUIRE] = { "package.requires[].resources[]", "import", HTA_RT_NONE, HTA_REF_NAMED, "X4",
        "a resource a package takes from a package it requires; that package must provide it" },
    [HTA_REF_MATERIAL_TEXTURE] = { "assets.materials[].texture", "texture", HTA_RT_TEXTURE, HTA_REF_IMPORT, "X5",
        "the texture a material draws with; the library's own or imported" },
    [HTA_REF_MODEL_MATERIAL] = { "assets.models[].materials[]", "material slot", HTA_RT_MATERIAL, HTA_REF_IMPORT, "X5",
        "a model's material slots, in slot order; the library's own or imported" },
    [HTA_REF_PROP_MODEL] = { "world_entities.entities[].model", "model", HTA_RT_MODEL, HTA_REF_IMPORT, "X5",
        "the model a placed prop draws (and collides as its bounds); imported from a library" },
    [HTA_REF_MOVER_SOUND] = { "world_entities.mover_definitions[].sound", "sound", HTA_RT_SOUND, HTA_REF_IMPORT, "X5",
        "the sound a mover makes when it starts to open or close; imported from a library" },
    [HTA_REF_PREFAB_MODEL] = { "prefabs.prefabs[].children[].model", "model", HTA_RT_MODEL, HTA_REF_IMPORT, "X6",
        "a prefab child's model (a prop's, or what draws a mover); resolved from the prefab's own package: its own or imported" },
    [HTA_REF_PREFAB_SOUND] = { "prefabs.prefabs[].children[].sound", "sound", HTA_RT_SOUND, HTA_REF_IMPORT, "X6",
        "a prefab mover child's sound; resolved from the prefab's own package: its own or imported" },
    [HTA_REF_PREFAB_SCRIPT] = { "prefabs.prefabs[].children[].script", "script", HTA_RT_SCRIPT, HTA_REF_IMPORT, "X6",
        "a prefab interactable child's script (declares on_used); resolved from the prefab's own package: its own or imported" },
    [HTA_REF_PREFAB_INSTANCE] = { "world_entities.prefab_instances[].prefab", "prefab", HTA_RT_PREFAB, HTA_REF_IMPORT, "X6",
        "the prefab a world's instance places; imported from a library (the world needs nothing else the prefab uses)" },
};

const hta_ref_info *hta_ref_get(uint8_t f) { return f < HTA_REF_FIELD_COUNT ? &REFS[f] : NULL; }

/* ---- the set ------------------------------------------------------------- */

void hta_res_init(hta_res_set *s) { if (s) memset(s, 0, sizeof(*s)); }

const char *hta_res_provider_name(const hta_res_set *s, uint8_t p, char *buf, size_t n)
{
    if (s && p < s->provider_count && s->provider[p][0]) snprintf(buf, n, "package %s", s->provider[p]);
    else snprintf(buf, n, "%s", p ? "an unnamed package" : "this package");
    return buf;
}

static const char *article(const char *noun) { return strchr("aeiou", noun[0]) ? "an" : "a"; }

const hta_res_entry *hta_res_find(const hta_res_set *s, const char *id)
{
    for (uint32_t i = 0; s && id && i < s->count; i++) if (!strcmp(s->e[i].id, id)) return &s->e[i];
    return NULL;
}

bool hta_res_add(hta_res_set *s, const char *id, uint8_t type, uint8_t provider, uint16_t index, char *err, size_t n)
{
    if (!s || !id || strlen(id) > HTA_RID_MAX) return say(err, n, "resource ID out of range");
    const hta_res_entry *d = hta_res_find(s, id);
    if (d) {
        char a[HTA_PKG_ID_MAX + 16], b[HTA_PKG_ID_MAX + 16];
        if (d->provider == provider)
            return say(err, n, "%s: provided twice by %s", id, hta_res_provider_name(s, provider, a, sizeof(a)));
        return say(err, n, "%s: provided by both %s and %s (duplicate providers are refused, never picked)", id,
                   hta_res_provider_name(s, d->provider, a, sizeof(a)), hta_res_provider_name(s, provider, b, sizeof(b)));
    }
    if (s->count >= HTA_RES_MAX) return say(err, n, "more than %u resources in one package set", HTA_RES_MAX);
    hta_res_entry *e = &s->e[s->count++];
    memset(e, 0, sizeof(*e));
    memcpy(e->id, id, strlen(id) + 1);
    e->type = type; e->provider = provider; e->index = index;
    return true;
}

/* Another type's resource with the same namespace and name, for a hint. */
static const hta_res_entry *same_name(const hta_res_set *s, const hta_rid *r)
{
    const char *name = r->text + r->ns_len + 1 + r->type_len + 1;
    for (uint32_t i = 0; i < s->count; i++) {
        const char *id = s->e[i].id, *c = strchr(id, ':'), *sl = c ? strchr(c, '/') : NULL;
        if (sl && (size_t)(c - id) == r->ns_len && !memcmp(id, r->text, r->ns_len) && !strcmp(sl + 1, name) &&
            s->e[i].type != r->type) return &s->e[i];
    }
    return NULL;
}

/* Does anything in the set live in the reference's namespace? */
static bool same_namespace(const hta_res_set *s, const hta_rid *r)
{
    for (uint32_t i = 0; i < s->count; i++)
        if (!strncmp(s->e[i].id, r->text, r->ns_len) && s->e[i].id[r->ns_len] == ':') return true;
    return false;
}

const hta_res_entry *hta_res_resolve(const hta_res_set *s, uint8_t field, const char *who, const char *ref,
                                     char *err, size_t n)
{
    const hta_ref_info *f = hta_ref_get(field);
    if (!s || !f || !ref) { say(err, n, "bad reference"); return NULL; }
    const hta_rtype_info *want = hta_rtype_get(f->expects);
    char why[128], pa[HTA_PKG_ID_MAX + 16], pb[HTA_PKG_ID_MAX + 16];
    hta_rid r;
    int rc = hta_rid_parse(ref, &r, why, sizeof(why));
    if (rc == HTA_RID_MALFORMED) {
        say(err, n, "%s: %s '%s' is not a resource ID: %s (expected namespace:%s/name)", who, f->label, ref, why,
            want ? want->name : "type");
        return NULL;
    }
    if (rc != HTA_RID_OK) {
        say(err, n, "%s: %s '%s': %s (expected %s %s)", who, f->label, ref, why, want ? article(want->noun) : "an",
            want ? want->noun : "importable resource");
        return NULL;
    }
    const hta_res_entry *e = hta_res_find(s, ref);
    if (want && r.type != f->expects) {
        const hta_rtype_info *got = hta_rtype_get(r.type);
        if (e) say(err, n, "%s: %s %s is %s %s, expected %s %s", who, f->label, ref, article(got->noun), got->noun,
                   article(want->noun), want->noun);
        else say(err, n, "%s: %s '%s' is not %s %s ID (namespace:%s/name)", who, f->label, ref, article(want->noun),
                 want->noun, want->name);
        return NULL;
    }
    const hta_rtype_info *is = hta_rtype_get(r.type);
    if (!e) {
        const hta_res_entry *o = same_name(s, &r);
        char hint[HTA_RID_MAX + 48] = "";
        if (o) snprintf(hint, sizeof(hint), " (%s is %s %s)", o->id, article(hta_rtype_get(o->type)->noun),
                        hta_rtype_get(o->type)->noun);
        else if (f->from == HTA_REF_IMPORT && !same_namespace(s, &r))
            snprintf(hint, sizeof(hint), " (no package in this set provides namespace '%.*s': is a requirement missing?)",
                     (int)r.ns_len, r.text);
        say(err, n, "%s references missing %s %s%s", who, is->noun, ref, hint);
        return NULL;
    }
    if (e->provider == 0) return e;
    hta_res_provider_name(s, e->provider, pa, sizeof(pa));
    hta_res_provider_name(s, 0, pb, sizeof(pb));
    if (f->from == HTA_REF_SELF) {
        say(err, n, "%s: %s %s is provided by %s; a %s must be in the same package", who, f->label, ref, pa, f->label);
        return NULL;
    }
    bool required = false;
    for (uint32_t i = 0; i < s->import_count; i++) {
        if (s->imports[i].provider != e->provider) continue;
        required = true;
        if (!strcmp(s->imports[i].id, ref)) return e;
    }
    if (required || (e->provider < 32 && (s->required_mask >> e->provider) & 1u))
        say(err, n, "%s: %s %s is provided by %s, which %s requires but does not import it from (list it in requires[].resources)",
            who, f->label, ref, pa, pb);
    else say(err, n, "%s: %s %s is provided by %s, which %s does not require", who, f->label, ref, pa, pb);
    return NULL;
}
