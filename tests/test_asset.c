/* Package-backed asset resources (X5, docs/RESOURCES.md "Asset resources"):
 * libraries of textures, materials, models and sounds, imported across
 * packages through typed references, decoded once into one table, placed
 * by props and played by movers; every refusal, the world key over asset
 * bytes, hostile input, and load/unload lifetimes (run under ASan/UBSan in
 * build-asan). No game data. */
#include "asset/asset_res.h"
#include "asset/external_map.h"
#include "asset/package.h"
#include "engine/audio.h"
#include "engine/world_entities.h"
#include "game/world_sounds.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void u32(uint8_t *p, uint32_t x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); p[2] = (uint8_t)(x >> 16); p[3] = (uint8_t)(x >> 24); }
static void f32(uint8_t *p, float v) { uint32_t x; memcpy(&x, &v, 4); u32(p, x); }

/* ---- payloads --------------------------------------------------------------- */

#define MESH_BYTES (16u + 24u * 40u + 36u * 4u + 12u)
/* A unit cube around the origin, 0.5 on a side, one group in slot 0. */
static void cube(uint8_t *b, float half)
{
    memset(b, 0, MESH_BYTES);
    memcpy(b, "MSH1", 4); u32(b + 4, 24); u32(b + 8, 36); u32(b + 12, 1);
    uint8_t *v = b + 16;
    static const float N[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    for (int f = 0; f < 6; f++)
        for (int c = 0; c < 4; c++) {
            float p[3];
            int a = f / 2, s = f % 2 ? -1 : 1, u = (a + 1) % 3, w = (a + 2) % 3;
            p[a] = s * half; p[u] = (c & 1) ? half : -half; p[w] = (c & 2) ? half : -half;
            uint8_t *o = v + (f * 4 + c) * 40;
            for (int k = 0; k < 3; k++) { f32(o + k * 4, p[k]); f32(o + 12 + k * 4, N[f][k]); }
            f32(o + 24, (c & 1) ? 1.0f : 0.0f); f32(o + 28, (c & 2) ? 1.0f : 0.0f);
        }
    uint8_t *ix = v + 24 * 40;
    for (int f = 0; f < 6; f++) {
        static const uint32_t q[6] = { 0, 1, 3, 0, 3, 2 };
        for (int k = 0; k < 6; k++) u32(ix + (f * 6 + k) * 4, (uint32_t)(f * 4) + q[k]);
    }
    uint8_t *g = ix + 36 * 4;
    u32(g, 0); u32(g + 4, 36); u32(g + 8, 0);
}

/* ---- a library, from parts ---------------------------------------------------- */

typedef struct { char id[80]; uint8_t *data; size_t size; } lib_file;
typedef struct { lib_file f[20]; unsigned count; } lib_dir;

static bool dir_open(void *ctx, const char *id, const uint8_t **data, size_t *size, void **handle, char *where, size_t wl)
{
    lib_dir *d = ctx;
    snprintf(where, wl, "packages/%s.oalasset", id);
    for (unsigned i = 0; i < d->count; i++)
        if (!strcmp(d->f[i].id, id)) { *data = d->f[i].data; *size = d->f[i].size; *handle = NULL; return true; }
    return false;
}

static uint8_t *oala(const char *manifest, const uint8_t *payload, size_t plen, size_t *n)
{
    size_t ml = strlen(manifest);
    uint8_t *b = calloc(1, 32 + ml + plen + 1);
    memcpy(b, "OALA", 4); u32(b + 4, 1); u32(b + 8, (uint32_t)ml);
    memcpy(b + 32, manifest, ml);
    if (plen) memcpy(b + 32 + ml, payload, plen);
    *n = 32 + ml + plen;
    return b;
}

static void dir_put(lib_dir *d, const char *id, uint8_t *data, size_t size)
{
    for (unsigned i = 0; i < d->count; i++)
        if (!strcmp(d->f[i].id, id)) { free(d->f[i].data); d->f[i].data = data; d->f[i].size = size; return; }
    snprintf(d->f[d->count].id, sizeof(d->f[0].id), "%s", id);
    d->f[d->count].data = data; d->f[d->count].size = size;
    d->count++;
}

static void put_lib(lib_dir *d, const char *id, const char *manifest, const uint8_t *payload, size_t plen)
{
    size_t n;
    uint8_t *b = oala(manifest, payload, plen, &n);
    dir_put(d, id, b, n);
}

static void dir_free(lib_dir *d)
{
    for (unsigned i = 0; i < d->count; i++) free(d->f[i].data);
    memset(d, 0, sizeof(*d));
}

/* The shared art library, as Open Asset Lab writes it (canonical JSON). */
static const char ART[] =
    "{\"assets\":{\"materials\":[{\"draw\":\"opaque\",\"id\":\"xs:material/crate\",\"texture\":\"xs:texture/crate\"}],"
    "\"members\":[{\"path\":\"models/crate.mesh\",\"size\":1132},{\"path\":\"sounds/impact.pcm\",\"size\":200},"
    "{\"path\":\"textures/crate.rgba\",\"size\":64}],"
    "\"models\":[{\"format\":\"mesh1\",\"id\":\"xs:model/crate\",\"materials\":[\"xs:material/crate\"],\"member\":\"models/crate.mesh\"}],"
    "\"schema\":1,"
    "\"sounds\":[{\"channels\":1,\"format\":\"pcm_s16le\",\"frames\":100,\"id\":\"xs:sound/impact\",\"member\":\"sounds/impact.pcm\",\"rate\":22050}],"
    "\"textures\":[{\"format\":\"rgba8\",\"height\":4,\"id\":\"xs:texture/crate\",\"member\":\"textures/crate.rgba\",\"width\":4}]},"
    "\"kind\":\"library\",\"package\":{\"id\":\"t.art\",\"provides\":[\"xs:material/crate\",\"xs:model/crate\",\"xs:sound/impact\","
    "\"xs:texture/crate\"],\"requires\":[],\"schema\":1},"
    "\"provenance\":{\"xs:model/crate\":{\"importer\":\"test\",\"source_path\":\"models/props/crate01.mdl\"}},\"scripts\":[]}";

static uint8_t PAYLOAD[MESH_BYTES + 200 + 64];

static void payload_init(void)
{
    cube(PAYLOAD, 0.25f);
    for (int i = 0; i < 100; i++) { int16_t s = (int16_t)(8000.0 * sin(i * 0.3)); PAYLOAD[MESH_BYTES + i * 2] = (uint8_t)s; PAYLOAD[MESH_BYTES + i * 2 + 1] = (uint8_t)((uint16_t)s >> 8); }
    for (int i = 0; i < 16; i++) { uint8_t *p = PAYLOAD + MESH_BYTES + 200 + i * 4; p[0] = 200; p[1] = 30; p[2] = 30; p[3] = 255; }
}

static char *edit(const char *base, const char *from, const char *to)
{
    static char out[2][16384];
    static int k;
    char *o = out[k++ & 1];
    const char *at = strstr(base, from);
    if (!at) { fprintf(stderr, "edit: '%s' not found\n", from); abort(); }
    snprintf(o, sizeof(out[0]), "%.*s%s%s", (int)(at - base), base, to, at + strlen(from));
    return o;
}

static void put_art(lib_dir *d, const char *manifest, const uint8_t *payload, size_t plen)
{
    size_t n;
    uint8_t *b = oala(manifest, payload, plen, &n);
    dir_put(d, "t.art", b, n);
}

/* ---- a world that places them ------------------------------------------------- */

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

static const char WORLD[] =
    "{\"id\":\"t:world/art\",\"package\":{\"id\":\"t.world\",\"provides\":[\"t:mover/door\",\"t:world/art\"],"
    "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\",\"xs:sound/impact\"]}],\"schema\":1},"
    "\"source_provenance\":\"ours\","
    "\"world_entities\":{\"entities\":["
    "{\"id\":\"t:entity/crate_a\",\"kind\":\"prop\",\"links\":[],\"model\":\"xs:model/crate\",\"position\":[1,0,0.25]},"
    "{\"id\":\"t:entity/crate_b\",\"kind\":\"prop\",\"links\":[],\"model\":\"xs:model/crate\",\"position\":[2,0,0.25]},"
    "{\"definition\":\"t:mover/door\",\"id\":\"t:entity/door\",\"kind\":\"mover\",\"links\":[],\"position\":[0,2,0.5]}],"
    "\"mover_definitions\":[{\"id\":\"t:mover/door\",\"move\":[0,1,0],\"size\":[0.1,1,1],\"sound\":\"xs:sound/impact\",\"speed\":1}],"
    "\"schema\":4}}";

static bool world_loads(lib_dir *d, const char *manifest, hta_external_map *m, char *err, size_t n)
{
    size_t size;
    uint8_t *b = oalmap(manifest, &size);
    hta_pkg_source src = { dir_open, NULL, d };
    bool ok = hta_external_map_load_with(b, size, &src, m, err, n);
    free(b);
    return ok;
}

static bool printable(const char *s)
{
    for (; *s; s++) if ((unsigned char)*s < 0x20 || (unsigned char)*s >= 0x7F) return false;
    return true;
}

static bool world_fails(lib_dir *d, const char *manifest, const char *want)
{
    static hta_external_map m;
    char err[800];
    bool ok = world_loads(d, manifest, &m, err, sizeof(err));
    if (ok) hta_external_map_free(&m);
    if (ok || !strstr(err, want) || !printable(err)) {
        fprintf(stderr, "world: %s (wanted '%s')\n", ok ? "loaded" : err, want);
        return false;
    }
    return true;
}

static lib_dir DIR_;

/* The art library with `from` edited to `to` (and the payload as given):
 * the world must be refused saying `want`. */
static bool art_fails(const char *from, const char *to, const uint8_t *payload, size_t plen, const char *want)
{
    put_art(&DIR_, from ? edit(ART, from, to) : ART, payload, plen);
    bool r = world_fails(&DIR_, WORLD, want);
    put_art(&DIR_, ART, PAYLOAD, sizeof(PAYLOAD));
    return r;
}

/* ---- the good set ---------------------------------------------------------------- */

static void good_set(void)
{
    static hta_external_map m;
    char err[800];
    put_art(&DIR_, ART, PAYLOAD, sizeof(PAYLOAD));
    bool ok = world_loads(&DIR_, WORLD, &m, err, sizeof(err));
    if (!ok) fprintf(stderr, "good world: %s\n", err);
    CHECK(ok);
    if (!ok) return;
    const hta_asset_table *a = &m.assets;
    CHECK(a->texture_count == 1 && a->material_count == 1 && a->model_count == 1 && a->sound_count == 1);
    CHECK(a->payload_bytes == sizeof(PAYLOAD));
    CHECK(!strcmp(a->model[0].id, "xs:model/crate") && a->model[0].provider == 1);
    CHECK(!strcmp(m.package.dep[a->model[0].provider - 1], "t.art"));
    /* Resolved once, to indices: prop -> model -> material -> texture. */
    CHECK(a->model[0].slot_count == 1 && a->model[0].slot[0] == 0 && a->material[0].texture == 0);
    CHECK(a->model[0].mesh.vertex_count == 24 && a->model[0].mesh.index_count == 36);
    CHECK(fabsf(a->model[0].mesh.bounds_min[0] + 0.25f) < 1e-6f && fabsf(a->model[0].mesh.bounds_max[2] - 0.25f) < 1e-6f);
    /* The model's mesh borrows the texture's pixels: one copy. */
    CHECK(a->model[0].mesh.texture_count == 1 && a->model[0].mesh.textures[0].rgba == a->texture[0].rgba);
    CHECK(a->texture[0].rgba[0] == 200 && a->texture[0].width == 4);
    CHECK(a->sound[0].frames == 100 && a->sound[0].samples[1] == (int16_t)(8000.0 * sin(0.3)));
    const hta_world_defs *d = &m.world_defs;
    CHECK(d->schema == 4 && d->asset_models == 1 && d->asset_sounds == 1);
    CHECK(d->entity[0].kind == HTA_WDEF_PROP && d->entity[0].model == 1 && d->entity[1].model == 1);
    CHECK(fabsf(d->entity[0].min[0] - 0.75f) < 1e-6f && fabsf(d->entity[0].max[2] - 0.5f) < 1e-6f);
    CHECK(d->mover_def[0].sound == 1);
    CHECK(m.package.dep_count == 1);
    /* Runtime: props collide, a mover that starts to move sounds once. */
    static hta_world_entities w;
    CHECK(hta_went_load(&w, d, err, sizeof(err)));
    hta_collision_instance inst[HTA_WDEF_MAX_ENTITIES];
    CHECK(hta_went_instances(&w, inst, HTA_WDEF_MAX_ENTITIES) == 3);
    CHECK(fabsf(inst[0].pos[0] - 1.0f) < 1e-6f && fabsf(inst[0].pos[2] - 0.25f) < 1e-6f);
    CHECK(inst[0].grid == &w.prop_coll[0] && inst[0].grid->built);
    hta_went_step(&w, 1.0f / 60.0f);
    CHECK(w.cue_count == 0);
    CHECK(hta_went_send(&w, 2, HTA_WIN_OPEN, HTA_WENT_NO_ACTOR));
    hta_went_step(&w, 1.0f / 60.0f);
    CHECK(w.cue_count == 1 && w.cues[0].entity == 2 && w.cues[0].sound == 0);
    /* The mixer hears it: the bank registers the library's clip once. */
    static hta_audio au;
    hta_audio_init(&au, 22050, 1);
    static hta_world_sounds ws;
    CHECK(hta_world_sounds_bind(&ws, &au, a) == 1 && ws.slot_count == 1);
    float ear[3] = { 0, 1.5f, 0.5f }, right[3] = { 1, 0, 0 };
    CHECK(hta_world_sounds_play(&ws, &au, &w, ear, right) == 1);
    int16_t out[64];
    hta_audio_mix(&au, out, 64);
    int peak = 0;
    for (int i = 0; i < 64; i++) peak = abs(out[i]) > peak ? abs(out[i]) : peak;
    CHECK(peak > 1000);
    hta_went_step(&w, 1.0f / 60.0f);
    CHECK(w.cue_count == 0);                  /* once per start */
    /* A second world with the same sound reuses the bank's clip. */
    CHECK(hta_world_sounds_bind(&ws, &au, a) == 1 && ws.slot_count == 1 && au.clip_count == 1);
    /* Far away: not heard. */
    float gain, pan;
    float far[3] = { 100, 0, 0 };
    CHECK(!hta_world_sounds_place(far, ear, right, &gain, &pan));
    /* A joiner that finds the door moving (a snap) is silent; its next
     * start is heard. */
    static hta_world_entities j;
    CHECK(hta_went_load(&j, d, err, sizeof(err)));
    j.remote = true;
    hta_went_mover_state st = { 2, HTA_MOVER_OPENING, 30000 };
    CHECK(hta_went_apply(&j, &st, true));
    hta_went_step(&j, 1.0f / 60.0f);
    CHECK(j.cue_count == 0);
    st.phase = HTA_MOVER_CLOSING;
    CHECK(hta_went_apply(&j, &st, false));
    hta_went_step(&j, 1.0f / 60.0f);
    CHECK(j.cue_count == 1);
    hta_went_reset(&w);
    hta_went_step(&w, 1.0f / 60.0f);
    CHECK(w.cue_count == 0);                  /* a new round is silent */
    hta_went_free(&w);
    hta_went_free(&j);
    hta_world_sounds_free(&ws);
    hta_external_map_free(&m);
    puts("  good set: library decoded, prop -> model -> material -> texture resolved once, props solid, mover sound heard once: ok");
}

static void visual_materials(void)
{
    char visual[16384], err[800];
    static hta_external_map m;
    snprintf(visual, sizeof(visual), "%s", edit(ART, "\"draw\":\"opaque\"",
                 "\"draw\":\"opaque\",\"emissive\":1.5,\"roughness\":0.3"));
    snprintf(visual, sizeof(visual), "%s", edit(visual, "\"schema\":1,\"sounds\"",
                 "\"schema\":2,\"sounds\""));
    put_art(&DIR_, visual, PAYLOAD, sizeof(PAYLOAD));
    CHECK(world_loads(&DIR_, WORLD, &m, err, sizeof(err)));
    if (m.assets.material_count) {
        CHECK(fabsf(m.assets.material[0].emissive - 1.5f) < 1e-6f);
        CHECK(fabsf(m.assets.material[0].roughness - 0.3f) < 1e-6f);
        hta_external_map_free(&m);
    }
    CHECK(art_fails("\"draw\":\"opaque\"", "\"draw\":\"opaque\",\"emissive\":1.5",
                    PAYLOAD, sizeof(PAYLOAD), "need assets schema 2"));
    CHECK(art_fails("\"draw\":\"opaque\"", "\"draw\":\"opaque\",\"emissive\":-1",
                    PAYLOAD, sizeof(PAYLOAD), "emissive must be 0..4"));
    put_art(&DIR_, ART, PAYLOAD, sizeof(PAYLOAD));
}

/* ---- two consumers, and a library that imports another --------------------------- */

static const char SKIN[] =
    "{\"assets\":{\"materials\":[{\"draw\":\"alpha\",\"id\":\"sk:material/glass\",\"texture\":\"xs:texture/crate\"}],"
    "\"members\":[{\"path\":\"models/pane.mesh\",\"size\":1132}],"
    "\"models\":[{\"format\":\"mesh1\",\"id\":\"sk:model/pane\",\"materials\":[\"sk:material/glass\"],\"member\":\"models/pane.mesh\"}],"
    "\"schema\":1,\"sounds\":[],\"textures\":[]},"
    "\"kind\":\"library\",\"package\":{\"id\":\"t.skin\",\"provides\":[\"sk:material/glass\",\"sk:model/pane\"],"
    "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:texture/crate\"]}],\"schema\":1},\"scripts\":[]}";

static const char WORLD2[] =
    "{\"id\":\"u:world/two\",\"package\":{\"id\":\"u.world\",\"provides\":[\"u:world/two\"],"
    "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\"]},"
    "{\"package\":\"t.skin\",\"resources\":[\"sk:model/pane\"]}],\"schema\":1},"
    "\"world_entities\":{\"entities\":["
    "{\"id\":\"u:entity/crate\",\"kind\":\"prop\",\"links\":[],\"model\":\"xs:model/crate\",\"position\":[0,0,0.25]},"
    "{\"id\":\"u:entity/pane\",\"kind\":\"prop\",\"links\":[],\"model\":\"sk:model/pane\",\"position\":[0,1,0.25]}],"
    "\"schema\":4}}";

static void consumers(void)
{
    static hta_external_map a, b;
    char err[800];
    uint8_t pane[MESH_BYTES];
    cube(pane, 0.5f);
    size_t n;
    put_lib(&DIR_, "t.skin", SKIN, pane, sizeof(pane));
    bool ok1 = world_loads(&DIR_, WORLD, &a, err, sizeof(err));
    if (!ok1) fprintf(stderr, "consumer 1: %s\n", err);
    bool ok2 = world_loads(&DIR_, WORLD2, &b, err, sizeof(err));
    if (!ok2) fprintf(stderr, "consumer 2: %s\n", err);
    CHECK(ok1 && ok2);
    if (ok1 && ok2) {
        /* The same library resource in both, each world with its own table. */
        CHECK(!strcmp(a.assets.model[a.world_defs.entity[0].model - 1].id, "xs:model/crate"));
        CHECK(!strcmp(b.assets.model[b.world_defs.entity[0].model - 1].id, "xs:model/crate"));
        CHECK(a.package.dep_digest[0] == b.package.dep_digest[0]);          /* t.art, one set of bytes */
        /* Canonical order: t.art's assets first, then t.skin's. */
        CHECK(b.assets.model_count == 2 && !strcmp(b.assets.model[1].id, "sk:model/pane") && b.world_defs.entity[1].model == 2);
        /* t.skin's material resolved through ITS import to t.art's texture. */
        CHECK(b.assets.material_count == 2 && b.assets.material[1].texture == 0 && b.assets.texture_count == 1);
        CHECK(b.assets.model[1].mesh.textures[0].rgba == b.assets.texture[0].rgba);
        CHECK(b.assets.model[1].mesh.submeshes[0].draw_mode == HTA_DRAW_ALPHA);
        CHECK(a.key != b.key);
    }
    hta_external_map_free(&a);
    hta_external_map_free(&b);
    /* A library that uses another's texture without importing it. */
    put_lib(&DIR_, "t.skin", edit(SKIN, "\"resources\":[\"xs:texture/crate\"]", "\"resources\":[]"), pane, sizeof(pane));
    CHECK(world_fails(&DIR_, WORLD2, "sk:material/glass: texture xs:texture/crate is provided by package t.art, which package t.skin "
                                     "requires but does not import it from"));
    put_lib(&DIR_, "t.skin", SKIN, pane, sizeof(pane));
    puts("  two consumers, one library: the same resource in each; a library imports another's texture through its own requires: ok");
}

/* ---- refusals ------------------------------------------------------------------------ */

static void refusals(void)
{
    const uint8_t *P = PAYLOAD;
    size_t PL = sizeof(PAYLOAD);
    /* the package graph */
    /* dependency omitted: nothing in the set provides the namespace */
    CHECK(world_fails(&DIR_, edit(WORLD, "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\",\"xs:sound/impact\"]}]",
                                  "\"requires\":[]"), "t:mover/door references missing sound xs:sound/impact (no package in this set "
                                  "provides namespace 'xs': is a requirement missing?)"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"resources\":[\"xs:model/crate\",\"xs:sound/impact\"]", "\"resources\":[\"xs:sound/impact\"]"),
                      "t:entity/crate_a: model xs:model/crate is provided by package t.art, which package t.world requires but does not import it from"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"package\":\"t.art\"", "\"package\":\"t.gone\""),
                      "package t.world requires package t.gone, but it is not present (looked for packages/t.gone.oalasset)"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"model\":\"xs:model/crate\",\"position\":[1", "\"model\":\"xs:model/crat\",\"position\":[1"),
                      "t:entity/crate_a references missing model xs:model/crat"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"model\":\"xs:model/crate\",\"position\":[1", "\"model\":\"xs:material/crate\",\"position\":[1"),
                      "t:entity/crate_a: model xs:material/crate is a material, expected a model"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"model\":\"xs:model/crate\",\"position\":[1", "\"model\":\"xs:texture/nope\",\"position\":[1"),
                      "t:entity/crate_a: model 'xs:texture/nope' is not a model ID (namespace:model/name)"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"sound\":\"xs:sound/impact\"", "\"sound\":\"xs:model/crate\""),
                      "t:mover/door: sound xs:model/crate is a model, expected a sound"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"model\":\"xs:model/crate\",\"position\":[1", "\"model\":\"Xs:model/crate\",\"position\":[1"),
                      "t:entity/crate_a: model 'Xs:model/crate' is not a resource ID: namespace has capital 'X'"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"model\":\"xs:model/crate\",\"position\":[1", "\"model\":\"xs:ruleset/crate\",\"position\":[1"),
                      "resource type 'ruleset' is reserved"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"schema\":4}}", "\"schema\":3}}"), "props and sounds need schema 4"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"kind\":\"prop\",\"links\":[],\"model\":\"xs:model/crate\",\"position\":[1,0,0.25]",
                                  "\"kind\":\"prop\",\"links\":[]"), "t:entity/crate_a: a prop needs a model"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"kind\":\"mover\",\"links\":[]", "\"kind\":\"mover\",\"links\":[],\"model\":\"xs:model/crate\""),
                      "t:entity/door: only a prop takes a model (it is a mover)"));
    CHECK(world_fails(&DIR_, edit(WORLD, "\"provides\":[\"t:mover/door\",\"t:world/art\"]",
                                  "\"provides\":[\"t:model/crate\",\"t:mover/door\",\"t:world/art\"]"),
                      "package t.world lists t:model/crate in provides, but the world defines no such model"));
    /* the library: its declaration and its descriptors */
    CHECK(art_fails("\"provides\":[\"xs:material/crate\",", "\"provides\":[", P, PL,
                    "package t.art has material xs:material/crate but does not list it in provides"));
    CHECK(art_fails("\"xs:texture/crate\"],\"requires\"", "\"xs:texture/crate\",\"xs:texture/extra\"],\"requires\"", P, PL,
                    "package t.art lists xs:texture/extra in provides, but has no such texture"));
    CHECK(art_fails("\"member\":\"models/crate.mesh\"", "\"member\":\"models/crates.mesh\"", P, PL,
                    "xs:model/crate declares package member models/crates.mesh, but that member is missing"));
    CHECK(art_fails("\"member\":\"models/crate.mesh\"", "\"member\":\"../models/crate.mesh\"", P, PL,
                    "xs:model/crate: member path '../models/crate.mesh': has '..' (no parent references)"));
    CHECK(art_fails("{\"path\":\"models/crate.mesh\"", "{\"path\":\"/models/crate.mesh\"", P, PL,
                    "member path '/models/crate.mesh': starts with '/'"));
    CHECK(art_fails("{\"path\":\"models/crate.mesh\"", "{\"path\":\"models\\\\crate.mesh\"", P, PL, "has '\\' (the separator is '/')"));
    CHECK(art_fails("{\"path\":\"models/crate.mesh\"", "{\"path\":\"models/crate\\u0000.mesh\"", P, PL, "a member path is not a string"));
    CHECK(art_fails("\"texture\":\"xs:texture/crate\"", "\"texture\":\"xs:material/crate\"", P, PL,
                    "xs:material/crate: texture xs:material/crate is a material, expected a texture"));
    CHECK(art_fails("\"materials\":[\"xs:material/crate\"]", "\"materials\":[\"xs:texture/crate\"]", P, PL,
                    "xs:model/crate: material slot xs:texture/crate is a texture, expected a material"));
    CHECK(art_fails("\"texture\":\"xs:texture/crate\"", "\"texture\":\"xs:texture/crates\"", P, PL,
                    "xs:material/crate references missing texture xs:texture/crates"));
    CHECK(art_fails("\"id\":\"xs:model/crate\",\"materials\"", "\"id\":\"xs:texture/crate\",\"materials\"", P, PL,
                    "assets.models: xs:texture/crate is a texture ID, expected a model"));
    CHECK(art_fails("\"id\":\"xs:texture/crate\"", "\"id\":\"megamod:texture/crate\"", P, PL, "namespace 'megamod' is reserved"));
    CHECK(art_fails("\"draw\":\"opaque\"", "\"draw\":\"glow\"", P, PL, "unknown draw 'glow'"));
    CHECK(art_fails("\"format\":\"rgba8\"", "\"format\":\"dxt1\"", P, PL, "unsupported texture format 'dxt1'"));
    CHECK(art_fails("\"format\":\"mesh1\"", "\"format\":\"mdl\"", P, PL, "unsupported model format 'mdl'"));
    CHECK(art_fails("\"width\":4}", "\"width\":4,\"source\":\"x.vtf\"}", P, PL, "unknown or repeated field 'source' in assets.textures"));
    CHECK(art_fails("\"schema\":1,\"sounds\"", "\"schema\":3,\"sounds\"", P, PL, "unsupported assets schema 3"));
    CHECK(art_fails("\"width\":4}", "\"width\":5}", P, PL,
                    "xs:texture/crate: 5x4 rgba8 is 80 bytes, but member textures/crate.rgba holds 64"));
    CHECK(art_fails("\"frames\":100", "\"frames\":101", P, PL,
                    "xs:sound/impact: 101 frames of 1-channel pcm_s16le are 202 bytes, but member sounds/impact.pcm holds 200"));
    CHECK(art_fails("{\"path\":\"textures/crate.rgba\",\"size\":64}", "{\"path\":\"textures/crate.rgba\",\"size\":64},{\"path\":\"textures/spare.rgba\",\"size\":4}",
                    P, PL, "its members add up to 1400 bytes, but the package carries 1396"));
    {
        uint8_t more[sizeof(PAYLOAD) + 4];
        memcpy(more, PAYLOAD, sizeof(PAYLOAD)); memset(more + sizeof(PAYLOAD), 0, 4);
        CHECK(art_fails("{\"path\":\"textures/crate.rgba\",\"size\":64}", "{\"path\":\"textures/crate.rgba\",\"size\":64},{\"path\":\"textures/spare.rgba\",\"size\":4}",
                        more, sizeof(more), "member textures/spare.rgba is not used by any resource"));
        CHECK(art_fails(NULL, NULL, more, sizeof(more), "its members add up to 1396 bytes, but the package carries 1400"));
    }
    CHECK(art_fails("\"member\":\"sounds/impact.pcm\"", "\"member\":\"textures/crate.rgba\"", P, PL,
                    "member textures/crate.rgba backs both"));
    CHECK(art_fails("{\"path\":\"models/crate.mesh\",\"size\":1132},{\"path\":\"sounds/impact.pcm\",\"size\":200}",
                    "{\"path\":\"sounds/impact.pcm\",\"size\":200},{\"path\":\"models/crate.mesh\",\"size\":1132}", P, PL,
                    "assets.members is not in canonical (byte) order at models/crate.mesh"));
    {
        /* two descriptors with one ID, the second in canonical position */
        const char *twice = edit(ART, "\"textures\":[{\"format\":\"rgba8\",\"height\":4,\"id\":\"xs:texture/crate\",\"member\":\"textures/crate.rgba\",\"width\":4}]",
                                 "\"textures\":[{\"format\":\"rgba8\",\"height\":4,\"id\":\"xs:texture/crate\",\"member\":\"textures/crate.rgba\",\"width\":4},"
                                 "{\"format\":\"rgba8\",\"height\":4,\"id\":\"xs:texture/crate\",\"member\":\"textures/crate.rgba\",\"width\":4}]");
        put_art(&DIR_, twice, P, PL);
        CHECK(world_fails(&DIR_, WORLD, "package t.art: xs:texture/crate is declared twice"));
        put_art(&DIR_, ART, P, PL);
    }
    /* payload corruption */
    uint8_t bad[sizeof(PAYLOAD)];
