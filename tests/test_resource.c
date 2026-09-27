/* X4: resource identity and dependencies (asset/resource.h, package.h;
 * docs/RESOURCES.md). The grammar, the registry, typed resolution, the
 * package declaration, library packages, the package graph, a world that
 * imports scripts, the world key over dependencies, hostile input, and the
 * contract docs/RESOURCES.md prints. */
#include "asset/external_map.h"
#include "asset/package.h"
#include "asset/resource.h"
#include "asset/world_def.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void u32(unsigned char *p, uint32_t x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); p[2] = (uint8_t)(x >> 16); p[3] = (uint8_t)(x >> 24); }
static void f32(unsigned char *p, float v) { uint32_t x; memcpy(&x, &v, 4); u32(p, x); }

/* ---- the grammar ------------------------------------------------------------ */

static void ids(void)
{
    char why[160];
    hta_rid r;
    static const char *const good[] = {
        "x3:script/button_logic", "x1:entity/door_main", "x2:mover/basic_slide_door", "lab_demo:world/relay_test",
        "owner:character/goku", "megamod2:weapon/ion_rifle", "a:sounds/b", "a1_:script/b__2_",
    };
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        int rc = hta_rid_parse(good[i], &r, why, sizeof(why));
        if (rc != HTA_RID_OK) fprintf(stderr, "%s: %s\n", good[i], why);
        CHECK(rc == HTA_RID_OK);
    }
    CHECK(hta_rid_parse("x3:script/button_logic", &r, NULL, 0) == HTA_RID_OK && r.type == HTA_RT_SCRIPT &&
          r.ns_len == 2 && r.type_len == 6 && r.name_len == 12);
    /* Edge lengths: namespace 40, name 48, whole 96 exactly. */
    char edge[128];
    snprintf(edge, sizeof(edge), "%s:script/%s", "n234567890123456789012345678901234567890",
             "m23456789012345678901234567890123456789012345678");
    CHECK(strlen(edge) == 96 && hta_rid_parse(edge, &r, why, sizeof(why)) == HTA_RID_OK);
    static const struct { const char *id; int rc; const char *why; } bad[] = {
        { "", HTA_RID_MALFORMED, "empty ID" },
        { "script/door", HTA_RID_MALFORMED, "no ':'" },
        { "x4:script", HTA_RID_MALFORMED, "no '/'" },
        { ":script/door", HTA_RID_MALFORMED, "empty namespace" },
        { "x4:/door", HTA_RID_MALFORMED, "empty type" },
        { "x4:script/", HTA_RID_MALFORMED, "empty name" },
        { "X4:script/door", HTA_RID_MALFORMED, "namespace has capital 'X'" },
        { "x4:Script/door", HTA_RID_MALFORMED, "type has capital 'S'" },
        { "x4:script/Door", HTA_RID_MALFORMED, "name has capital 'D'" },
        { "x4:script/door-logic", HTA_RID_MALFORMED, "name has '-'" },
        { "x4:script/door.lua", HTA_RID_MALFORMED, "name has '.'" },
        { "x4:script/door logic", HTA_RID_MALFORMED, "name has a space" },
        { " x4:script/door", HTA_RID_MALFORMED, "namespace starts with a space" },
        { "x4:script/door ", HTA_RID_MALFORMED, "name has a space" },
        { "x4:script/door\t", HTA_RID_MALFORMED, "name has \\x09" },
        { "x4:script//door", HTA_RID_MALFORMED, "name starts with '/'" },
        { "x4::script/door", HTA_RID_MALFORMED, "type starts with ':'" },
        { "x4:script/a/b", HTA_RID_MALFORMED, "name has '/'" },
        { "x4:script/../door", HTA_RID_MALFORMED, "name starts with '.'" },
        { "x4:script:x/door", HTA_RID_MALFORMED, "type has ':'" },
        { "4x:script/door", HTA_RID_MALFORMED, "namespace starts with '4'" },
        { "_x:script/door", HTA_RID_MALFORMED, "namespace starts with '_'" },
        { "x4:script/_door", HTA_RID_MALFORMED, "name starts with '_'" },
        { "x4:script/d\xc3\xb6r", HTA_RID_MALFORMED, "name has \\xc3" },
        { "x4:widget/door", HTA_RID_UNKNOWN_TYPE, "unknown resource type 'widget'" },
        { "x4:scripts/door", HTA_RID_UNKNOWN_TYPE, "unknown resource type 'scripts'" },
        { "showdown:ruleset/security", HTA_RID_RESERVED_TYPE, "resource type 'ruleset' is reserved" },
        { "common:animation/rifle_run", HTA_RID_RESERVED_TYPE, "reserved" },
        { "showdown:model/red_crate", HTA_RID_OK, "" },            /* X5: supported */
        { "common:material/industrial_metal", HTA_RID_OK, "" },
        { "showdown:prefab/security_door", HTA_RID_OK, "" },      /* X6: supported */
        { "showdown:ruleset/team_deathmatch", HTA_RID_RESERVED_TYPE, "reserved" },
        { "community:animation/rifle_run", HTA_RID_RESERVED_TYPE, "reserved" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        int rc = hta_rid_parse(bad[i].id, &r, why, sizeof(why));
        if (rc != bad[i].rc || !strstr(why, bad[i].why)) {
            fprintf(stderr, "id '%s': rc %d, why '%s' (wanted %d, '%s')\n", bad[i].id, rc, why, bad[i].rc, bad[i].why);
            failures++;
        }
    }
    /* Over-long parts, and a whole ID one byte over. */
    snprintf(edge, sizeof(edge), "%s:script/a", "n2345678901234567890123456789012345678901");
    CHECK(hta_rid_parse(edge, &r, why, sizeof(why)) == HTA_RID_MALFORMED && strstr(why, "namespace longer than 40"));
    snprintf(edge, sizeof(edge), "a:script/%s", "m234567890123456789012345678901234567890123456789");
    CHECK(hta_rid_parse(edge, &r, why, sizeof(why)) == HTA_RID_MALFORMED && strstr(why, "name longer than 48"));
    snprintf(edge, sizeof(edge), "%s:script/%s", "n234567890123456789012345678901234567890",
             "m234567890123456789012345678901234567890123456789");
    CHECK(strlen(edge) == 97 && hta_rid_parse(edge, &r, why, sizeof(why)) == HTA_RID_MALFORMED && strstr(why, "longer than 96"));
    char huge[4096];
    memset(huge, 'a', sizeof(huge) - 1); huge[sizeof(huge) - 1] = 0;
    CHECK(hta_rid_parse(huge, &r, why, sizeof(why)) == HTA_RID_MALFORMED);
    CHECK(hta_rid_parse(NULL, &r, why, sizeof(why)) == HTA_RID_MALFORMED);
    CHECK(hta_rid_is("x1:entity/a", HTA_RT_ENTITY) && !hta_rid_is("x1:entity/a", HTA_RT_SCRIPT) &&
          hta_rid_is("x1:model/a", HTA_RT_MODEL) && !hta_rid_is("x1:model/a", HTA_RT_MATERIAL) &&
          !hta_rid_is("x1:ruleset/a", HTA_RT_RULESET) && hta_rid_is("x1:prefab/a", HTA_RT_PREFAB));
    CHECK(hta_rid_same_namespace("x4:script/a", "x4:entity/b") && !hta_rid_same_namespace("x4:script/a", "x44:script/a"));

    /* Package IDs: a grammar of their own, never a resource ID. */
    static const char *const pgood[] = { "x4.resource_lab", "showdown", "a.b.c.d.e.f.g.h", "common.gameplay_scripts2" };
    for (size_t i = 0; i < sizeof(pgood) / sizeof(pgood[0]); i++) CHECK(hta_package_id_valid(pgood[i], why, sizeof(why)));
    static const struct { const char *id, *why; } pbad[] = {
        { "", "empty" }, { "x4:resource_lab", "':'" }, { "x4/lab", "'/'" }, { "X4.lab", "capital" },
        { "x4..lab", "empty package ID segment" }, { ".x4", "empty" }, { "x4.", "ends with '.'" },
        { "showdown-industrial-pack", "'-'" }, { "a.b.c.d.e.f.g.h.i", "more than 8 segments" }, { "../etc", "empty package ID segment" },
        { "x4.Lab", "capital" }, { "x4.1lab", "starts with '1'" },
    };
    for (size_t i = 0; i < sizeof(pbad) / sizeof(pbad[0]); i++) {
        bool ok = hta_package_id_valid(pbad[i].id, why, sizeof(why));
        if (ok || !strstr(why, pbad[i].why)) { fprintf(stderr, "package id '%s': '%s'\n", pbad[i].id, why); failures++; }
    }
    char plong[80];
    memset(plong, 'p', 65); plong[65] = 0;
    CHECK(!hta_package_id_valid(plong, why, sizeof(why)) && strstr(why, "longer than 64"));
    puts("  ids: grammar, edge lengths, every malformed shape, reserved types, package IDs: ok");
}

