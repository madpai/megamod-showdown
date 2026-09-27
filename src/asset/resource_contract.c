/* The resource contract, printed (resource.h): what megamod-resources
 * --json and --markdown show, and what Open Asset Lab keeps a copy of
 * (assetlab/data/megamod_resources.json). Everything comes from the tables
 * the loader itself uses -- the type registry, the reference fields, the
 * limits in package.h and world_def.h, the world key's member lists -- so
 * the contract cannot say one thing while the loader does another. */
#include "asset_res.h"
#include "external_map.h"
#include "package.h"
#include "resource.h"
#include "world_def.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct { char *buf; size_t cap, len; } out;

#if defined(__GNUC__)
static void put(out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
static void put(out *o, const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    int w = vsnprintf(o->buf && o->len < o->cap ? o->buf + o->len : NULL, o->buf && o->len < o->cap ? o->cap - o->len : 0, fmt, a);
    va_end(a);
    if (w > 0) o->len += (size_t)w;
}

static const char *scope_name(uint8_t s)
{
    return s == HTA_RS_PACKAGE ? "package" : s == HTA_RS_PLACEMENT ? "placement" : "definition";
}

static void referenced_by(out *o, uint8_t type)
{
    bool first = true;
    for (uint8_t f = 0; f < HTA_REF_FIELD_COUNT; f++) {
        const hta_ref_info *r = hta_ref_get(f);
        bool any = r->expects == HTA_RT_NONE && hta_rtype_get(type)->importable;
        if (r->expects != type && !any) continue;
        put(o, "%s\"%s\"", first ? "" : ", ", r->field);
        first = false;
    }
}

size_t hta_resource_contract_json(char *buf, size_t cap)
{
    out o = { buf, cap, 0 };
    if (buf && cap) buf[0] = 0;
    put(&o, "{\n  \"contract\": \"megamod.resources\",\n  \"version\": 1,\n");
    put(&o, "  \"id_grammar\": {\"version\": %u, \"form\": \"namespace:type/name\", \"segment\": \"[a-z][a-z0-9_]*\", "
            "\"limits\": {\"namespace\": %u, \"type\": %u, \"name\": %u, \"total\": %u}, "
            "\"case\": \"lowercase ASCII only; compared byte for byte; never folded\", "
            "\"normalization\": \"none: any other spelling is refused, never rewritten\", \"reserved_namespaces\": [",
        HTA_RID_GRAMMAR, HTA_RID_NS_MAX, HTA_RID_TYPE_MAX, HTA_RID_NAME_MAX, HTA_RID_MAX);
    for (size_t i = 0; i < HTA_RID_RESERVED_NS_COUNT; i++) put(&o, "%s\"%s\"", i ? ", " : "", HTA_RID_RESERVED_NS[i]);
    put(&o, "]},\n");
    put(&o, "  \"package_id_grammar\": {\"form\": \"segment(.segment)*\", \"segment\": \"[a-z][a-z0-9_]*\", "
            "\"max_bytes\": %u, \"max_segments\": %u},\n", HTA_PKG_ID_MAX, HTA_PKG_SEG_MAX);
    put(&o, "  \"types\": [\n");
    for (uint8_t t = 1; t < HTA_RT_COUNT; t++) {
        const hta_rtype_info *i = hta_rtype_get(t);
        put(&o, "    {\"name\": \"%s\", \"noun\": \"%s\", \"status\": \"%s\", \"scope\": \"%s\", \"runtime_resolved\": %s, "
                "\"importable\": %s, \"listed_in_provides\": %s, \"referenced_by\": [",
            i->name, i->noun, i->status == HTA_RT_SUPPORTED ? "supported" : "reserved", scope_name(i->scope),
            i->runtime ? "true" : "false", i->importable ? "true" : "false",
            i->scope != HTA_RS_PLACEMENT && i->status == HTA_RT_SUPPORTED ? "true" : "false");
        referenced_by(&o, t);
        put(&o, "], \"since\": \"%s\", \"doc\": \"%s\"}%s\n", i->since, i->doc, t + 1 < HTA_RT_COUNT ? "," : "");
    }
    put(&o, "  ],\n  \"references\": [\n");
    for (uint8_t f = 0; f < HTA_REF_FIELD_COUNT; f++) {
        const hta_ref_info *r = hta_ref_get(f);
        put(&o, "    {\"field\": \"%s\", \"label\": \"%s\", \"expects\": ", r->field, r->label);
        if (r->expects) put(&o, "\"%s\"", hta_rtype_get(r->expects)->name);
        else put(&o, "null");
        put(&o, ", \"resolves\": \"%s\", \"since\": \"%s\", \"doc\": \"%s\"}%s\n",
            r->from == HTA_REF_SELF ? "same package" : r->from == HTA_REF_NAMED ? "the required package it is listed under" :
            "same package, or an import from a required package",
            r->since, r->doc, f + 1 < HTA_REF_FIELD_COUNT ? "," : "");
    }
    put(&o, "  ],\n");
    put(&o, "  \"package\": {\"member\": \"package\", \"schema\": %u, \"kinds\": [\"world\", \"library\"], "
            "\"fields\": {\"id\": \"package ID\", \"schema\": %u, "
            "\"provides\": \"every resource the package defines except placements; canonical byte order; each once; must equal the content\", "
            "\"requires\": \"[{package, resources}] in canonical package-ID order; resources: the imports, canonical order, each provided by that package\"}, "
            "\"implicit\": \"a manifest with no package member (every pre-X4 package): provides what it defines, requires nothing\", "
            "\"library\": {\"container\": \"OALASSET v1, kind library: the manifest, then the payload of its asset members (X5), nothing else\", "
            "\"location\": \"%s/<package id>.oalasset\", "
            "\"provides\": \"its scripts and its assets, exactly\"}, "
            "\"graph\": \"package requirements are acyclic (a cycle is refused with its path); each package loads once; "
            "the set is kept sorted by package ID; references may only reach the package itself or its declared imports\", "
            "\"limits\": {\"provides\": %u, \"requires\": %u, \"imports\": %u, \"packages_per_set\": %u, \"depth\": %u, "
            "\"resources_per_set\": %u}},\n",
        HTA_PKG_SCHEMA, HTA_PKG_SCHEMA, HTA_PKG_LIBRARY_DIR, HTA_PKG_MAX_PROVIDES, HTA_PKG_MAX_REQUIRES,
        HTA_PKG_MAX_IMPORTS, HTA_PKG_MAX_SET, HTA_PKG_MAX_DEPTH, HTA_RES_MAX);
    put(&o, "  \"world_key\": {\"schema\": %u, \"played_members\": [", HTA_WORLD_KEY_SCHEMA);
    for (uint32_t i = 0; hta_world_key_played(i); i++) put(&o, "%s\"%s\"", i ? ", " : "", hta_world_key_played(i));
    put(&o, "], \"library_members\": [");
    for (uint32_t i = 0; hta_library_key_played(i); i++) put(&o, "%s\"%s\"", i ? ", " : "", hta_library_key_played(i));
    put(&o, "], \"dependencies\": \"after the members: per package of the closure, sorted by package ID: "
            "OALD, u32 ID length, ID, u64 digest (FNV-1a 64 of OALL, u32 schema, its library members; then, when it "
            "declares assets, OALP, u32 payload length and every payload byte)\"},\n");
    /* X5: package-backed asset resources (asset_res.h). */
    put(&o, "  \"assets\": {\"member\": \"assets\", \"schema\": %u, \"in\": \"library packages\", "
            "\"fields\": [\"materials\", \"members\", \"models\", \"schema\", \"sounds\", \"textures\"], "
            "\"members\": {\"entry\": {\"path\": \"member path\", \"size\": \"bytes, at least 1\"}, "
            "\"order\": \"canonical byte order of path, each once; the payload is the members' bytes in this order, right after the manifest\", "
            "\"use\": \"every member backs exactly one resource; every resource's member exists\"}, "
            "\"member_path\": {\"form\": \"segment(/segment)*, the last with one extension: name.ext\", \"segment\": \"[a-z0-9_]+\", "
            "\"max_bytes\": %u, \"max_segments\": %u, \"identity\": \"none: storage inside the package; never a host path, never a resource ID\"}, "
            "\"types\": {"
            "\"texture\": {\"fields\": [\"format\", \"height\", \"id\", \"member\", \"width\"], \"formats\": [\"rgba8\"], \"max_side\": %u, "
            "\"payload\": \"width x height x 4 bytes, rows top down\"}, "
            "\"material\": {\"fields\": [\"draw\", \"id\", \"texture\"], \"draw\": [\"opaque\", \"alpha\"], \"references\": [\"assets.materials[].texture\"]}, "
            "\"model\": {\"fields\": [\"format\", \"id\", \"materials\", \"member\"], \"formats\": [\"mesh1\"], \"max_slots\": %u, "
            "\"max_vertices\": %u, \"max_indices\": %u, \"max_groups\": %u, \"references\": [\"assets.models[].materials[]\"], "
            "\"payload\": \"MSH1, u32 vertex, index, group counts; vertices of 10 f32 (position, normal, uv, lightmap uv; |v| <= 4096); "
            "u32 indices; groups of u32 first, count, material slot, contiguous from 0\"}, "
            "\"sound\": {\"fields\": [\"channels\", \"format\", \"frames\", \"id\", \"member\", \"rate\"], \"formats\": [\"pcm_s16le\"], "
            "\"rate\": [4000, 96000], \"channels\": [1, 2], \"max_frames\": %u, \"payload\": \"frames x channels x 2 bytes, interleaved, little endian\"}}, "
            "\"limits\": {\"per_type\": %u, \"members\": %u, \"payload_bytes\": %u}, "
            "\"lists\": \"canonical byte order of id, each once; every descriptor field required, nothing else allowed\", "
            "\"runtime\": \"decoded once when the package set loads into one table (by package ID, then resource ID, per type); "
            "placements hold indices; a resource imported by several consumers or placed many times exists once\"},\n",
        HTA_ASSET_SCHEMA, HTA_ASSET_MEMBER_MAX, HTA_ASSET_MEMBER_SEGS, HTA_ASSET_TEX_MAX, HTA_ASSET_MAX_SLOTS,
        HTA_ASSET_MESH_MAX_VERTS, HTA_ASSET_MESH_MAX_INDICES, HTA_ASSET_MESH_MAX_GROUPS, HTA_ASSET_SOUND_MAX_FRAMES,
        HTA_ASSET_MAX_PER_TYPE, HTA_ASSET_MAX_MEMBERS, HTA_ASSET_MAX_PAYLOAD);
    put(&o, "  \"world_entities\": {\"schema\": %u, \"kinds\": [", HTA_WDEF_SCHEMA);
    for (uint8_t k = 1; k < HTA_WDEF_KIND_COUNT; k++) put(&o, "%s\"%s\"", k > 1 ? ", " : "", hta_wdef_kind_name(k));
    put(&o, "], \"prop\": \"schema 4: {id, kind prop, links [], model, position}: draws its model where it stands, solid as the "
            "model's bounds\", \"mover_sound\": \"schema 4: a mover definition's optional sound, played when a mover starts to open or close\"},\n");
    put(&o, "  \"scripts\": {\"api\": \"%s\", \"max_scripts\": %u, \"max_source_bytes\": %u, \"max_pool_bytes\": %u}\n}\n",
        HTA_WDEF_SCRIPT_API, HTA_WDEF_MAX_SCRIPTS, HTA_WDEF_SCRIPT_MAX_BYTES, HTA_WDEF_SCRIPT_POOL);
    return o.len;
}

size_t hta_resource_types_markdown(char *buf, size_t cap)
{
    out o = { buf, cap, 0 };
    if (buf && cap) buf[0] = 0;
    put(&o, "| Type | Noun | Status | Scope | Resolved at load | Importable | Since | What |\n");
    put(&o, "|---|---|---|---|---|---|---|---|\n");
    for (uint8_t t = 1; t < HTA_RT_COUNT; t++) {
        const hta_rtype_info *i = hta_rtype_get(t);
        put(&o, "| `%s` | %s | %s | %s | %s | %s | %s | %s |\n", i->name, i->noun,
            i->status == HTA_RT_SUPPORTED ? "supported" : "reserved", scope_name(i->scope), i->runtime ? "yes" : "-",
            i->importable ? "yes" : "-", i->since, i->doc);
    }
    put(&o, "\n| Reference field | Expects | Resolves to | Since |\n|---|---|---|---|\n");
    for (uint8_t f = 0; f < HTA_REF_FIELD_COUNT; f++) {
        const hta_ref_info *r = hta_ref_get(f);
        put(&o, "| `%s` | %s | %s | %s |\n", r->field, r->expects ? hta_rtype_get(r->expects)->noun : "any importable type",
            r->from == HTA_REF_SELF ? "the same package" : r->from == HTA_REF_NAMED ? "the required package it is listed under" :
            "the same package or a declared import", r->since);
    }
    return o.len;
}

/* The conformance corpus: resource and package IDs with the engine's own
 * verdict on each, printed live. Open Asset Lab keeps a copy and checks
 * its grammar gives the same verdict and the same words for every one
 * (assetlab/data/megamod_id_conformance.json); scripts/test_x4.sh checks
 * the copy is current. Add a case here when a grammar question comes up. */
static const char *const RID_CASES[] = {
    "x3:script/button_logic", "x1:entity/door_main", "x2:mover/basic_slide_door", "x4shared:script/pulse_ability",
    "lab_demo:world/relay_test", "owner:character/goku", "a:sounds/b", "a1_:script/b__2_", "showdown:script/door_controller",
    "n234567890123456789012345678901234567890:script/m23456789012345678901234567890123456789012345678",
    "n2345678901234567890123456789012345678901:script/a", "a:script/m234567890123456789012345678901234567890123456789",
    "n234567890123456789012345678901234567890:script/m234567890123456789012345678901234567890123456789",
    "", "script/door", "x4:script", ":script/door", "x4:/door", "x4:script/", "X4:script/door", "x4:Script/door",
    "x4:script/Door", "x4:script/door-logic", "x4:script/door.lua", "x4:script/door logic", " x4:script/door",
    "x4:script/door ", "x4:script/door\t", "x4:script//door", "x4::script/door", "x4:script/a/b", "x4:script/../door",
    "x4:script:x/door", "4x:script/door", "_x:script/door", "x4:script/_door", "x4:script/9mm", "x4:script/d\xc3\xb6r",
    "x4:widget/door", "x4:scripts/door", "x4:Entity/a", "showdown:model/red_crate", "common:material/industrial_metal",
    "x:texture/t", "x:sound/s", "community:animation/rifle_run", "showdown:prefab/security_door",
    "showdown:ruleset/team_deathmatch", "megamod:script/x", "x4:script/a%s", "x4:script/a\\b", "x4:script/a\"b",
    "x5shared:model/test_crate", "x5shared:material/test_crate", "x5shared:texture/test_crate", "x5shared:sound/test_impact",
    "x5shared:model/models/test_crate", "x5shared:model/test_crate.mesh", "x5shared:Model/test_crate", "x5shared:sounds/impact",
};
static const char *const PKG_CASES[] = {
    "x4.resource_lab", "x4.shared", "showdown", "a.b.c.d.e.f.g.h", "common.gameplay_scripts2", "", "x4:resource_lab",
    "x4/lab", "X4.lab", "x4..lab", ".x4", "x4.", "showdown-industrial-pack", "a.b.c.d.e.f.g.h.i", "../etc", "x4.Lab",
    "x4.1lab", "x4.lab ", "pppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppp",
    "ppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppp",
};

/* Member paths (X5, asset_res.h): package-local storage, checked by the
 * same rules on both sides. */
static const char *const PATH_CASES[] = {
    "models/test_crate.mesh", "textures/test_crate.rgba", "sounds/test_impact.pcm", "a.b", "a/b/c/d/e/f.x",
    "a/b/c/d/e/f/g.x", "", "/models/a.mesh", "../a.mesh", "models/../a.mesh", "models/./a.mesh", "models//a.mesh",
    "models\\a.mesh", "Models/a.mesh", "models/A.mesh", "models/a", "models/a.", "models/.mesh", "models/a.b.c",
    "models.x/a.mesh", "models/a-b.mesh", "models/a b.mesh", "models/a.mesh/", "C:/a.mesh", "models/a\xc3\xa9.mesh",
    "models/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.mesh",
    "models/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.mesh", "models/a:b.mesh", "%2e%2e/a.mesh",
};

static void jstr(out *o, const char *s)
{
    put(o, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') put(o, "\\%c", c);
        else if (c < 0x20 || c >= 0x7F) put(o, "\\u%04x", c);
        else put(o, "%c", c);
    }
    put(o, "\"");
}

/* A case as written above: C escapes \t, \xNN, \\, \" decoded. */
static void unescape(const char *in, char *outb, size_t cap)
{
    size_t n = 0;
    for (; *in && n + 1 < cap; in++) {
        if (*in == '\\' && in[1] == 't') { outb[n++] = '\t'; in++; }
        else if (*in == '\\' && in[1] == 'x' && in[2] && in[3]) {
            unsigned v = 0;
            for (int k = 2; k < 4; k++) v = v * 16 + (unsigned)(in[k] <= '9' ? in[k] - '0' : in[k] - 'a' + 10);
            outb[n++] = (char)v; in += 3;
        } else if (*in == '\\' && (in[1] == '\\' || in[1] == '"')) { outb[n++] = in[1]; in++; }
        else outb[n++] = *in;
    }
    outb[n] = 0;
}

size_t hta_resource_conformance_json(char *buf, size_t cap)
{
    static const char *const CODE[] = { "ok", "malformed", "unknown_type", "reserved_type" };
    out o = { buf, cap, 0 };
    if (buf && cap) buf[0] = 0;
    put(&o, "{\n  \"grammar\": %u,\n  \"resource_ids\": [\n", HTA_RID_GRAMMAR);
    for (size_t i = 0; i < sizeof(RID_CASES) / sizeof(RID_CASES[0]); i++) {
        char id[256], why[160];
        unescape(RID_CASES[i], id, sizeof(id));
        hta_rid r;
        int rc = hta_rid_parse(id, &r, why, sizeof(why));
        put(&o, "    {\"id\": ");
        jstr(&o, id);
        put(&o, ", \"result\": \"%s\", \"type\": ", CODE[rc]);
        if (r.type) put(&o, "\"%s\"", hta_rtype_get(r.type)->name); else put(&o, "null");
        put(&o, ", \"reserved_namespace\": %s, \"why\": ", rc == HTA_RID_OK && hta_rid_reserved_namespace(id, r.ns_len) ? "true" : "false");
        jstr(&o, why);
        put(&o, "}%s\n", i + 1 < sizeof(RID_CASES) / sizeof(RID_CASES[0]) ? "," : "");
    }
    put(&o, "  ],\n  \"package_ids\": [\n");
    for (size_t i = 0; i < sizeof(PKG_CASES) / sizeof(PKG_CASES[0]); i++) {
        char why[160];
        bool ok = hta_package_id_valid(PKG_CASES[i], why, sizeof(why));
        put(&o, "    {\"id\": ");
        jstr(&o, PKG_CASES[i]);
        put(&o, ", \"valid\": %s, \"why\": ", ok ? "true" : "false");
        jstr(&o, why);
        put(&o, "}%s\n", i + 1 < sizeof(PKG_CASES) / sizeof(PKG_CASES[0]) ? "," : "");
    }
    put(&o, "  ],\n  \"member_paths\": [\n");
    for (size_t i = 0; i < sizeof(PATH_CASES) / sizeof(PATH_CASES[0]); i++) {
        char p[256], why[160];
        unescape(PATH_CASES[i], p, sizeof(p));
        bool ok = hta_asset_member_valid(p, why, sizeof(why));
        put(&o, "    {\"path\": ");
        jstr(&o, p);
        put(&o, ", \"valid\": %s, \"why\": ", ok ? "true" : "false");
        jstr(&o, why);
        put(&o, "}%s\n", i + 1 < sizeof(PATH_CASES) / sizeof(PATH_CASES[0]) ? "," : "");
    }
    put(&o, "  ]\n}\n");
    return o.len;
}