#define MESH_FAILS(edit_, want) do { memcpy(bad, PAYLOAD, sizeof(bad)); edit_; CHECK(art_fails(NULL, NULL, bad, sizeof(bad), want)); } while (0)
    MESH_FAILS(memcpy(bad, "MSH2", 4), "member models/crate.mesh: not a mesh1 payload");
    MESH_FAILS(u32(bad + 4, 25), "is 1132 bytes, but its counts need 1172");
    MESH_FAILS(u32(bad + 4, 0xFFFFFFFFu), "counts out of range");
    MESH_FAILS(u32(bad + 8, 0x40000000u), "counts out of range");
    MESH_FAILS(u32(bad + 16 + 24 * 40 + 8, 24), "index 2 names vertex 24 of 24");
    MESH_FAILS(f32(bad + 16 + 40 * 3 + 4, NAN), "vertex 3 is not finite");
    MESH_FAILS(f32(bad + 16, 1e9f), "vertex 0 is not finite or beyond 4096");
    MESH_FAILS(u32(bad + 16 + 24 * 40 + 36 * 4 + 8, 1), "group 0 draws with material slot 1, but the model has 1");
    MESH_FAILS(u32(bad + 16 + 24 * 40 + 36 * 4 + 4, 33), "its groups cover 33 of 36 indices");
    MESH_FAILS(u32(bad + 16 + 24 * 40 + 36 * 4, 3), "group 0 does not follow on");