static void registry(void)
{
    unsigned supported = 0, reserved = 0, importable = 0;
    for (uint8_t t = 1; t < HTA_RT_COUNT; t++) {
        const hta_rtype_info *i = hta_rtype_get(t);
        CHECK(i && i->name && i->noun && i->since && i->doc);
        CHECK(hta_rtype_find(i->name, strlen(i->name)) == t);
        char id[64];
        snprintf(id, sizeof(id), "ns:%s/n", i->name);
        CHECK(hta_rid_parse(id, NULL, NULL, 0) == (i->status == HTA_RT_SUPPORTED ? HTA_RID_OK : HTA_RID_RESERVED_TYPE));
        supported += i->status == HTA_RT_SUPPORTED; reserved += i->status == HTA_RT_RESERVED;
        importable += i->importable;
        CHECK(!(i->importable && i->status != HTA_RT_SUPPORTED));
        CHECK(!(i->importable && i->scope == HTA_RS_PLACEMENT));
    }
    /* X5: model, material, texture and sound are supported and importable;
     * X6: so is prefab. animation and ruleset stay reserved. */
    CHECK(supported == 12 && reserved == 2 && importable == 6 && hta_rtype_get(HTA_RT_SCRIPT)->importable &&
          hta_rtype_get(HTA_RT_MODEL)->importable && hta_rtype_get(HTA_RT_SOUND)->importable &&
          !hta_rtype_get(HTA_RT_SOUNDS)->importable && hta_rtype_get(HTA_RT_PREFAB)->status == HTA_RT_SUPPORTED &&
          hta_rtype_get(HTA_RT_PREFAB)->importable && hta_rtype_get(HTA_RT_RULESET)->status == HTA_RT_RESERVED);
    CHECK(!hta_rtype_get(0) && !hta_rtype_get(HTA_RT_COUNT) && hta_rtype_find("", 0) == HTA_RT_NONE);
    for (uint8_t f = 0; f < HTA_REF_FIELD_COUNT; f++) {
        const hta_ref_info *r = hta_ref_get(f);
        CHECK(r && r->field && r->label && (r->from == HTA_REF_SELF || r->from == HTA_REF_IMPORT || r->from == HTA_REF_NAMED));
        CHECK(!r->expects || hta_rtype_get(r->expects)->runtime);
    }
    CHECK(hta_rid_reserved_namespace("megamod", 7) && hta_rid_reserved_namespace("halo_trial", 10) &&
          !hta_rid_reserved_namespace("megamod2", 8));
    puts("  registry: every type round-trips, five importable types (script + X5 assets), reference fields well formed: ok");
}

/* ---- typed resolution ------------------------------------------------------- */

static bool resolve_fails(const hta_res_set *s, uint8_t field, const char *ref, const char *want)
{
    char err[400];
    const hta_res_entry *e = hta_res_resolve(s, field, "x4:entity/button", ref, err, sizeof(err));
    if (e || !strstr(err, want)) {
        fprintf(stderr, "resolve '%s': %s (wanted '%s')\n", ref, e ? "resolved" : err, want);
        return false;
    }
    return true;
}

static void resolution(void)
{
    static hta_res_set s;
    char err[400];
    hta_res_init(&s);
    s.provider_count = 3;
    strcpy(s.provider[0], "x4.resource_lab"); strcpy(s.provider[1], "x4.shared"); strcpy(s.provider[2], "x4.other");
    CHECK(hta_res_add(&s, "x4:entity/door", HTA_RT_ENTITY, 0, 3, err, sizeof(err)));
    CHECK(hta_res_add(&s, "x4:script/open_door", HTA_RT_SCRIPT, 0, 0, err, sizeof(err)));
    CHECK(hta_res_add(&s, "x4:mover/door", HTA_RT_MOVER, 0, 1, err, sizeof(err)));
    CHECK(hta_res_add(&s, "x4shared:script/pulse", HTA_RT_SCRIPT, 1, 0, err, sizeof(err)));
    CHECK(hta_res_add(&s, "x4shared:script/other", HTA_RT_SCRIPT, 1, 1, err, sizeof(err)));
    CHECK(hta_res_add(&s, "x4other:script/pulse", HTA_RT_SCRIPT, 2, 0, err, sizeof(err)));
    /* Duplicates are refused and name both providers; nothing is picked. */
    CHECK(!hta_res_add(&s, "x4shared:script/pulse", HTA_RT_SCRIPT, 0, 1, err, sizeof(err)) &&
          strstr(err, "x4shared:script/pulse: provided by both package x4.shared and package x4.resource_lab"));
    CHECK(!hta_res_add(&s, "x4:entity/door", HTA_RT_ENTITY, 0, 4, err, sizeof(err)) && strstr(err, "provided twice by package x4.resource_lab"));
    hta_res_import imp[] = { { "x4shared:script/pulse", 1 } };
    s.imports = imp; s.import_count = 1; s.required_mask = 1u << 1;
    const hta_res_entry *e;
    /* script -> script, entity -> entity, mover -> mover. */
    e = hta_res_resolve(&s, HTA_REF_SCRIPT, "x4:entity/button", "x4:script/open_door", err, sizeof(err));
    CHECK(e && e->provider == 0 && e->index == 0);
    e = hta_res_resolve(&s, HTA_REF_LINK_TARGET, "x4:entity/button", "x4:entity/door", err, sizeof(err));
    CHECK(e && e->index == 3);
    e = hta_res_resolve(&s, HTA_REF_MOVER_DEF, "x4:entity/door", "x4:mover/door", err, sizeof(err));
    CHECK(e && e->index == 1);
    /* An import: resolves to its provider. */
    e = hta_res_resolve(&s, HTA_REF_ABILITY_SCRIPT, "ability_script", "x4shared:script/pulse", err, sizeof(err));
    CHECK(e && e->provider == 1 && e->index == 0);
    /* Refusals. */
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4:mover/door", "script x4:mover/door is a mover definition, expected a script"));
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4:entity/door", "script x4:entity/door is a placed entity, expected a script"));
    CHECK(resolve_fails(&s, HTA_REF_LINK_TARGET, "x4:script/open_door", "link target x4:script/open_door is a script, expected a placed entity"));
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4:weapon/door", "script 'x4:weapon/door' is not a script ID (namespace:script/name)"));
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4:ruleset/door", "resource type 'ruleset' is reserved"));
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4:model/door", "x4:entity/button: script 'x4:model/door' is not a script ID"));
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4:gizmo/door", "unknown resource type 'gizmo'"));
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "X4:script/Open", "is not a resource ID: namespace has capital 'X'"));
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "", "is not a resource ID: empty ID"));
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4:script/opne_door", "x4:entity/button references missing script x4:script/opne_door"));
    /* Same namespace and name, another type: a hint, never a substitution. */
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4:script/door", "references missing script x4:script/door (x4:entity/door is a placed entity)"));
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4shared:script/other",
                        "is provided by package x4.shared, which package x4.resource_lab requires but does not import it from"));
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4other:script/pulse",
                        "is provided by package x4.other, which package x4.resource_lab does not require"));
    /* Same-package fields never reach another package. */
    CHECK(hta_res_add(&s, "x4shared:entity/far", HTA_RT_ENTITY, 1, 0, err, sizeof(err)));
    CHECK(resolve_fails(&s, HTA_REF_LINK_TARGET, "x4shared:entity/far", "is provided by package x4.shared; a link target must be in the same package"));
    /* Resolution is by exact bytes: no folding, no nearest match. */
    CHECK(resolve_fails(&s, HTA_REF_SCRIPT, "x4:script/open_door ", "is not a resource ID"));
    /* A full set. */
    static hta_res_set big;
    hta_res_init(&big);
    for (uint32_t i = 0; i < HTA_RES_MAX; i++) {
        char id[40];
        snprintf(id, sizeof(id), "t:script/s%u", i);
        CHECK(hta_res_add(&big, id, HTA_RT_SCRIPT, 0, (uint16_t)i, err, sizeof(err)));
    }
    CHECK(!hta_res_add(&big, "t:script/one_more", HTA_RT_SCRIPT, 0, 0, err, sizeof(err)) && strstr(err, "more than 512"));
    clock_t t0 = clock();
    for (uint32_t i = 0; i < HTA_RES_MAX; i++) {
        char id[40];
        snprintf(id, sizeof(id), "t:script/s%u", i);
        e = hta_res_resolve(&big, HTA_REF_SCRIPT, "who", id, err, sizeof(err));
        CHECK(e && e->index == i);
    }
    printf("  resolution: typed, imports, hints, duplicates; %u resolutions in a full set in %.2f ms: ok\n", HTA_RES_MAX,
           (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
}

/* ---- package declarations --------------------------------------------------- */

static bool decl_fails(const char *manifest, const char *want)
{
    static hta_package p;
    char err[400];
    bool ok = hta_package_parse((const uint8_t *)manifest, strlen(manifest), HTA_PKG_WORLD, &p, err, sizeof(err));
    if (ok || !strstr(err, want)) { fprintf(stderr, "decl: %s (wanted '%s')\n", ok ? "accepted" : err, want); return false; }
    return true;
}

static void declarations(void)
{
    static hta_package p;
    char err[400];
    const char *m = "{\"id\":\"x4:world/lab\",\"package\":{\"id\":\"x4.lab\",\"provides\":[\"x4:script/a\",\"x4:world/lab\"],"
                    "\"requires\":[{\"package\":\"x4.a\",\"resources\":[]},{\"package\":\"x4.b\",\"resources\":[\"b:script/x\",\"b:script/y\"]}],"
                    "\"schema\":1},\"zzz\":1e999}";
    CHECK(hta_package_parse((const uint8_t *)m, strlen(m), HTA_PKG_WORLD, &p, err, sizeof(err)));
    CHECK(p.declared && !strcmp(p.id, "x4.lab") && !strcmp(p.content_id, "x4:world/lab") && p.provide_count == 2 &&
          p.require_count == 2 && p.import_count == 2 && p.requires[1].first == 0 && p.requires[1].count == 2 &&
          p.provides[1].type == HTA_RT_WORLD && hta_package_provides(&p, "x4:script/a"));
    const char *none = "{\"id\":\"x3:world/lab\",\"namespace\":\"x3\"}";
    CHECK(hta_package_parse((const uint8_t *)none, strlen(none), HTA_PKG_WORLD, &p, err, sizeof(err)) && !p.declared &&
          !strcmp(p.content_id, "x3:world/lab"));
#define P(body) "{\"package\":{" body "}}"
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[],\"requires\":[],\"schema\":2"), "unsupported package schema 2 (this engine has 1)"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[],\"requires\":[]"), "needs id, schema, provides and requires"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[],\"requires\":[],\"schema\":1,\"version\":\"1.0\""), "unknown field 'version'"));
    CHECK(decl_fails(P("\"id\":\"A\",\"provides\":[],\"requires\":[],\"schema\":1"), "package 'A': package ID segment has capital"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[\"a:script/b\",\"a:script/a\"],\"requires\":[],\"schema\":1"), "not in canonical (byte) order at a:script/a"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[\"a:script/a\",\"a:script/a\"],\"requires\":[],\"schema\":1"), "lists a:script/a twice"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[\"a:entity/door\"],\"requires\":[],\"schema\":1"), "placed entity is a placement"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[\"a:ruleset/crate\"],\"requires\":[],\"schema\":1"), "resource type 'ruleset' is reserved"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[\"megamod:script/x\"],\"requires\":[],\"schema\":1"), "namespace 'megamod' is reserved"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[\"a:Script/x\"],\"requires\":[],\"schema\":1"), "type has capital 'S'"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[],\"requires\":[{\"package\":\"b\",\"resources\":[\"b:mover/door\"]}],\"schema\":1"),
                     "mover definition cannot be imported"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[],\"requires\":[{\"package\":\"c\",\"resources\":[]},{\"package\":\"b\",\"resources\":[]}],\"schema\":1"),
                     "requires is not in canonical (package ID) order at b"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[],\"requires\":[{\"package\":\"b\",\"resources\":[]},{\"package\":\"b\",\"resources\":[]}],\"schema\":1"),
                     "requires package b twice"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[],\"requires\":[{\"package\":\"a\",\"resources\":[]}],\"schema\":1"), "package a: requires itself"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[],\"requires\":[{\"package\":\"b:c\",\"resources\":[]}],\"schema\":1"), "requires 'b:c'"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[],\"requires\":[{\"package\":\"b\"}],\"schema\":1"), "needs package and resources"));
    CHECK(decl_fails(P("\"id\":\"a\",\"provides\":[],\"requires\":[{\"package\":\"b\",\"resources\":[],\"why\":1}],\"schema\":1"), "unknown requirement field"));
    CHECK(decl_fails("{\"package\":{\"id\":\"a\",\"provides\":[],\"requires\":[],\"schema\":1},\"package\":{}}", "package appears twice"));
    CHECK(decl_fails("{\"package\":[]}", "package: not an object"));
    CHECK(decl_fails("{\"package\":{\"id\":\"a\",\"provides\":[1],\"requires\":[],\"schema\":1}}", "an entry is not a string"));
    /* Too many entries. */
    static char big[64 * 1024];
    size_t at = (size_t)snprintf(big, sizeof(big), "{\"package\":{\"id\":\"a\",\"provides\":[");
    for (unsigned i = 0; i <= HTA_PKG_MAX_PROVIDES; i++) at += (size_t)snprintf(big + at, sizeof(big) - at, "%s\"a:script/s%04u\"", i ? "," : "", i);
    snprintf(big + at, sizeof(big) - at, "],\"requires\":[],\"schema\":1}}");
    CHECK(decl_fails(big, "provides has more than 256 entries"));
    at = (size_t)snprintf(big, sizeof(big), "{\"package\":{\"id\":\"a\",\"provides\":[],\"requires\":[");
    for (unsigned i = 0; i <= HTA_PKG_MAX_REQUIRES; i++) at += (size_t)snprintf(big + at, sizeof(big) - at, "%s{\"package\":\"p%02u\",\"resources\":[]}", i ? "," : "", i);
    snprintf(big + at, sizeof(big) - at, "],\"schema\":1}}");
    CHECK(decl_fails(big, "requires more than 16 packages"));
    at = (size_t)snprintf(big, sizeof(big), "{\"package\":{\"id\":\"a\",\"provides\":[],\"requires\":[{\"package\":\"b\",\"resources\":[");
    for (unsigned i = 0; i <= HTA_PKG_MAX_IMPORTS; i++) at += (size_t)snprintf(big + at, sizeof(big) - at, "%s\"b:script/s%04u\"", i ? "," : "", i);
    snprintf(big + at, sizeof(big) - at, "]}],\"schema\":1}}");
    CHECK(decl_fails(big, "more than"));
    /* Deep nesting elsewhere is bounded, not recursed. */
    at = (size_t)snprintf(big, sizeof(big), "{\"x\":");
    for (unsigned i = 0; i < 5000; i++) big[at++] = '[';
    big[at] = 0;
    CHECK(decl_fails(big, "malformed manifest"));
    puts("  declarations: parse, implicit, every refusal, limits, nesting: ok");
}