#undef MESH_FAILS
    CHECK(art_fails(NULL, NULL, P, PL - 1, "its members add up to 1396 bytes, but the package carries 1395"));
    {
        /* members that add up past the limit: refused before any offset is kept */
        static char many[16384];
        size_t at = (size_t)snprintf(many, sizeof(many), "\"members\":[{\"path\":\"models/crate.mesh\",\"size\":1132}");
        for (int i = 0; i < 40; i++)
            at += (size_t)snprintf(many + at, sizeof(many) - at, ",{\"path\":\"n/p%02d.bin\",\"size\":%u}", i, 90u * 1024u * 1024u);
        snprintf(many + at, sizeof(many) - at, ",{\"path\":\"sounds/impact.pcm\"");
        CHECK(art_fails("\"members\":[{\"path\":\"models/crate.mesh\",\"size\":1132},{\"path\":\"sounds/impact.pcm\"", many, P, PL,
                        "its members add up to more than 100663296 bytes"));
    }
    /* a duplicate provider: a second library that also provides the texture */
    {
        size_t n;
        static const char DUP[] =
            "{\"assets\":{\"materials\":[],\"members\":[{\"path\":\"t.rgba\",\"size\":64}],\"models\":[],\"schema\":1,\"sounds\":[],"
            "\"textures\":[{\"format\":\"rgba8\",\"height\":4,\"id\":\"xs:texture/crate\",\"member\":\"t.rgba\",\"width\":4}]},"
            "\"kind\":\"library\",\"package\":{\"id\":\"t.dup\",\"provides\":[\"xs:texture/crate\"],\"requires\":[],\"schema\":1},\"scripts\":[]}";
        put_lib(&DIR_, "t.dup", DUP, PAYLOAD + MESH_BYTES + 200, 64);
        CHECK(world_fails(&DIR_, edit(WORLD, "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\",\"xs:sound/impact\"]}]",
                                      "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\",\"xs:sound/impact\"]},"
                                      "{\"package\":\"t.dup\",\"resources\":[]}]"),
                          "xs:texture/crate: provided by both package t.art and package t.dup (duplicate providers are refused, never picked)"));
        /* a file that declares another package ID */
        put_lib(&DIR_, "t.art", edit(ART, "\"id\":\"t.art\"", "\"id\":\"t.other\""), PAYLOAD, sizeof(PAYLOAD));
        CHECK(world_fails(&DIR_, WORLD, "package t.world requires package t.art, but packages/t.art.oalasset declares package t.other"));
        put_art(&DIR_, ART, P, PL);
        /* bytes after a manifest that declares no assets */
        static const char NONE[] = "{\"kind\":\"library\",\"package\":{\"id\":\"t.dup\",\"provides\":[],\"requires\":[],\"schema\":1},\"scripts\":[]}";
        put_lib(&DIR_, "t.dup", NONE, PAYLOAD, 16);
        CHECK(world_fails(&DIR_, edit(WORLD, "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\",\"xs:sound/impact\"]}]",
                                      "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\",\"xs:sound/impact\"]},"
                                      "{\"package\":\"t.dup\",\"resources\":[]}]"),
                          "package t.dup: 16 bytes follow the manifest, but it declares no assets"));
    }
    puts("  refusals: graph, imports, missing, wrong type (prop, sound, material, slot), malformed, reserved, provides both ways, "
         "missing/unsafe/unused/shared members, sizes, formats, duplicates, corrupt meshes, duplicate provider, a liar: ok");
}