/* ---- libraries, and a source for them ----------------------------------------- */

typedef struct { char id[80]; uint8_t *data; size_t size; } lib_file;
typedef struct { lib_file f[40]; unsigned count; char order[40][80]; unsigned opened; } lib_dir;

static bool dir_open(void *ctx, const char *id, const uint8_t **data, size_t *size, void **handle, char *where, size_t wl)
{
    lib_dir *d = ctx;
    snprintf(where, wl, "packages/%s.oalasset", id);
    for (unsigned i = 0; i < d->count; i++)
        if (!strcmp(d->f[i].id, id)) {
            *data = d->f[i].data; *size = d->f[i].size; *handle = NULL;
            snprintf(d->order[d->opened++ % 40], 80, "%s", id);
            return true;
        }
    return false;
}

static uint8_t *oala(const char *manifest, size_t *n)
{
    size_t ml = strlen(manifest);
    uint8_t *b = calloc(1, 32 + ml);
    memcpy(b, "OALA", 4); u32(b + 4, 1); u32(b + 8, (uint32_t)ml);
    memcpy(b + 32, manifest, ml);
    *n = 32 + ml;
    return b;
}

/* A library `id` providing scripts `names` (ns:script/name, sorted) and
 * requiring `reqs` (a JSON list, canonical). */
static void add_lib(lib_dir *d, const char *id, const char *ns, const char *const *names, unsigned count, const char *reqs,
                    const char *extra)
{
    static char m[16384];
    size_t at = (size_t)snprintf(m, sizeof(m), "{%s\"kind\":\"library\",\"package\":{\"id\":\"%s\",\"provides\":[", extra ? extra : "", id);
    for (unsigned i = 0; i < count; i++) at += (size_t)snprintf(m + at, sizeof(m) - at, "%s\"%s:script/%s\"", i ? "," : "", ns, names[i]);
    at += (size_t)snprintf(m + at, sizeof(m) - at, "],\"requires\":%s,\"schema\":1},\"scripts\":[", reqs ? reqs : "[]");
    for (unsigned i = 0; i < count; i++)
        at += (size_t)snprintf(m + at, sizeof(m) - at, "%s{\"api\":\"megamod.v1\",\"callbacks\":[\"on_ability\",\"on_used\"],"
                               "\"id\":\"%s:script/%s\",\"source\":\"function on_used(e, p) end\\nfunction on_ability(p) log('%s') end\\n\"}",
                               i ? "," : "", ns, names[i], names[i]);
    snprintf(m + at, sizeof(m) - at, "]}");
    lib_file *f = &d->f[d->count++];
    snprintf(f->id, sizeof(f->id), "%s", id);
    f->data = oala(m, &f->size);
}

static void dir_free(lib_dir *d)
{
    for (unsigned i = 0; i < d->count; i++) free(d->f[i].data);
    memset(d, 0, sizeof(*d));
}

static hta_package *root_requiring(const char *reqs_json)
{
    static hta_package p;
    static char m[4096];
    char err[300];
    snprintf(m, sizeof(m), "{\"id\":\"r:world/root\",\"package\":{\"id\":\"r.root\",\"provides\":[\"r:world/root\"],\"requires\":%s,\"schema\":1}}", reqs_json);
    bool ok = hta_package_parse((const uint8_t *)m, strlen(m), HTA_PKG_WORLD, &p, err, sizeof(err));
    if (!ok) fprintf(stderr, "root: %s\n", err);
    assert(ok);
    return &p;
}

static bool graph_fails(lib_dir *d, const char *reqs, const char *want)
{
    static hta_pkg_set set;
    char err[600];
    hta_pkg_source src = { dir_open, NULL, d };
    bool ok = hta_pkg_set_load(&set, root_requiring(reqs), &src, err, sizeof(err));
    if (ok) hta_pkg_set_free(&set);
    if (ok || !strstr(err, want)) { fprintf(stderr, "graph: %s (wanted '%s')\n", ok ? "loaded" : err, want); return false; }
    return true;
}