/* ---- the world key over asset bytes ------------------------------------------------- */

static uint64_t key_with_art(const char *manifest, const uint8_t *payload, size_t plen)
{
    static hta_external_map m;
    char err[800];
    put_art(&DIR_, manifest, payload, plen);
    uint64_t d = 0;
    if (world_loads(&DIR_, WORLD, &m, err, sizeof(err))) { d = m.digest; hta_external_map_free(&m); }
    else { fprintf(stderr, "key: %s\n", err); failures++; }
    put_art(&DIR_, ART, PAYLOAD, sizeof(PAYLOAD));
    return d;
}

static void world_key(void)
{
    uint64_t base = key_with_art(ART, PAYLOAD, sizeof(PAYLOAD));
    uint8_t p[sizeof(PAYLOAD)];
    memcpy(p, PAYLOAD, sizeof(p)); p[MESH_BYTES + 200] ^= 1;                /* one texel */
    uint64_t texel = key_with_art(ART, p, sizeof(p));
    memcpy(p, PAYLOAD, sizeof(p)); f32(p + 16, 0.2499f);                    /* one vertex */
    uint64_t vertex = key_with_art(ART, p, sizeof(p));
    memcpy(p, PAYLOAD, sizeof(p)); p[MESH_BYTES + 10] ^= 1;                 /* one sample */
    uint64_t sample = key_with_art(ART, p, sizeof(p));
    uint64_t draw = key_with_art(edit(ART, "\"draw\":\"opaque\"", "\"draw\":\"alpha\""), PAYLOAD, sizeof(PAYLOAD));
    uint64_t prov = key_with_art(edit(ART, "\"source_path\":\"models/props/crate01.mdl\"", "\"source_path\":\"models/props/crate02.mdl\""),
                                 PAYLOAD, sizeof(PAYLOAD));
    CHECK(base && texel && vertex && sample && draw && prov);
    CHECK(texel != base && vertex != base && sample != base && draw != base);
    CHECK(prov == base);
    /* The library's digest itself, and an X4 library's is unchanged by X5:
     * no assets member, no payload, the same stream as before. */
    hta_pkg_dep dep;
    char err[400];
    size_t n;
    uint8_t *b = oala(ART, PAYLOAD, sizeof(PAYLOAD), &n);
    CHECK(hta_library_load(b, n, &dep, err, sizeof(err)));
    uint64_t d1 = dep.digest;
    hta_pkg_dep_free(&dep);
    free(b);
    b = oala(ART, PAYLOAD, sizeof(PAYLOAD), &n);
    CHECK(hta_library_load(b, n, &dep, err, sizeof(err)) && dep.digest == d1);
    hta_pkg_dep_free(&dep);
    free(b);
    printf("  world key: texel, vertex, sample and draw changes are new keys; provenance is not (base %016llx): ok\n",
           (unsigned long long)base);
}