static void libraries_and_graphs(void)
{
    static const char *const one[] = { "pulse" }, *const two[] = { "a", "b" };
    static lib_dir d;
    static hta_pkg_set set;
    char err[600];
    hta_pkg_source src = { dir_open, NULL, &d };
    /* A library on its own. */
    add_lib(&d, "l.a", "la", two, 2, NULL, NULL);
    static hta_pkg_dep dep;
    CHECK(hta_library_load(d.f[0].data, d.f[0].size, &dep, err, sizeof(err)));
    CHECK(dep.decl.declared && dep.decl.kind == HTA_PKG_LIBRARY && dep.scripts->script_count == 2 && dep.digest);
    uint64_t dg = dep.digest;
    hta_pkg_dep_free(&dep);
    /* Its digest ignores provenance, not scripts. */
    {
        static lib_dir e;
        add_lib(&e, "l.a", "la", two, 2, NULL, "\"source_provenance\":\"elsewhere\",");
        CHECK(hta_library_load(e.f[0].data, e.f[0].size, &dep, err, sizeof(err)) && dep.digest == dg);
        hta_pkg_dep_free(&dep);
        dir_free(&e);
        add_lib(&e, "l.a", "la", one, 1, NULL, NULL);
        CHECK(hta_library_load(e.f[0].data, e.f[0].size, &dep, err, sizeof(err)) && dep.digest != dg);
        hta_pkg_dep_free(&dep);
        dir_free(&e);
    }
    /* Library refusals. */
    {
        static const struct { const char *m, *want; } bad[] = {
            { "{\"kind\":\"weapon\",\"package\":{\"id\":\"l.x\",\"provides\":[],\"requires\":[],\"schema\":1},\"scripts\":[]}", "not a library package" },
            { "{\"kind\":\"library\",\"scripts\":[]}", "a library must declare its package" },
            { "{\"kind\":\"library\",\"package\":{\"id\":\"l.x\",\"provides\":[\"lx:script/a\"],\"requires\":[],\"schema\":1},\"scripts\":[]}",
              "package l.x lists lx:script/a in provides, but has no such script" },
            { "{\"kind\":\"library\",\"package\":{\"id\":\"l.x\",\"provides\":[],\"requires\":[],\"schema\":1},\"scripts\":[{\"api\":\"megamod.v1\","
              "\"callbacks\":[\"on_used\"],\"id\":\"lx:script/a\",\"source\":\"function on_used() end\"}]}",
              "package l.x has script lx:script/a but does not list it in provides" },
            { "{\"kind\":\"library\",\"package\":{\"id\":\"l.x\",\"provides\":[\"lx:script/a\"],\"requires\":[],\"schema\":1},\"scripts\":[{\"api\":\"megamod.v2\","
              "\"callbacks\":[\"on_used\"],\"id\":\"lx:script/a\",\"source\":\"x\"}]}", "unsupported script API 'megamod.v2'" },
            { "{\"kind\":\"library\",\"package\":{\"id\":\"l.x\",\"provides\":[\"lx:script/a\"],\"requires\":[],\"schema\":1},\"scripts\":[{\"api\":\"megamod.v1\","
              "\"callbacks\":[\"on_tick\"],\"id\":\"lx:script/a\",\"source\":\"x\"}]}", "unknown callback 'on_tick'" },
            { "{\"kind\":\"library\",\"package\":{\"id\":\"l.x\",\"provides\":[],\"requires\":[],\"schema\":1}}", "a library has no scripts member" },
            { "{\"kind\":\"library\",\"package\":{\"id\":\"l.x\",\"provides\":[],\"requires\":[],\"schema\":1},\"scripts\":[{\"api\":\"megamod.v1\","
              "\"callbacks\":[\"on_used\"],\"id\":\"megamod:script/a\",\"source\":\"x\"}]}", "namespace 'megamod' is reserved" },
        };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            size_t n;
            uint8_t *b = oala(bad[i].m, &n);
            bool ok = hta_library_load(b, n, &dep, err, sizeof(err));
            if (ok) hta_pkg_dep_free(&dep);
            if (ok || !strstr(err, bad[i].want)) { fprintf(stderr, "library %zu: %s (wanted '%s')\n", i, ok ? "loaded" : err, bad[i].want); failures++; }
            if (i == 0) {   /* a library with bytes after its manifest */
                b = realloc(b, n + 1); b[n] = 0;
                CHECK(!hta_library_load(b, n + 1, &dep, err, sizeof(err)));
            }
            free(b);
        }
        size_t n;
        uint8_t *b = oala("{}", &n);
        CHECK(!hta_library_load(b, 31, &dep, err, sizeof(err)) && strstr(err, "not an OALASSET"));
        u32(b + 4, 2);
        CHECK(!hta_library_load(b, n, &dep, err, sizeof(err)));
        free(b);
    }
    dir_free(&d);

    /* Graphs. chain: root -> a -> b -> c. */
    add_lib(&d, "g.a", "ga", one, 1, "[{\"package\":\"g.b\",\"resources\":[\"gb:script/pulse\"]}]", NULL);
    add_lib(&d, "g.b", "gb", one, 1, "[{\"package\":\"g.c\",\"resources\":[]}]", NULL);
    add_lib(&d, "g.c", "gc", one, 1, NULL, NULL);
    CHECK(hta_pkg_set_load(&set, root_requiring("[{\"package\":\"g.a\",\"resources\":[\"ga:script/pulse\"]}]"), &src, err, sizeof(err)));
    CHECK(set.dep_count == 3 && !strcmp(set.dep[0].decl.id, "g.a") && set.dep[0].direct && !set.dep[1].direct && !set.dep[2].direct);
    hta_pkg_set_free(&set);
    /* fan-out: root -> a, c; fan-in (diamond): a -> b, c -> ... b loaded once. */
    add_lib(&d, "g.d", "gd", one, 1, "[{\"package\":\"g.b\",\"resources\":[]}]", NULL);
    d.opened = 0;
    CHECK(hta_pkg_set_load(&set, root_requiring("[{\"package\":\"g.a\",\"resources\":[]},{\"package\":\"g.d\",\"resources\":[\"gd:script/pulse\"]}]"),
                           &src, err, sizeof(err)));
    CHECK(set.dep_count == 4 && d.opened == 4);
    /* Canonical order whatever order they were found in (a, b, c, d). */
    CHECK(!strcmp(set.dep[0].decl.id, "g.a") && !strcmp(set.dep[1].decl.id, "g.b") && !strcmp(set.dep[2].decl.id, "g.c") &&
          !strcmp(set.dep[3].decl.id, "g.d") && set.dep[0].direct && set.dep[3].direct && !set.dep[1].direct);
    CHECK(!strcmp(d.order[0], "g.a") && !strcmp(d.order[1], "g.b") && !strcmp(d.order[3], "g.d"));
    hta_pkg_set_free(&set);
    /* Missing, and the path it was looked for at. */
    CHECK(graph_fails(&d, "[{\"package\":\"g.zzz\",\"resources\":[]}]",
                      "package r.root requires package g.zzz, but it is not present (looked for packages/g.zzz.oalasset)"));
    /* An import the provider does not provide. */
    CHECK(graph_fails(&d, "[{\"package\":\"g.a\",\"resources\":[\"ga:script/nope\"]}]",
                      "package r.root requires ga:script/nope from package g.a, but package g.a does not provide it"));
    CHECK(graph_fails(&d, "[{\"package\":\"g.a\",\"resources\":[\"gb:script/pulse\"]}]",
                      "requires gb:script/pulse from package g.a, but package g.a does not provide it"));
    /* No source at all. */
    {
        char e2[300];
        CHECK(!hta_pkg_set_load(&set, root_requiring("[{\"package\":\"g.a\",\"resources\":[]}]"), NULL, e2, sizeof(e2)) &&
              strstr(e2, "no package source was given"));
    }
    dir_free(&d);
    /* A cycle among libraries, with its path; a cycle back to the root. */
    add_lib(&d, "c.a", "ca", one, 1, "[{\"package\":\"c.b\",\"resources\":[]}]", NULL);
    add_lib(&d, "c.b", "cb", one, 1, "[{\"package\":\"c.a\",\"resources\":[]}]", NULL);
    add_lib(&d, "c.r", "cr", one, 1, "[{\"package\":\"r.root\",\"resources\":[]}]", NULL);
    CHECK(graph_fails(&d, "[{\"package\":\"c.a\",\"resources\":[]}]", "package cycle: c.a -> c.b -> c.a"));
    CHECK(graph_fails(&d, "[{\"package\":\"c.r\",\"resources\":[]}]", "package cycle: r.root -> c.r -> r.root"));
    /* A file that declares another ID than the one it was asked for. */
    add_lib(&d, "c.liar", "cl", one, 1, NULL, NULL);
    snprintf(d.f[d.count - 1].id, sizeof(d.f[0].id), "c.honest");
    CHECK(graph_fails(&d, "[{\"package\":\"c.honest\",\"resources\":[]}]", "packages/c.honest.oalasset declares package c.liar"));
    dir_free(&d);
    /* Depth: 8 links load, 9 are refused. */
    for (unsigned i = 1; i <= 9; i++) {
        char id[16], ns[16], req[96];
        snprintf(id, sizeof(id), "k.d%u", i); snprintf(ns, sizeof(ns), "kd%u", i);
        snprintf(req, sizeof(req), "[{\"package\":\"k.d%u\",\"resources\":[]}]", i + 1);
        add_lib(&d, id, ns, one, 1, i < 9 ? req : NULL, NULL);
    }
    CHECK(graph_fails(&d, "[{\"package\":\"k.d1\",\"resources\":[]}]", "package requirements deeper than 8: r.root -> k.d1"));
    CHECK(hta_pkg_set_load(&set, root_requiring("[{\"package\":\"k.d2\",\"resources\":[]}]"), &src, err, sizeof(err)) && set.dep_count == 8);
    hta_pkg_set_free(&set);
    dir_free(&d);
    /* Width: 16 packages in a set at most (the root + 15). */
    {
        char reqs[2048];
        size_t at = (size_t)snprintf(reqs, sizeof(reqs), "[");
        for (unsigned i = 0; i < 16; i++) {
            char id[16], ns[16];
            snprintf(id, sizeof(id), "w.p%02u", i); snprintf(ns, sizeof(ns), "wp%02u", i);
            add_lib(&d, id, ns, one, 1, NULL, NULL);
            if (i < 15) at += (size_t)snprintf(reqs + at, sizeof(reqs) - at, "%s{\"package\":\"%s\",\"resources\":[]}", i ? "," : "", id);
        }
        snprintf(reqs + at, sizeof(reqs) - at, "]");
        CHECK(hta_pkg_set_load(&set, root_requiring(reqs), &src, err, sizeof(err)) && set.dep_count == 15);
        hta_pkg_set_free(&set);
        snprintf(reqs + at - 0, sizeof(reqs) - at, ",{\"package\":\"w.p15\",\"resources\":[]}]");
        CHECK(graph_fails(&d, reqs, "more than 16 packages in one set"));
    }
    dir_free(&d);
    puts("  libraries and graphs: load, digest, refusals, chain, fan-out, fan-in, missing, cycles, depth, width: ok");
}