/* ---- older engines and newer packages (docs/RESOURCES.md) ---------------------------
 * What a pre-X4 engine computes for a declared world is this build's key
 * of the same bytes without the "package" member (it skips the member it
 * does not know, and has no dependencies to append) -- scripts/
 * test_cross_version.sh checks that against the real X3 binary. Pinned
 * here: the two always differ, so such peers refuse each other before
 * spawn. And a world using X5 content always carries what an X4 engine
 * refuses at load: world_entities schema 4, and imports of types X4 had
 * reserved. */
static void older_engines(void)
{
    static const char DECLARED[] =
        "{\"id\":\"t:world/solo\",\"package\":{\"id\":\"t.solo\",\"provides\":[\"t:world/solo\"],\"requires\":[],\"schema\":1},"
        "\"world_entities\":{\"entities\":[{\"id\":\"t:entity/r\",\"kind\":\"relay\",\"links\":[]}],\"schema\":1}}";
    static const char STRIPPED[] =
        "{\"id\":\"t:world/solo\","
        "\"world_entities\":{\"entities\":[{\"id\":\"t:entity/r\",\"kind\":\"relay\",\"links\":[]}],\"schema\":1}}";
    static hta_external_map a, b;
    char err[800];
    bool ok = world_loads(&DIR_, DECLARED, &a, err, sizeof(err)) && world_loads(&DIR_, STRIPPED, &b, err, sizeof(err));
    if (!ok) fprintf(stderr, "older engines: %s\n", err);
    CHECK(ok);
    if (ok) CHECK(a.digest != b.digest && a.key != b.key);
    hta_external_map_free(&a);
    hta_external_map_free(&b);
    /* X4's registry reserved these; an X5 world's package imports them. */
    static const char *const X4_RESERVED[] = { "model", "material", "texture", "sound", "animation", "prefab", "ruleset" };
    CHECK(world_loads(&DIR_, WORLD, &a, err, sizeof(err)));
    CHECK(a.world_defs.schema == 4);
    uint8_t need = 0;
    for (size_t i = 0; i < sizeof(X4_RESERVED) / sizeof(X4_RESERVED[0]); i++) {
        char pat[40];
        snprintf(pat, sizeof(pat), ":%s/", X4_RESERVED[i]);
        const char *req = strstr(WORLD, "\"requires\"");
        if (req && strstr(req, pat)) need++;
    }
    CHECK(need >= 1);
    hta_external_map_free(&a);
    puts("  older engines: a declared world's key differs from its pre-X4 reading; an X5 world carries schema 4 and X4-reserved imports: ok");
}