/* ---- a world that imports ---------------------------------------------------- */

/* A v3 OALMAP around `manifest`: one triangle, one texture, one start. */
static uint8_t *oalmap(const char *manifest, size_t *n)
{
    size_t ml = strlen(manifest), total = 64 + ml + 120 + 12 + 20 + 12 + 16 + 16;
    uint8_t *b = calloc(1, total);
    memcpy(b, "OALM", 4);
    u32(b + 4, 3); u32(b + 8, (uint32_t)ml); u32(b + 12, 3); u32(b + 16, 3); u32(b + 20, 1); u32(b + 24, 1); u32(b + 28, 1);
    f32(b + 36, -8); f32(b + 40, -8); f32(b + 44, -2); f32(b + 48, 8); f32(b + 52, 8); f32(b + 56, 8);
    memcpy(b + 64, manifest, ml);
    size_t at = 64 + ml;
    float xyz[3][3] = { { -4, -4, 0 }, { 4, -4, 0 }, { 0, 4, 0 } };
    for (int i = 0; i < 3; i++) { for (int k = 0; k < 3; k++) f32(b + at + i * 40 + k * 4, xyz[i][k]); f32(b + at + i * 40 + 20, 1); }
    at += 120;
    u32(b + at, 0); u32(b + at + 4, 1); u32(b + at + 8, 2); at += 12;
    u32(b + at, 0); u32(b + at + 4, 3); u32(b + at + 8, 0); u32(b + at + 12, 0); u32(b + at + 16, UINT32_MAX); at += 20;
    u32(b + at, 2); u32(b + at + 4, 2); u32(b + at + 8, 16); at += 12;
    memset(b + at, 255, 16); at += 16;
    f32(b + at, 0); f32(b + at + 4, 0); at += 16;
    assert(at == total);
    *n = total;
    return b;
}

/* The X4 lab in miniature: its own button script, an imported ability. */
static const char WORLD[] =
    "{\"id\":\"t:world/lab\",\"package\":{\"id\":\"t.lab\",\"provides\":[\"t:script/open_door\",\"t:world/lab\"],"
    "\"requires\":[{\"package\":\"t.shared\",\"resources\":[\"ts:script/pulse\"]}],\"schema\":1},"
    "\"source_provenance\":\"ours\","
    "\"world_entities\":{\"ability_script\":\"ts:script/pulse\",\"entities\":["
    "{\"id\":\"t:entity/button\",\"kind\":\"interactable\",\"links\":[],\"position\":[0,0,1],\"reach\":1.0,\"script\":\"t:script/open_door\"},"
    "{\"id\":\"t:entity/relay\",\"kind\":\"relay\",\"links\":[]}],"
    "\"schema\":3,\"scripts\":[{\"api\":\"megamod.v1\",\"callbacks\":[\"on_used\"],\"id\":\"t:script/open_door\","
    "\"source\":\"function on_used(e, p) end\\n\"}]}}";

static const char *const PULSE[] = { "pulse" };

static char *edit(const char *base, const char *from, const char *to)
{
    static char out[8192];
    const char *at = strstr(base, from);
    if (!at) { fprintf(stderr, "edit: '%s' not found\n", from); abort(); }
    snprintf(out, sizeof(out), "%.*s%s%s", (int)(at - base), base, to, at + strlen(from));
    return out;
}

static bool world_loads(lib_dir *d, const char *manifest, hta_external_map *m, char *err, size_t n)
{
    size_t size;
    uint8_t *b = oalmap(manifest, &size);
    hta_pkg_source src = { dir_open, NULL, d };
    bool ok = hta_external_map_load_with(b, size, &src, m, err, n);
    free(b);
    return ok;
}

static bool world_fails(lib_dir *d, const char *manifest, const char *want)
{
    static hta_external_map m;
    char err[700];
    bool ok = world_loads(d, manifest, &m, err, sizeof(err));
    if (ok) hta_external_map_free(&m);
    if (ok || !strstr(err, want)) { fprintf(stderr, "world: %s (wanted '%s')\n", ok ? "loaded" : err, want); return false; }
    return true;
}

static uint64_t world_digest(lib_dir *d, const char *manifest)
{
    static hta_external_map m;
    char err[700];
    if (!world_loads(d, manifest, &m, err, sizeof(err))) { fprintf(stderr, "world: %s\n", err); failures++; return 0; }
    uint64_t dg = m.digest;
    hta_external_map_free(&m);
    return dg;
}