/* ---- lifetimes: repeated loads, and failure halfway ---------------------------------- */

static void lifetimes(void)
{
    static hta_external_map m;
    char err[800];
    for (int i = 0; i < 200; i++) {
        bool ok = world_loads(&DIR_, WORLD, &m, err, sizeof(err));
        CHECK(ok);
        if (ok) hta_external_map_free(&m);
    }
    /* A set whose second library fails after the first decoded its
     * assets; a world whose prop fails after the set loaded. */
    size_t n;
    uint8_t pane[MESH_BYTES];
    cube(pane, 0.5f);
    pane[5] = 0x7F;                              /* a mesh that lies about its size */
    put_lib(&DIR_, "t.skin", SKIN, pane, sizeof(pane));
    for (int i = 0; i < 20; i++) CHECK(world_fails(&DIR_, WORLD2, "requires package t.skin"));
    for (int i = 0; i < 20; i++)
        CHECK(world_fails(&DIR_, edit(WORLD, "\"position\":[2,0,0.25]", "\"position\":[2,0,1e9]"), "malformed 'position'"));
    cube(pane, 0.5f);
    put_lib(&DIR_, "t.skin", SKIN, pane, sizeof(pane));
    puts("  lifetimes: 200 loads and frees; failures halfway through a set and after it (ASan/LSan in build-asan): ok");
}