static void importing_world(void)
{
    static lib_dir d;
    static hta_external_map m;
    char err[700];
    add_lib(&d, "t.shared", "ts", PULSE, 1, NULL, NULL);
    CHECK(world_loads(&d, WORLD, &m, err, sizeof(err)));
    if (failures) fprintf(stderr, "%s\n", err);
    const hta_world_defs *w = &m.world_defs;
    /* Own scripts first, imports after; references are indices. */
    CHECK(w->script_count == 2 && !strcmp(w->script[0].id, "t:script/open_door") && w->script[0].provider == 0 &&
          !strcmp(w->script[1].id, "ts:script/pulse") && w->script[1].provider == 1);
    CHECK(w->entity[0].script == 1 && w->ability_script == 2);
    CHECK(!memcmp(w->pool + w->script[1].at, "function on_used", 16));
    CHECK(m.package.declared && !strcmp(m.package.id, "t.lab") && m.package.dep_count == 1 && m.package.dep_direct[0] &&
          !strcmp(m.package.dep[0], "t.shared"));
    uint64_t d0 = m.digest;
    hta_external_map_free(&m);
    /* Deterministic: the same bytes, the same key and table. */
    CHECK(world_digest(&d, WORLD) == d0);
    /* Without a source: refused, naming what it needs. */
    {
        size_t size;
        uint8_t *b = oalmap(WORLD, &size);
        CHECK(!hta_external_map_load_memory(b, size, &m, err, sizeof(err)) &&
              strstr(err, "package t.lab requires package t.shared, but no package source was given"));
        free(b);
    }
    /* The dependency omitted. */
    {
        static lib_dir none;
        CHECK(world_fails(&none, WORLD, "package: package t.lab requires package t.shared, but it is not present (looked for packages/t.shared.oalasset)"));
    }
    /* Typed references, across packages. */
    CHECK(world_fails(&d, edit(WORLD, "\"ability_script\":\"ts:script/pulse\"", "\"ability_script\":\"ts:script/plse\""),
                      "ability_script references missing script ts:script/plse"));
    CHECK(world_fails(&d, edit(WORLD, "\"ability_script\":\"ts:script/pulse\"", "\"ability_script\":\"t:entity/relay\""),
                      "ability_script: ability script t:entity/relay is a placed entity, expected a script"));
    CHECK(world_fails(&d, edit(WORLD, "\"script\":\"t:script/open_door\"}", "\"script\":\"t:mover/open_door\"}"),
                      "t:entity/button: script 't:mover/open_door' is not a script ID (namespace:script/name)"));
    CHECK(world_fails(&d, edit(WORLD, "\"script\":\"t:script/open_door\"}", "\"script\":\"T:script/open_door\"}"),
                      "t:entity/button: script 'T:script/open_door' is not a resource ID: namespace has capital 'T'"));
    /* An import the world does not declare (the library provides it, the
     * world requires the library, the resource is not in its imports). */
    {
        static lib_dir two;
        static const char *const both[] = { "other", "pulse" };
        add_lib(&two, "t.shared", "ts", both, 2, NULL, NULL);
        CHECK(world_fails(&two, edit(WORLD, "\"ability_script\":\"ts:script/pulse\"", "\"ability_script\":\"ts:script/other\""),
                          "ability script ts:script/other is provided by package t.shared, which package t.lab requires but does not import it from"));
        dir_free(&two);
    }
    /* A reference to a package the world does not require: it is not even
     * loaded, so it is simply missing -- nothing is found by accident. */
    CHECK(world_fails(&d, edit(WORLD, "\"ability_script\":\"ts:script/pulse\"", "\"ability_script\":\"zz:script/pulse\""),
                      "ability_script references missing script zz:script/pulse"));
    /* The same script from the world and its library: two providers. */
    {
        static lib_dir dup;
        static const char *const od[] = { "open_door" };
        add_lib(&dup, "t.shared", "t", od, 1, NULL, NULL);
        static char m2[8192];
        snprintf(m2, sizeof(m2), "%s", edit(WORLD, "\"resources\":[\"ts:script/pulse\"]", "\"resources\":[\"t:script/open_door\"]"));
        snprintf(m2, sizeof(m2), "%s", edit(m2, "\"ability_script\":\"ts:script/pulse\",", ""));
        CHECK(world_fails(&dup, m2, "t:script/open_door: provided by both package t.lab and package t.shared"));
        dir_free(&dup);
    }
    /* provides must equal the content. */
    CHECK(world_fails(&d, edit(WORLD, "\"provides\":[\"t:script/open_door\",\"t:world/lab\"]", "\"provides\":[\"t:world/lab\"]"),
                      "package t.lab defines script t:script/open_door but does not list it in provides"));
    CHECK(world_fails(&d, edit(WORLD, "\"provides\":[\"t:script/open_door\",\"t:world/lab\"]",
                               "\"provides\":[\"t:script/open_door\",\"t:script/zzz\",\"t:world/lab\"]"),
                      "package t.lab lists t:script/zzz in provides, but the world defines no such script"));
    CHECK(world_fails(&d, edit(WORLD, "\"provides\":[\"t:script/open_door\",\"t:world/lab\"]", "\"provides\":[\"t:script/open_door\"]"),
                      "package t.lab defines world t:world/lab but does not list it in provides"));
    CHECK(world_fails(&d, edit(WORLD, "{\"id\":\"t:world/lab\",", "{\"id\":\"t:entity/lab\","), "the manifest's id 't:entity/lab' is not a world ID"));
    /* An imported script can be a world's interactable script too. */
    {
        const char *m2 = edit(WORLD, "\"script\":\"t:script/open_door\"}", "\"script\":\"ts:script/pulse\"}");
        CHECK(world_digest(&d, m2) != 0);
    }
    /* A declaration on its own, requiring nothing (no scripts at all). */
    {
        const char *m3 = "{\"id\":\"t:world/lab\",\"package\":{\"id\":\"t.lab\",\"provides\":[\"t:world/lab\"],\"requires\":[],\"schema\":1},"
                         "\"world_entities\":{\"entities\":[{\"id\":\"t:entity/relay\",\"kind\":\"relay\",\"links\":[]}],\"schema\":1}}";
        CHECK(world_digest(&d, m3) != 0);
    }

    /* The world key over dependencies. */
    uint64_t base = world_digest(&d, WORLD);
    /* A provenance-only change: same key; the world's or the library's. */
    CHECK(world_digest(&d, edit(WORLD, "\"source_provenance\":\"ours\"", "\"source_provenance\":\"rebuilt elsewhere\"")) == base);
    {
        static lib_dir p2;
        add_lib(&p2, "t.shared", "ts", PULSE, 1, NULL, "\"source_provenance\":\"a different machine\",\"importer_version\":\"9\",");
        CHECK(world_digest(&p2, WORLD) == base);
        dir_free(&p2);
    }
    /* The library's script changes: the world's key changes, though the
     * world's own bytes did not. */
    {
        static lib_dir p3;
        add_lib(&p3, "t.shared", "ts", PULSE, 1, NULL, NULL);
        uint8_t *mb = p3.f[0].data + 32;
        char *s = strstr((char *)mb, "log('pulse')");
        assert(s);
        s[5] = 'P';                                   /* one character of Lua */
        CHECK(world_digest(&p3, WORLD) != base);
        dir_free(&p3);
    }
    /* The library gains a dependency of its own: the closure changes. */
    {
        static lib_dir p4;
        static const char *const x[] = { "x" };
        add_lib(&p4, "t.shared", "ts", PULSE, 1, "[{\"package\":\"t.zeta\",\"resources\":[]}]", NULL);
        add_lib(&p4, "t.zeta", "tz", x, 1, NULL, NULL);
        CHECK(world_digest(&p4, WORLD) != base);
        dir_free(&p4);
    }
    /* A declaration changes the key of otherwise identical content. */
    {
        const char *plain = "{\"id\":\"t:world/lab\",\"world_entities\":{\"entities\":[{\"id\":\"t:entity/relay\",\"kind\":\"relay\",\"links\":[]}],\"schema\":1}}";
        const char *declared = "{\"id\":\"t:world/lab\",\"package\":{\"id\":\"t.lab\",\"provides\":[\"t:world/lab\"],\"requires\":[],\"schema\":1},"
                               "\"world_entities\":{\"entities\":[{\"id\":\"t:entity/relay\",\"kind\":\"relay\",\"links\":[]}],\"schema\":1}}";
        CHECK(world_digest(&d, plain) != world_digest(&d, declared));
        CHECK(world_digest(&d, plain) == world_digest(&d, plain));
    }
    /* The order dependencies are found in does not change the key: two
     * libraries, required in canonical order, one requiring the other. */
    {
        static lib_dir p5, p6;
        static const char *const x[] = { "x" };
        const char *two_req = edit(WORLD, "\"requires\":[{\"package\":\"t.shared\",\"resources\":[\"ts:script/pulse\"]}]",
                                   "\"requires\":[{\"package\":\"t.alpha\",\"resources\":[]},{\"package\":\"t.shared\",\"resources\":[\"ts:script/pulse\"]}]");
        static char keep[8192];
        snprintf(keep, sizeof(keep), "%s", two_req);
        add_lib(&p5, "t.alpha", "ta", x, 1, "[{\"package\":\"t.shared\",\"resources\":[]}]", NULL);
        add_lib(&p5, "t.shared", "ts", PULSE, 1, NULL, NULL);
        add_lib(&p6, "t.shared", "ts", PULSE, 1, NULL, NULL);            /* listed the other way round */
        add_lib(&p6, "t.alpha", "ta", x, 1, "[{\"package\":\"t.shared\",\"resources\":[]}]", NULL);
        uint64_t k5 = world_digest(&p5, keep), k6 = world_digest(&p6, keep);
        CHECK(k5 && k5 == k6 && k5 != base);
        dir_free(&p5); dir_free(&p6);
    }
    dir_free(&d);
    puts("  importing world: own + imported scripts, typed refusals, provides, key over dependencies: ok");
}