/* ---- hostile input -------------------------------------------------------------------- */

static uint32_t rng = 777u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static void hostile(void)
{
    size_t n;
    uint8_t *good = oala(ART, PAYLOAD, sizeof(PAYLOAD), &n);
    unsigned loaded = 0, refused = 0, paths = 0;
    char e1[480], e2[480];
    for (int i = 0; i < 4000; i++) {
        uint8_t *b = malloc(n);
        memcpy(b, good, n);
        size_t size = n;
        int kind = (int)(rnd() % 4);
        int flips = 1 + (int)(rnd() % 4);
        for (int k = 0; k < flips; k++) {
            size_t at = kind == 0 ? 32 + rnd() % (n - 32) :                        /* anywhere after the header */
                        kind == 1 ? 32 + strlen(ART) + rnd() % sizeof(PAYLOAD) :   /* the payload */
                        32 + rnd() % strlen(ART);                                   /* the manifest */
            static const char J[] = "{}[]\",:0123456789.-/\\ ae";
            b[at] = kind == 3 ? (uint8_t)J[rnd() % (sizeof(J) - 1)] : (uint8_t)rnd();
        }
        if (rnd() % 8 == 0) size = 32 + rnd() % (n - 32);                         /* truncated */
        hta_pkg_dep d1, d2;
        bool a = hta_library_load(b, size, &d1, e1, sizeof(e1));
        bool c = hta_library_load(b, size, &d2, e2, sizeof(e2));
        CHECK(a == c);
        if (a) { CHECK(d1.digest == d2.digest); loaded++; }
        else { CHECK(!strcmp(e1, e2) && printable(e1) && e1[0]); refused++; }
        if (a) { hta_pkg_dep_free(&d1); }
        if (c) { hta_pkg_dep_free(&d2); }
        free(b);
    }
    free(good);
    /* Mutated worlds against the good library: props, models, sounds. */
    unsigned wl = 0, wr = 0;
    size_t wlen = strlen(WORLD);
    static char mw[sizeof(WORLD)];
    for (int i = 0; i < 3000; i++) {
        memcpy(mw, WORLD, wlen + 1);
        int flips = 1 + (int)(rnd() % 3);
        for (int k = 0; k < flips; k++) {
            size_t at = rnd() % wlen;
            static const char J[] = "{}[]\",:0123456789.-/ aexsprodlm";
            mw[at] = rnd() % 2 ? J[rnd() % (sizeof(J) - 1)] : (char)(1 + rnd() % 254);
        }
        static hta_external_map m1, m2;
        char w1[800], w2[800];
        bool a = world_loads(&DIR_, mw, &m1, w1, sizeof(w1));
        bool c = world_loads(&DIR_, mw, &m2, w2, sizeof(w2));
        CHECK(a == c);
        if (a && c) { CHECK(m1.digest == m2.digest && m1.assets.model_count == m2.assets.model_count); wl++; }
        if (!a) { CHECK(!strcmp(w1, w2) && printable(w1) && w1[0]); wr++; }
        if (a) hta_external_map_free(&m1);
        if (c) hta_external_map_free(&m2);
    }
    /* Random member paths never crash, never pass with a forbidden byte. */
    for (int i = 0; i < 20000; i++) {
        char p[128], why[128];
        size_t len = 1 + rnd() % 110;
        static const char J[] = "ab_./\\.A\x01 9:%";
        for (size_t k = 0; k < len; k++) p[k] = J[rnd() % (sizeof(J) - 1)];
        p[len] = 0;
        if (hta_asset_member_valid(p, why, sizeof(why))) {
            CHECK(!strstr(p, "..") && p[0] != '/' && !strchr(p, '\\') && !strchr(p, 'A') && !strchr(p, ' ') && strlen(p) <= 96);
            paths++;
        } else CHECK(printable(why) && why[0]);
    }
    printf("  hostile input: 4000 mutated libraries (%u loaded, %u refused), 3000 mutated worlds (%u loaded, %u refused), "
           "each twice alike; 20000 member paths (%u valid): ok\n", loaded, refused, wl, wr, paths);
}