/* ---- hostile input ------------------------------------------------------------ */

/* Every message is printable text: nothing unterminated or raw leaks out. */
static bool printable(const char *s)
{
    for (; *s; s++) if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E) return false;
    return true;
}

static uint32_t rng = 12345u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static void hostile(void)
{
    static lib_dir d;
    add_lib(&d, "t.shared", "ts", PULSE, 1, NULL, NULL);
    char err[700], err2[700];
    static hta_external_map m;
    unsigned loaded = 0, refused = 0;
    size_t base_n = strlen(WORLD);
    static char buf[8192];
    static const char alphabet[] = "{}[]\":,\\ tx4:/._-Aa0\x01\xff";
    for (int it = 0; it < 3000; it++) {
        memcpy(buf, WORLD, base_n + 1);
        size_t n = base_n;
        int edits = 1 + (int)(rnd() % 4);
        for (int k = 0; k < edits; k++) {
            size_t at = rnd() % n;
            switch (rnd() % 4) {
            case 0: buf[at] = alphabet[rnd() % (sizeof(alphabet) - 1)]; break;           /* flip */
            case 1: n = at; buf[n] = 0; break;                                                /* truncate */
            case 2: if (n + 1 < sizeof(buf)) { memmove(buf + at + 1, buf + at, n - at + 1); buf[at] = alphabet[rnd() % (sizeof(alphabet) - 1)]; n++; } break;
            default: if (n > 1) { memmove(buf + at, buf + at + 1, n - at); n--; } break;   /* delete */
            }
            if (!n) break;
        }
        bool a = world_loads(&d, buf, &m, err, sizeof(err));
        uint64_t da = a ? m.digest : 0;
        if (a) hta_external_map_free(&m);
        bool b = world_loads(&d, buf, &m, err2, sizeof(err2));
        uint64_t db = b ? m.digest : 0;
        if (b) hta_external_map_free(&m);
        CHECK(a == b && da == db && (a || !strcmp(err, err2)));   /* the same answer every time */
        CHECK(a || (err[0] && printable(err)));
        a ? loaded++ : refused++;
    }
    /* Mutated libraries. */
    unsigned lib_ok = 0;
    for (int it = 0; it < 2000; it++) {
        static hta_pkg_dep dep;
        size_t n = d.f[0].size;
        uint8_t *b = malloc(n);
        memcpy(b, d.f[0].data, n);
        for (int k = 0; k < 3; k++) b[32 + rnd() % (n - 32)] = (uint8_t)alphabet[rnd() % (sizeof(alphabet) - 1)];
        if (rnd() % 4 == 0) n = 32 + rnd() % (n - 32);
        bool ok = hta_library_load(b, n, &dep, err, sizeof(err));
        CHECK(ok || (err[0] && printable(err)));
        if (ok) { lib_ok++; hta_pkg_dep_free(&dep); }
        if (getenv("HTA_FUZZ_TRACE")) fprintf(stderr, "lib %d %zu %d %s\n", it, n, ok, ok ? "" : err);
        free(b);
    }
    /* Random IDs never crash the parser and never pass unless well formed. */
    for (int it = 0; it < 20000; it++) {
        char id[128];
        size_t n = rnd() % 120;
        for (size_t i = 0; i < n; i++) id[i] = (char)("ab:/_x9A-. \x80"[rnd() % 13]);
        id[n] = 0;
        hta_rid r;
        if (hta_rid_parse(id, &r, err, sizeof(err)) == HTA_RID_OK) {
            CHECK(strlen(id) <= HTA_RID_MAX && strchr(id, ':') && strchr(id, '/') && !strchr(id, 'A') && !strchr(id, ' '));
        }
        hta_package_id_valid(id, err, sizeof(err));
    }
    printf("  hostile input: 3000 mutated worlds (%u loaded, %u refused, each twice alike), 2000 libraries (%u loaded), 20000 IDs: ok\n",
           loaded, refused, lib_ok);
    dir_free(&d);
}

/* ---- the contract ----------------------------------------------------------- */

static void contract(void)
{
    size_t n = hta_resource_contract_json(NULL, 0);
    char *j = malloc(n + 1);
    CHECK(hta_resource_contract_json(j, n + 1) == n && j[n - 2] == '}');
    /* Every type and reference field is in it, from the tables. */
    for (uint8_t t = 1; t < HTA_RT_COUNT; t++) {
        char want[64];
        snprintf(want, sizeof(want), "{\"name\": \"%s\"", hta_rtype_get(t)->name);
        CHECK(strstr(j, want));
    }
    for (uint8_t f = 0; f < HTA_REF_FIELD_COUNT; f++) CHECK(strstr(j, hta_ref_get(f)->field));
    CHECK(strstr(j, "\"played_members\": [\"breakables\", \"flag_points\", \"package\", \"spawn_points\", \"weather\", \"world_entities\"]"));
    CHECK(strstr(j, "\"limits\": {\"namespace\": 40, \"type\": 24, \"name\": 48, \"total\": 96}"));
    /* Balanced braces and brackets outside strings. */
    int depth = 0; bool in = false;
    for (size_t i = 0; i < n; i++) {
        if (j[i] == '"' && j[i - 1] != '\\') in = !in;
        else if (!in && (j[i] == '{' || j[i] == '[')) depth++;
        else if (!in && (j[i] == '}' || j[i] == ']')) depth--;
        CHECK(depth >= 0);
    }
    CHECK(depth == 0 && !in);
    free(j);
    /* docs/RESOURCES.md carries the tables exactly as the registry prints them. */
    size_t mn = hta_resource_types_markdown(NULL, 0);
    char *md = malloc(mn + 1);
    hta_resource_types_markdown(md, mn + 1);
    FILE *f = fopen(HTA_SOURCE_DIR "/docs/RESOURCES.md", "rb");
    CHECK(f != NULL);
    if (f) {
        static char doc[256 * 1024];
        size_t dn = fread(doc, 1, sizeof(doc) - 1, f);
        doc[dn] = 0;
        fclose(f);
        const char *begin = "<!-- megamod-resources --markdown: begin -->\n", *end = "<!-- megamod-resources --markdown: end -->";
        char *b = strstr(doc, begin), *e = b ? strstr(b, end) : NULL;
        CHECK(b && e);
        if (b && e) {
            b += strlen(begin);
            bool same = (size_t)(e - b) == mn && !memcmp(b, md, mn);
            if (!same) fprintf(stderr, "docs/RESOURCES.md's tables differ from `megamod-resources --markdown`; regenerate them\n");
            CHECK(same);
        }
    }
    free(md);
    puts("  contract: JSON from the tables, balanced; docs/RESOURCES.md tables match the registry: ok");
}

int main(void)
{
    ids();
    registry();
    resolution();
    declarations();
    libraries_and_graphs();
    importing_world();
    hostile();
    contract();
    if (failures) { fprintf(stderr, "resource: %d failures\n", failures); return 1; }
    puts("resource: all ok");
    return 0;
}