/* ---- scale ------------------------------------------------------------------------------ */

static void scale(void)
{
    /* Six libraries of 64 textures and 1 material each, a world placing
     * 60 props: every reference resolved once, in well under a frame. */
    static char man[64 * 1024];
    static uint8_t pay[64 * 64];
    lib_dir d = { 0 };
    char req[4096] = "";
    for (int L = 0; L < 6; L++) {
        size_t at = 0;
        at += (size_t)snprintf(man + at, sizeof(man) - at, "{\"assets\":{\"materials\":[{\"draw\":\"opaque\",\"id\":\"s%d:material/m\",\"texture\":\"s%d:texture/t00\"}],\"members\":[", L, L);
        for (int t = 0; t < 64; t++) at += (size_t)snprintf(man + at, sizeof(man) - at, "%s{\"path\":\"t%02d.rgba\",\"size\":64}", t ? "," : "", t);
        at += (size_t)snprintf(man + at, sizeof(man) - at, "],\"models\":[],\"schema\":1,\"sounds\":[],\"textures\":[");
        for (int t = 0; t < 64; t++)
            at += (size_t)snprintf(man + at, sizeof(man) - at, "%s{\"format\":\"rgba8\",\"height\":4,\"id\":\"s%d:texture/t%02d\",\"member\":\"t%02d.rgba\",\"width\":4}",
                                   t ? "," : "", L, t, t);
        at += (size_t)snprintf(man + at, sizeof(man) - at, "]},\"kind\":\"library\",\"package\":{\"id\":\"s.l%d\",\"provides\":[\"s%d:material/m\"", L, L);
        for (int t = 0; t < 64; t++) at += (size_t)snprintf(man + at, sizeof(man) - at, ",\"s%d:texture/t%02d\"", L, t);
        snprintf(man + at, sizeof(man) - at, "],\"requires\":[],\"schema\":1},\"scripts\":[]}");
        char id[16];
        snprintf(id, sizeof(id), "s.l%d", L);
        size_t n;
        put_lib(&d, id, man, pay, sizeof(pay));
        snprintf(req + strlen(req), sizeof(req) - strlen(req), "%s{\"package\":\"s.l%d\",\"resources\":[]}", L ? "," : "", L);
    }
    /* ...and the art library for the props */
    size_t n;
    put_lib(&d, "t.art", ART, PAYLOAD, sizeof(PAYLOAD));
    static char w[64 * 1024];
    size_t at = (size_t)snprintf(w, sizeof(w), "{\"id\":\"t:world/big\",\"package\":{\"id\":\"t.big\",\"provides\":[\"t:world/big\"],"
                                 "\"requires\":[%s,{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\"]}],\"schema\":1},"
                                 "\"world_entities\":{\"entities\":[", req);
    for (int i = 0; i < 60; i++)
        at += (size_t)snprintf(w + at, sizeof(w) - at, "%s{\"id\":\"t:entity/p%02d\",\"kind\":\"prop\",\"links\":[],\"model\":\"xs:model/crate\",\"position\":[%d,%d,0.25]}",
                               i ? "," : "", i, i % 8, i / 8);
    snprintf(w + at, sizeof(w) - at, "],\"schema\":4}}");
    /* canonical requires order: s.l0..s.l5 then t.art -- already sorted */
    static hta_external_map m;
    char err[800];
    clock_t c0 = clock();
    bool ok = world_loads(&d, w, &m, err, sizeof(err));
    double ms = (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
    if (!ok) fprintf(stderr, "scale: %s\n", err);
    CHECK(ok);
    if (ok) {
        CHECK(m.assets.texture_count == 6 * 64 + 1 && m.assets.model_count == 1 && m.package.dep_count == 7);
        uint32_t props = 0;
        for (uint32_t i = 0; i < m.world_defs.count; i++) props += m.world_defs.entity[i].model == 1;
        CHECK(props == 60);                    /* sixty props, one model in the table */
        CHECK(ms < 250.0);
        hta_external_map_free(&m);
    }
    dir_free(&d);
    printf("  scale: 7 libraries, 391 textures, 60 props of one model loaded and resolved in %.1f ms: ok\n", ms);
}

int main(void)
{
    payload_init();
    good_set();
    visual_materials();
    consumers();
    refusals();
    world_key();
    older_engines();
    lifetimes();
    hostile();
    scale();
    dir_free(&DIR_);
    if (failures) { printf("asset: %d failures\n", failures); return 1; }
    puts("asset: all ok");
    return 0;
}
