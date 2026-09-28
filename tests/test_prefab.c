/* Prefabs (X6, docs/PREFABS.md): a library's prefab composed of another
 * library's model and sound and its own script; a world placing two
 * instances (one turned); expansion into ordinary entities with
 * deterministic, collision-free IDs; instance isolation; rotated and scaled
 * collision; every refusal; the world key over prefab bytes; limits, a
 * stress world, hostile input and load/unload lifetimes (ASan/UBSan in
 * build-asan). No game data. */
#include "asset/external_map.h"
#include "asset/package.h"
#include "asset/prefab.h"
#include "engine/world_entities.h"
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

/* ---- the art library (test_asset.c's): a 0.5 wu cube, a texture, a sound -- */

#define MESH_BYTES (16u + 24u * 40u + 36u * 4u + 12u)
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
        }
    uint8_t *ix = v + 24 * 40;
    for (int f = 0; f < 6; f++) {
        static const uint32_t q[6] = { 0, 1, 3, 0, 3, 2 };
        for (int k = 0; k < 6; k++) u32(ix + (f * 6 + k) * 4, (uint32_t)(f * 4) + q[k]);
    }
    uint8_t *g = ix + 36 * 4;
    u32(g, 0); u32(g + 4, 36); u32(g + 8, 0);
}

static const char ART[] =
    "{\"assets\":{\"materials\":[{\"draw\":\"opaque\",\"id\":\"xs:material/crate\",\"texture\":\"xs:texture/crate\"}],"
    "\"members\":[{\"path\":\"models/crate.mesh\",\"size\":1132},{\"path\":\"sounds/impact.pcm\",\"size\":200},"
    "{\"path\":\"textures/crate.rgba\",\"size\":64}],"
    "\"models\":[{\"format\":\"mesh1\",\"id\":\"xs:model/crate\",\"materials\":[\"xs:material/crate\"],\"member\":\"models/crate.mesh\"}],"
    "\"schema\":1,"
    "\"sounds\":[{\"channels\":1,\"format\":\"pcm_s16le\",\"frames\":100,\"id\":\"xs:sound/impact\",\"member\":\"sounds/impact.pcm\",\"rate\":22050}],"
    "\"textures\":[{\"format\":\"rgba8\",\"height\":4,\"id\":\"xs:texture/crate\",\"member\":\"textures/crate.rgba\",\"width\":4}]},"
    "\"kind\":\"library\",\"package\":{\"id\":\"t.art\",\"provides\":[\"xs:material/crate\",\"xs:model/crate\",\"xs:sound/impact\","
    "\"xs:texture/crate\"],\"requires\":[],\"schema\":1},\"scripts\":[]}";

static uint8_t PAYLOAD[MESH_BYTES + 200 + 64];

/* The facility library: one prefab of three children -- a button with the
 * library's own script and a link to its sibling door, a door drawn by the
 * art library's cube and sounding its impact, a post (a prop) -- and the
 * script. It imports what it uses from t.art. */
static const char FAC[] =
    "{\"kind\":\"library\",\"package\":{\"id\":\"t.fac\",\"provides\":[\"tf:prefab/door\",\"tf:script/log\"],"
    "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\",\"xs:sound/impact\"]}],\"schema\":1},"
    "\"prefabs\":{\"prefabs\":[{\"children\":["
    "{\"id\":\"button\",\"kind\":\"interactable\",\"links\":[{\"event\":\"used\",\"input\":\"toggle\",\"target\":\"door\"}],"
    "\"position\":[-0.2,-0.7,0.9],\"reach\":1.2,\"script\":\"tf:script/log\"},"
    "{\"id\":\"door\",\"kind\":\"mover\",\"links\":[],\"model\":\"xs:model/crate\",\"move\":[0,1.3,0],\"position\":[0,0,0.6],"
    "\"size\":[0.1,1.2,1.2],\"sound\":\"xs:sound/impact\",\"speed\":1.3},"
    "{\"id\":\"post\",\"kind\":\"prop\",\"links\":[],\"model\":\"xs:model/crate\",\"position\":[0,-0.9,0.25]}],"
    "\"id\":\"tf:prefab/door\"}],\"schema\":1},"
    "\"provenance\":{\"tf:prefab/door\":{\"creator\":\"test\"}},"
    "\"scripts\":[{\"api\":\"megamod.v1\",\"callbacks\":[\"on_used\"],\"id\":\"tf:script/log\","
    "\"source\":\"function on_used(e, p) log('used') end\"}]}";

/* A world that imports only the prefab and places it twice. */
static const char WORLD[] =
    "{\"id\":\"t:world/pf\",\"package\":{\"id\":\"t.world\",\"provides\":[\"t:world/pf\"],"
    "\"requires\":[{\"package\":\"t.fac\",\"resources\":[\"tf:prefab/door\"]}],\"schema\":1},"
    "\"world_entities\":{\"entities\":[],\"prefab_instances\":["
    "{\"id\":\"north\",\"position\":[0,3,0],\"prefab\":\"tf:prefab/door\"},"
    "{\"id\":\"south\",\"position\":[-3,-2,0],\"prefab\":\"tf:prefab/door\",\"yaw_degrees\":-90}],\"schema\":5}}";

/* ---- a package source in memory ------------------------------------------------ */

typedef struct { char id[80]; uint8_t *data; size_t size; } lib_file;
typedef struct { lib_file f[8]; unsigned count; } lib_dir;
static lib_dir DIR_;

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

static void put_lib(lib_dir *d, const char *id, const char *manifest, const uint8_t *payload, size_t plen)
{
    size_t n;
    uint8_t *b = oala(manifest, payload, plen, &n);
    for (unsigned i = 0; i < d->count; i++)
        if (!strcmp(d->f[i].id, id)) { free(d->f[i].data); d->f[i].data = b; d->f[i].size = n; return; }
    snprintf(d->f[d->count].id, sizeof(d->f[0].id), "%s", id);
    d->f[d->count].data = b; d->f[d->count].size = n;
    d->count++;
}

static void drop_lib(lib_dir *d, const char *id)
{
    for (unsigned i = 0; i < d->count; i++)
        if (!strcmp(d->f[i].id, id)) { free(d->f[i].data); d->f[i] = d->f[--d->count]; return; }
}

static void dir_free(lib_dir *d)
{
    for (unsigned i = 0; i < d->count; i++) free(d->f[i].data);
    memset(d, 0, sizeof(*d));
}

static char *edit(const char *base, const char *from, const char *to)
{
    static char out[2][32768];
    static int k;
    char *o = out[k++ & 1];
    const char *at = strstr(base, from);
    if (!at) { fprintf(stderr, "edit: '%s' not found\n", from); abort(); }
    snprintf(o, sizeof(out[0]), "%.*s%s%s", (int)(at - base), base, to, at + strlen(from));
    return o;
}

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
    float xyz[3][3] = { { -8, -8, 0 }, { 8, -8, 0 }, { 0, 8, 0 } };
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

static bool printable(const char *s)
{
    for (; *s; s++) if ((unsigned char)*s < 0x20 || (unsigned char)*s >= 0x7F) return false;
    return true;
}

static bool world_loads(const char *manifest, hta_external_map *m, char *err, size_t n)
{
    size_t size;
    uint8_t *b = oalmap(manifest, &size);
    hta_pkg_source src = { dir_open, NULL, &DIR_ };
    bool ok = hta_external_map_load_with(b, size, &src, m, err, n);
    free(b);
    return ok;
}

static void reset_libs(void)
{
    put_lib(&DIR_, "t.art", ART, PAYLOAD, sizeof(PAYLOAD));
    put_lib(&DIR_, "t.fac", FAC, NULL, 0);
}

/* The world (possibly edited) against the facility (possibly edited) must
 * be refused saying `want`. */
static bool refused(const char *world, const char *fac, const char *want)
{
    static hta_external_map m;
    char err[800];
    reset_libs();
    if (fac) put_lib(&DIR_, "t.fac", fac, NULL, 0);
    bool ok = world_loads(world ? world : WORLD, &m, err, sizeof(err));
    if (ok) hta_external_map_free(&m);
    reset_libs();
    if (ok || !strstr(err, want) || !printable(err)) {
        fprintf(stderr, "refusal: %s (wanted '%s')\n", ok ? "loaded" : err, want);
        return false;
    }
    return true;
}

static int32_t find(const hta_world_defs *d, const char *id) { return hta_world_defs_find(d, id); }

/* A ray (from, along dir, 3 wu) against the world's entity collision. */
static bool ray_hits(hta_world_entities *w, const float from[3], const float dir[3], float *t)
{
    static hta_vertex v[3];
    static uint32_t ix[3] = { 0, 1, 2 };
    static hta_bsp_mesh ground;
    static hta_collision c;
    static bool built;
    if (!built) {
        float p[3][2] = { { -50, -50 }, { 50, -50 }, { 0, 50 } };
        for (int i = 0; i < 3; i++) { v[i].pos[0] = p[i][0]; v[i].pos[1] = p[i][1]; v[i].pos[2] = -5; v[i].normal[2] = 1; }
        ground.vertices = v; ground.vertex_count = 3; ground.indices = ix; ground.index_count = 3;
        ground.bounds_min[0] = ground.bounds_min[1] = -50; ground.bounds_max[0] = ground.bounds_max[1] = 50;
        ground.bounds_min[2] = -6; ground.bounds_max[2] = 6;
        assert(hta_collision_build(&c, &ground));
        built = true;
    }
    static hta_collision_instance inst[HTA_WDEF_MAX_ENTITIES];
    c.instances = inst;
    c.instance_count = hta_went_instances(w, inst, HTA_WDEF_MAX_ENTITIES);
    c.instance_index = NULL;
    float hit[3], nrm[3];
    return hta_collision_ray(&c, from, dir, 3.0f, t, hit, nrm);
}

/* ---- the good world ------------------------------------------------------------ */

static void good_world(void)
{
    static hta_external_map m;
    char err[800];
    reset_libs();
    bool ok = world_loads(WORLD, &m, err, sizeof(err));
    if (!ok) fprintf(stderr, "good world: %s\n", err);
    CHECK(ok);
    if (!ok) return;
    const hta_world_defs *d = &m.world_defs;
    /* Expansion: instances by ID, children by local ID, ordinary entities. */
    CHECK(d->schema == 5 && d->count == 6 && d->prefab_instance_count == 2);
    static const char *const IDS[6] = { "t:entity/north__button", "t:entity/north__door", "t:entity/north__post",
                                        "t:entity/south__button", "t:entity/south__door", "t:entity/south__post" };
    for (int i = 0; i < 6; i++) CHECK(!strcmp(d->entity[i].id, IDS[i]));
    CHECK(d->entity[0].kind == HTA_WDEF_INTERACTABLE && d->entity[1].kind == HTA_WDEF_MOVER && d->entity[2].kind == HTA_WDEF_PROP);
    CHECK(d->entity[4].instance == 2 && d->entity[4].child == 1 && d->prefab_instance[1].first == 3 && d->prefab_instance[1].count == 3);
    CHECK(!strcmp(d->prefab_instance[0].prefab, "tf:prefab/door") && !strcmp(m.package.dep[d->prefab_instance[0].provider - 1], "t.fac"));
    /* Links go to the SAME instance's door: north 0 -> 1, south 3 -> 4. */
    CHECK(d->link_count == 2 && d->link[d->entity[0].first_link].target == 1 && d->link[d->entity[3].first_link].target == 4);
    /* Transforms: north untouched; south turned -90: +x -> -y, +y -> +x (exact). */
    CHECK(d->entity[0].pos[0] == -0.2f && d->entity[0].pos[1] == 2.3f && d->entity[0].pos[2] == 0.9f);
    CHECK(d->entity[3].pos[0] == -3.0f + -0.7f && d->entity[3].pos[1] == -2.0f + 0.2f);
    const hta_wmover_def *nd = hta_wdef_mover(d, 1), *sd = hta_wdef_mover(d, 4);
    CHECK(nd && sd && nd != sd && nd->generated && !nd->id[0]);
    CHECK(nd->move[0] == 0.0f && nd->move[1] == 1.3f && sd->move[0] == 1.3f && sd->move[1] == 0.0f);
    CHECK(d->entity[4].xform && d->entity[4].rot_c == 0.0f && d->entity[4].rot_s == -1.0f && d->entity[1].rot_c == 1.0f);
    CHECK(nd->sound == 1 && d->entity[1].model == 1 && d->entity[2].model == 1);
    /* The prefab's script is in the world's table, from its provider, once. */
    CHECK(d->script_count == 1 && !strcmp(d->script[0].id, "tf:script/log") &&
          !strcmp(m.package.dep[d->script[0].provider - 1], "t.fac") && d->entity[0].script == 1 && d->entity[3].script == 1);
    /* Implementation dependencies are loaded (and keyed), not imported. */
    CHECK(m.package.dep_count == 2 && !strcmp(m.package.dep[0], "t.art") && !m.package.dep_direct[0] && m.package.dep_direct[1]);
    /* A generated ID is an ordinary placed ID: world.entity finds it. */
    CHECK(find(d, "t:entity/south__door") == 4 && find(d, "tf:prefab/door") < 0);

    /* Runtime: ordinary entities, ordinary handles. */
    static hta_world_entities w;
    CHECK(hta_went_load(&w, d, err, sizeof(err)));
    hta_went_handle hn = hta_went_handle_of(&w, 1), hs = hta_went_handle_of(&w, 4);
    CHECK(hn && hs && hn != hs && hta_went_resolve(&w, hn) == 1 && hta_went_resolve(&w, hs) == 4);
    /* North's button, used, opens north's door only. */
    const float eye_n[3] = { -1.0f, 2.3f, 0.9f }, fwd_n[3] = { 1, 0, 0 };
    CHECK(hta_went_interact(&w, 1, eye_n, fwd_n) == 0);
    CHECK(w.call_count == 1 && w.calls[0].entity == 0);
    for (int i = 0; i < 120; i++) hta_went_step(&w, 1.0f / 60.0f);
    CHECK(w.st[1].phase == HTA_MOVER_OPEN && w.st[4].phase == HTA_MOVER_CLOSED);
    /* South's, used, opens south's only; north stays as it was. */
    const float eye_s[3] = { -3.7f, -1.0f, 0.9f }, fwd_s[3] = { 0, -1, 0 };
    CHECK(hta_went_interact(&w, 2, eye_s, fwd_s) == 3);
    hta_went_step(&w, 1.0f / 60.0f);
    CHECK(w.st[4].phase == HTA_MOVER_OPENING && w.st[1].phase == HTA_MOVER_OPEN);
    CHECK(w.cue_count == 1 && w.cues[0].entity == 4);
    for (int i = 0; i < 120; i++) hta_went_step(&w, 1.0f / 60.0f);
    float off[3];
    hta_went_offset(&w, 4, off);
    CHECK(fabsf(off[0] - 1.3f) < 1e-5f && fabsf(off[1]) < 1e-6f);          /* south slides along +x */
    /* Late join: state, per instance, not history. */
    hta_went_mover_state snap[8];
    uint32_t sn = hta_went_snapshot(&w, snap, 8);
    CHECK(sn == 2 && snap[0].index == 1 && snap[1].index == 4);
    static hta_world_entities j;
    CHECK(hta_went_load(&j, d, err, sizeof(err)));
    j.remote = true;
    hta_went_mover_state closed_north = snap[0];
    closed_north.phase = HTA_MOVER_CLOSED; closed_north.t_q = 0;
    CHECK(hta_went_apply(&j, &closed_north, true) && hta_went_apply(&j, &snap[1], true));
    CHECK(j.st[1].phase == HTA_MOVER_CLOSED && j.st[4].phase == HTA_MOVER_OPEN && j.st[4].t == 1.0f);
    CHECK(hta_went_interact(&j, 1, eye_n, fwd_n) < 0);    /* a joiner never decides */
    /* A round reset: generations move on, doors close, old handles stale. */
    hta_went_reset(&w);
    CHECK(hta_went_resolve(&w, hn) < 0 && hta_went_resolve(&w, hs) < 0 && w.st[1].phase == HTA_MOVER_CLOSED &&
          w.st[4].phase == HTA_MOVER_CLOSED);
    hta_went_step(&w, 1.0f / 60.0f);
    CHECK(w.cue_count == 0);

    /* Collision follows the transform. North's door (closed) blocks a ray
     * along +x at y = 3; south's door, turned, blocks one along -y at x = -3
     * and not one along +x there (it is 0.1 thick along y, 1.2 along x). */
    float t;
    const float fn[3] = { -1.5f, 3.0f, 0.6f }, dx[3] = { 1, 0, 0 };
    CHECK(ray_hits(&w, fn, dx, &t) && fabsf(t - 1.45f) < 0.01f);
    const float fs[3] = { -3.0f, -0.5f, 0.6f }, dny[3] = { 0, -1, 0 };
    CHECK(ray_hits(&w, fs, dny, &t) && fabsf(t - 1.45f) < 0.01f);
    const float fsx[3] = { -5.0f, -2.0f, 0.6f };
    CHECK(ray_hits(&w, fsx, dx, &t) && fabsf(t - 1.4f) < 0.01f);      /* its 1.2 wu face, 2 - 0.6 */
    /* Opened, south's door has slid +1.3 x: the doorway at x = -3 is clear. */
    CHECK(hta_went_send(&w, 4, HTA_WIN_OPEN, HTA_WENT_NO_ACTOR));
    for (int i = 0; i < 120; i++) hta_went_step(&w, 1.0f / 60.0f);
    CHECK(!ray_hits(&w, fs, dny, &t) || t > 2.0f);
    /* Posts are props, solid where drawn: north's at (0, 2.1). */
    const float fp[3] = { -1.0f, 2.1f, 0.25f };
    CHECK(ray_hits(&w, fp, dx, &t) && fabsf(t - 0.75f) < 0.01f);
    hta_went_free(&w); hta_went_free(&j);
    hta_external_map_free(&m);
    puts("  good world: two instances expand to 6 ordinary entities (IDs, order, links, transforms, script, deps); "
         "north's button moves north's door only, south's south's; late join per instance; reset; turned collision: ok");
}

/* ---- a rotated, scaled prop: the oriented box, drawn and solid ------------------- */

static void turned_props(void)
{
    static hta_external_map m;
    char err[800];
    reset_libs();
    const char *w = edit(WORLD, "\"yaw_degrees\":-90}", "\"scale\":2,\"yaw_degrees\":45}");
    bool ok = world_loads(w, &m, err, sizeof(err));
    if (!ok) fprintf(stderr, "turned: %s\n", err);
    CHECK(ok);
    if (!ok) return;
    const hta_world_defs *d = &m.world_defs;
    const hta_wdef *post = &d->entity[5];
    /* The post: at (-3,-2) + 2 * R45 * (0,-0.9,0.25): a 1 wu cube, turned. */
    float c = (float)cos(M_PI / 4), s = (float)sin(M_PI / 4);
    CHECK(fabsf(post->pos[0] - (-3.0f + 2 * (s * 0.9f))) < 1e-5f && fabsf(post->pos[1] - (-2.0f - 2 * (c * 0.9f))) < 1e-5f);
    CHECK(post->xform && post->scale == 2.0f && fabsf(post->box_h[0] - 0.5f) < 1e-6f && fabsf(post->box_c[2] - 0.5f) < 1e-6f);
    /* Its world-axis box surrounds the turned one: half width 0.5 * sqrt 2. */
    CHECK(fabsf((post->max[0] - post->min[0]) * 0.5f - 0.70711f) < 1e-4f);
    /* The door scaled: 2x the size, move and speed (the same time to open). */
    const hta_wmover_def *sd = hta_wdef_mover(d, 4);
    CHECK(fabsf(sd->size[1] - 2.4f) < 1e-6f && fabsf(sd->speed - 2.6f) < 1e-6f &&
          fabsf(sqrtf(sd->move[0] * sd->move[0] + sd->move[1] * sd->move[1]) - 2.6f) < 1e-5f);
    CHECK(fabsf(d->entity[3].reach - 2.4f) < 1e-6f);
    static hta_world_entities we;
    CHECK(hta_went_load(&we, d, err, sizeof(err)));
    /* A ray at the post's corner direction: an axis box would be hit 0.2 wu
     * early; the turned box is hit at its face, 0.5 from its centre along
     * its own axis. From 2 wu out along the box's local -y axis: */
    float ax[3] = { s, -c, 0 };           /* R45 * (0,-1,0) */
    float from[3] = { post->box_c[0] + ax[0] * 2.0f, post->box_c[1] + ax[1] * 2.0f, post->box_c[2] };
    float dir[3] = { -ax[0], -ax[1], 0 }, t;
    CHECK(ray_hits(&we, from, dir, &t) && fabsf(t - 1.5f) < 0.01f);
    /* Along world -x through its centre the turned face is 0.5*sqrt2 out. */
    float from2[3] = { post->box_c[0] + 2.0f, post->box_c[1], post->box_c[2] }, dir2[3] = { -1, 0, 0 };
    CHECK(ray_hits(&we, from2, dir2, &t) && fabsf(t - (2.0f - 0.70711f)) < 0.01f);
    /* Straight down beside the turned box's corner, inside its axis box
     * (|dx| + |dy| = 1.2 > the diamond's 1.0): clear. Down its middle: hit. */
    float from3[3] = { post->box_c[0] + 0.6f, post->box_c[1] + 0.6f, post->box_c[2] + 1.5f }, dir3[3] = { 0, 0, -1 };
    CHECK(!ray_hits(&we, from3, dir3, &t));
    float from4[3] = { post->box_c[0] + 0.3f, post->box_c[1] + 0.3f, post->box_c[2] + 1.5f };
    CHECK(ray_hits(&we, from4, dir3, &t) && fabsf(t - 1.0f) < 0.01f);
    hta_went_free(&we);
    hta_external_map_free(&m);
    /* A world-authored prop may carry a transform too (schema 5). */
    const char *pw =
        "{\"id\":\"t:world/pf\",\"package\":{\"id\":\"t.world\",\"provides\":[\"t:world/pf\"],"
        "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\"]}],\"schema\":1},"
        "\"world_entities\":{\"entities\":[{\"id\":\"t:entity/crate\",\"kind\":\"prop\",\"links\":[],\"model\":\"xs:model/crate\","
        "\"position\":[1,1,0.25],\"scale\":1.5,\"yaw_degrees\":30}],\"schema\":5}}";
    ok = world_loads(pw, &m, err, sizeof(err));
    CHECK(ok && m.world_defs.entity[0].xform && m.world_defs.entity[0].scale == 1.5f);
    if (ok) hta_external_map_free(&m);
    CHECK(refused(edit(edit(pw, "\"schema\":5", "\"schema\":4"), ",\"scale\":1.5", ""), NULL,
                  "t:entity/crate: a prop's yaw_degrees and scale need world_entities schema 5"));
    puts("  turned and scaled: a 45-degree, 2x instance -- oriented box collision, not its axis box; scaled mover and reach; "
         "a world prop's own transform (schema 5 only): ok");
}

/* ---- refusals ------------------------------------------------------------------ */

static const char FAC_MINIMAL_HEAD[] = "\"prefabs\":{\"prefabs\":[{\"children\":[";

static void refusals(void)
{
    /* The world's side. */
    CHECK(refused(edit(WORLD, "\"resources\":[\"tf:prefab/door\"]", "\"resources\":[]"), NULL,
                  "prefab instance north: prefab tf:prefab/door is provided by package t.fac, which package t.world requires but does not import it from"));
    CHECK(refused(edit(WORLD, "{\"package\":\"t.fac\",\"resources\":[\"tf:prefab/door\"]}", ""), NULL,
                  "prefab instance north references missing prefab tf:prefab/door (no package in this set provides namespace 'tf': is a requirement missing?)"));
    drop_lib(&DIR_, "t.fac");
    {
        static hta_external_map m;
        char err[800];
        put_lib(&DIR_, "t.art", ART, PAYLOAD, sizeof(PAYLOAD));
        CHECK(!world_loads(WORLD, &m, err, sizeof(err)) &&
              strstr(err, "package t.world requires package t.fac, but it is not present (looked for packages/t.fac.oalasset)"));
    }
    CHECK(refused(edit(WORLD, "\"prefab\":\"tf:prefab/door\"}", "\"prefab\":\"tf:prefab/dor\"}"), NULL,
                  "prefab instance north references missing prefab tf:prefab/dor"));
    CHECK(refused(edit(WORLD, "\"prefab\":\"tf:prefab/door\"}", "\"prefab\":\"tf:script/log\"}"), NULL,
                  "prefab instance north: prefab tf:script/log is a script, expected a prefab"));
    CHECK(refused(edit(WORLD, "\"prefab\":\"tf:prefab/door\"}", "\"prefab\":\"TF:prefab/door\"}"), NULL,
                  "prefab instance north: prefab 'TF:prefab/door' is not a resource ID: namespace has capital 'T'"));
    CHECK(refused(edit(WORLD, "{\"id\":\"north\"", "{\"id\":\"North\""), NULL,
                  "prefab instance 'North': instance id has capital 'N'"));
    CHECK(refused(edit(WORLD, "{\"id\":\"north\"", "{\"id\":\"no__rth\""), NULL,
                  "prefab instance 'no__rth': instance id has '__'"));
    CHECK(refused(edit(WORLD, "{\"id\":\"north\"", "{\"id\":\"south\""), NULL, "prefab instance south appears twice"));
    CHECK(refused(edit(WORLD, "{\"id\":\"north\"", "{\"id\":\"zulu\""), NULL,
                  "world_entities: prefab_instances are not in canonical (byte) order of id at south"));
    CHECK(refused(edit(WORLD, "\"yaw_degrees\":-90", "\"scale\":0"), NULL, "prefab instance south: scale 0 out of range (uniform, 0.25 to 4)"));
    CHECK(refused(edit(WORLD, "\"yaw_degrees\":-90", "\"scale\":-1"), NULL, "prefab instance south: scale -1 out of range"));
    CHECK(refused(edit(WORLD, "\"yaw_degrees\":-90", "\"scale\":1e30"), NULL, "prefab instance south: scale 1e+30 out of range"));
    CHECK(refused(edit(WORLD, "\"yaw_degrees\":-90", "\"scale\":1e999"), NULL, "prefab instance south: malformed 'scale'"));
    CHECK(refused(edit(WORLD, "\"yaw_degrees\":-90", "\"yaw_degrees\":720"), NULL, "prefab instance south: yaw_degrees out of range (|yaw| <= 360)"));
    CHECK(refused(edit(WORLD, "[-3,-2,0]", "[-3,-2,5000]"), NULL, "prefab instance south: position must be finite and inside the world"));
    CHECK(refused(edit(WORLD, "[-3,-2,0]", "[-3,-2,NaN]"), NULL, "malformed manifest"));
    CHECK(refused(edit(WORLD, "\"yaw_degrees\":-90", "\"color\":1"), NULL, "prefab instance south: unknown field 'color'"));
    CHECK(refused(edit(WORLD, "\"schema\":5}}", "\"schema\":4}}"), NULL, "world_entities: prefab instances need schema 5"));
    CHECK(refused(edit(WORLD, "\"entities\":[]", "\"entities\":[{\"id\":\"t:entity/north__door\",\"kind\":\"relay\",\"links\":[]}]"), NULL,
                  "t:entity/north__door: '__' is reserved for prefab children (<instance>__<child>)"));
    /* A world link may name a child (an ordinary entity); a child a world entity cannot. */
    {
        static hta_external_map m;
        char err[800];
        reset_libs();
        const char *w = edit(WORLD, "\"entities\":[]", "\"entities\":[{\"id\":\"t:entity/lockdown\",\"kind\":\"interactable\","
                             "\"links\":[{\"event\":\"used\",\"input\":\"close\",\"target\":\"t:entity/south__door\"}],"
                             "\"position\":[0,0,1],\"reach\":1}]");
        bool ok = world_loads(w, &m, err, sizeof(err));
        if (!ok) fprintf(stderr, "world link to a child: %s\n", err);
        CHECK(ok && m.world_defs.link[0].target == 5 && m.world_defs.entity[0].id[9] == 'l');
        if (ok) hta_external_map_free(&m);
        CHECK(refused(edit(w, "south__door", "south__dor"), NULL, "t:entity/lockdown references missing placed entity t:entity/south__dor"));
    }

    /* The prefab's side (the provider's words). */
    CHECK(refused(NULL, edit(FAC, "\"model\":\"xs:model/crate\",\"move\"", "\"model\":\"xs:model/crates\",\"move\""),
                  "prefab tf:prefab/door child 'door' references missing model xs:model/crates"));
    CHECK(refused(NULL, edit(FAC, "\"model\":\"xs:model/crate\",\"move\"", "\"model\":\"xs:sound/impact\",\"move\""),
                  "prefab tf:prefab/door child 'door': model xs:sound/impact is a sound, expected a model"));
    CHECK(refused(NULL, edit(FAC, "\"model\":\"xs:model/crate\",\"position\":[0,-0.9", "\"model\":\"Xs:model/crate\",\"position\":[0,-0.9"),
                  "prefab tf:prefab/door child 'post': model 'Xs:model/crate' is not a resource ID: namespace has capital 'X'"));
    CHECK(refused(NULL, edit(FAC, "\"resources\":[\"xs:model/crate\",\"xs:sound/impact\"]", "\"resources\":[\"xs:model/crate\"]"),
                  "prefab tf:prefab/door child 'door': sound xs:sound/impact is provided by package t.art, which package t.fac requires but does not import it from"));
    CHECK(refused(NULL, edit(FAC, "\"script\":\"tf:script/log\"", "\"script\":\"tf:script/lag\""),
                  "prefab tf:prefab/door child 'button' references missing script tf:script/lag"));
    CHECK(refused(NULL, edit(FAC, "\"callbacks\":[\"on_used\"]", "\"callbacks\":[\"on_ability\"]"),
                  "prefab tf:prefab/door child 'button': script tf:script/log does not declare on_used"));
    CHECK(refused(NULL, edit(FAC, "\"target\":\"door\"", "\"target\":\"dor\""),
                  "prefab tf:prefab/door child 'button' references missing child 'dor'"));
    CHECK(refused(NULL, edit(FAC, "\"target\":\"door\"", "\"target\":\"button\""), "prefab tf:prefab/door child 'button' links to itself"));
    CHECK(refused(NULL, edit(FAC, "\"target\":\"door\"", "\"target\":\"post\""),
                  "prefab tf:prefab/door child 'button' links to child 'post', and a prop does not accept 'toggle'"));
    CHECK(refused(NULL, edit(FAC, "{\"id\":\"post\"", "{\"id\":\"door\""), "prefab tf:prefab/door contains duplicate local child id 'door'"));
    CHECK(refused(NULL, edit(FAC, "{\"id\":\"post\"", "{\"id\":\"alpha\""),
                  "prefab tf:prefab/door: children are not in canonical (byte) order of local id at 'alpha'"));
    CHECK(refused(NULL, edit(FAC, "{\"id\":\"post\"", "{\"id\":\"Post\""), "prefab tf:prefab/door child 'Post': local child id has capital 'P'"));
    CHECK(refused(NULL, edit(FAC, "{\"id\":\"post\",\"kind\":\"prop\"", "{\"id\":\"post\",\"kind\":\"widget\""),
                  "prefab tf:prefab/door child 'post': unknown kind 'widget'"));
    CHECK(refused(NULL, edit(FAC, "{\"id\":\"post\",\"kind\":\"prop\"", "{\"id\":\"post\",\"kind\":\"prefab\""),
                  "prefab tf:prefab/door child 'post' contains a nested prefab reference, which is not supported in prefab schema 1"));
    CHECK(refused(NULL, edit(FAC, "{\"id\":\"post\",\"kind\":\"prop\",\"links\":[]", "{\"id\":\"post\",\"kind\":\"prop\",\"links\":[],\"prefab\":\"tf:prefab/door\""),
                  "prefab tf:prefab/door child 'post' contains a nested prefab reference, which is not supported in prefab schema 1"));
    CHECK(refused(NULL, edit(FAC, "\"reach\":1.2", "\"reach\":1.2,\"speed\":1"), "prefab tf:prefab/door child 'button': an interactable does not take 'speed'"));
    CHECK(refused(NULL, edit(FAC, ",\"speed\":1.3", ""), "prefab tf:prefab/door child 'door': a mover needs 'speed'"));
    CHECK(refused(NULL, edit(FAC, "\"speed\":1.3", "\"speed\":1e999"), "t.fac: malformed manifest"));     /* non-finite: refused by the strict reader first */
    CHECK(refused(NULL, edit(FAC, "[0,-0.9,0.25]", "[0,-900,0.25]"), "prefab tf:prefab/door child 'post': malformed or out-of-range 'position'"));
    CHECK(refused(NULL, edit(FAC, "\"speed\":1.3", "\"speed\":0"), "prefab tf:prefab/door child 'door': mover speed out of range"));
    CHECK(refused(NULL, edit(FAC, "\"id\":\"tf:prefab/door\"", "\"id\":\"tf:prefab/Door\""),
                  "package t.fac: prefab 'tf:prefab/Door' is not a resource ID: name has capital 'D'"));
    CHECK(refused(NULL, edit(FAC, "\"id\":\"tf:prefab/door\"}", "\"id\":\"tf:prefab/door\",\"extends\":\"tf:prefab/base\"}"),
                  "package t.fac: prefab tf:prefab/door: unknown field 'extends' (a prefab has children, id)"));
    CHECK(refused(NULL, edit(FAC, "\"id\":\"tf:prefab/door\"}],\"schema\":1}", "\"id\":\"tf:prefab/door\"}],\"schema\":3}"),
                  "package t.fac: unsupported prefab schema 3 (this engine has 2)"));
    CHECK(refused(NULL, edit(FAC, "\"provides\":[\"tf:prefab/door\",", "\"provides\":["),
                  "package t.fac has prefab tf:prefab/door but does not list it in provides"));
    CHECK(refused(NULL, edit(FAC, "\"provides\":[\"tf:prefab/door\",", "\"provides\":[\"tf:prefab/aaa\",\"tf:prefab/door\","),
                  "package t.fac lists tf:prefab/aaa in provides, but has no such prefab"));
    CHECK(refused(NULL, edit(FAC, "\"prefabs\":{", "\"prefabs\":[],\"prefabz\":{"), "t.fac: prefabs is not an object"));
    CHECK(refused(NULL, edit(FAC, "{\"children\":[", "{\"children\":[],\"id\":\"tf:prefab/aaa\"},{\"children\":["),
                  "prefab tf:prefab/aaa has no children"));
    /* A link cycle inside a prefab: two relays feeding each other. */
    const char *cyc = edit(FAC, "{\"id\":\"post\"",
                           "{\"id\":\"on\",\"kind\":\"relay\",\"links\":[{\"event\":\"fired\",\"input\":\"activate\",\"target\":\"one\"}]},"
                           "{\"id\":\"one\",\"kind\":\"relay\",\"links\":[{\"event\":\"fired\",\"input\":\"activate\",\"target\":\"on\"}]},"
                           "{\"id\":\"post\"");
    CHECK(refused(NULL, cyc, "prefab tf:prefab/door: link cycle: on -> one -> on"));
    /* A duplicate provider of a prefab. */
    put_lib(&DIR_, "t.dup", "{\"kind\":\"library\",\"package\":{\"id\":\"t.dup\",\"provides\":[\"tf:prefab/door\"],\"requires\":[],\"schema\":1},"
                           "\"prefabs\":{\"prefabs\":[{\"children\":[{\"id\":\"a\",\"kind\":\"relay\",\"links\":[]}],\"id\":\"tf:prefab/door\"}],"
                           "\"schema\":1},\"scripts\":[]}", NULL, 0);
    CHECK(refused(edit(WORLD, "\"requires\":[{", "\"requires\":[{\"package\":\"t.dup\",\"resources\":[]},{"), NULL,
                  "tf:prefab/door: provided by both package t.dup and package t.fac (duplicate providers are refused, never picked)"));
    drop_lib(&DIR_, "t.dup");
    /* A trigger child needs an axis-aligned instance. */
    const char *trig = edit(FAC, "{\"id\":\"post\"", "{\"id\":\"pad\",\"kind\":\"trigger\",\"bounds\":{\"max\":[1,1,1],\"min\":[0,0,0]},"
                            "\"links\":[{\"event\":\"entered\",\"input\":\"open\",\"target\":\"door\"}]},{\"id\":\"post\"");
    CHECK(refused(edit(WORLD, "\"yaw_degrees\":-90", "\"yaw_degrees\":30"), trig,
                  "prefab instance south (tf:prefab/door) child 'pad': a trigger is an axis-aligned box, so the instance's yaw must be a multiple of 90 degrees (is 30)"));
    {
        static hta_external_map m;
        char err[800];
        reset_libs();
        put_lib(&DIR_, "t.fac", trig, NULL, 0);
        bool ok = world_loads(WORLD, &m, err, sizeof(err));
        if (!ok) fprintf(stderr, "trigger at -90: %s\n", err);
        CHECK(ok);
        /* pad (0..1)^3 turned -90 at (-3,-2): x in [-3,-2], y in [-3,-2]. */
        if (ok) {
            const hta_wdef *pad = &m.world_defs.entity[m.world_defs.prefab_instance[1].first + 2];
            CHECK(pad->kind == HTA_WDEF_TRIGGER && pad->min[0] == -3.0f && pad->max[0] == -2.0f && pad->min[1] == -3.0f && pad->max[1] == -2.0f);
            hta_external_map_free(&m);
        }
        reset_libs();
    }
    /* Scale pushing a child past the world's limits. */
    CHECK(refused(edit(WORLD, "\"yaw_degrees\":-90", "\"scale\":4"), NULL,
                  "prefab instance south (tf:prefab/door) child 'button': reach 4.8 after scale 4 exceeds 4 wu"));
    puts("  refusals: instance ID, order, duplicates, transform (zero/negative/huge/non-finite scale, yaw, position), schema, "
         "'__', missing/unimported/wrong-type/malformed prefab, absent provider; child references (missing, wrong type, malformed, "
         "unimported, script callback), local links (missing, self, not accepted, cycle), duplicate/unsorted/bad local IDs, "
         "unknown kind, nesting, fields, numbers, inheritance, schema, provides both ways, duplicate provider, trigger yaw, scale "
         "limits: ok");
}

/* ---- limits, and a stress world --------------------------------------------------- */

static char *instances_world(unsigned count, const char *prefab)
{
    static char w[16384];
    size_t at = (size_t)snprintf(w, sizeof(w),
        "{\"id\":\"t:world/pf\",\"package\":{\"id\":\"t.world\",\"provides\":[\"t:world/pf\"],"
        "\"requires\":[{\"package\":\"t.fac\",\"resources\":[\"%s\"]}],\"schema\":1},"
        "\"world_entities\":{\"entities\":[],\"prefab_instances\":[", prefab);
    for (unsigned i = 0; i < count; i++)
        at += (size_t)snprintf(w + at, sizeof(w) - at, "%s{\"id\":\"i%03u\",\"position\":[%d,%d,0],\"prefab\":\"%s\",\"yaw_degrees\":%u}",
                               i ? "," : "", i, (int)(i % 8) * 3 - 12, (int)(i / 8) * 3 - 12, prefab, (i * 37) % 360);
    snprintf(w + at, sizeof(w) - at, "],\"schema\":5}}");
    return w;
}

static void limits(void)
{
    /* X8: 65 instances of 16 children: 1040 runtime objects, over 1024 --
     * refused before anything is written past the limit. (Until X8, 22
     * instances of 3 -- 66 -- were over 64.) */
    char big[16384];
    size_t at = (size_t)snprintf(big, sizeof(big), "{\"kind\":\"library\",\"package\":{\"id\":\"t.fac\",\"provides\":[\"tf:prefab/door\"],"
                                 "\"requires\":[],\"schema\":1},\"prefabs\":{\"prefabs\":[{\"children\":[");
    for (int i = 0; i < 16; i++) at += (size_t)snprintf(big + at, sizeof(big) - at, "%s{\"id\":\"r%02d\",\"kind\":\"relay\",\"links\":[]}", i ? "," : "", i);
    snprintf(big + at, sizeof(big) - at, "],\"id\":\"tf:prefab/door\"}],\"schema\":1},\"scripts\":[]}");
    CHECK(refused(instances_world(65, "tf:prefab/door"), big, "prefab instance i064 expands the world to 1040 entities, exceeding limit 1024"));
    CHECK(refused(instances_world(129, "tf:prefab/door"), NULL, "world_entities: more than 128 prefab instances"));
    /* 17 children: over 16. */
    at = (size_t)snprintf(big, sizeof(big), "{\"kind\":\"library\",\"package\":{\"id\":\"t.fac\",\"provides\":[\"tf:prefab/door\"],"
                          "\"requires\":[],\"schema\":1},\"prefabs\":{\"prefabs\":[{\"children\":[");
    for (int i = 0; i < 17; i++) at += (size_t)snprintf(big + at, sizeof(big) - at, "%s{\"id\":\"r%02d\",\"kind\":\"relay\",\"links\":[]}", i ? "," : "", i);
    snprintf(big + at, sizeof(big) - at, "],\"id\":\"tf:prefab/door\"}],\"schema\":1},\"scripts\":[]}");
    CHECK(refused(NULL, big, "prefab tf:prefab/door has more than 16 children"));
    /* A chain of 17 relays in one prefab: too long. */
    at = (size_t)snprintf(big, sizeof(big), "{\"kind\":\"library\",\"package\":{\"id\":\"t.fac\",\"provides\":[\"tf:prefab/door\"],"
                          "\"requires\":[],\"schema\":1},\"prefabs\":{\"prefabs\":[{\"children\":[");
    for (int i = 0; i < 16; i++)
        at += (size_t)snprintf(big + at, sizeof(big) - at, "%s{\"id\":\"r%02d\",\"kind\":\"relay\",\"links\":[%s%s%s]}", i ? "," : "", i,
                               i < 15 ? "{\"event\":\"fired\",\"input\":\"activate\",\"target\":\"r" : "", i < 15 ? (char[4]){ (char)('0' + (i + 1) / 10), (char)('0' + (i + 1) % 10), 0 } : "",
                               i < 15 ? "\"}" : "");
    snprintf(big + at, sizeof(big) - at, "],\"id\":\"tf:prefab/door\"}],\"schema\":1},\"scripts\":[]}");
    {
        static hta_external_map m;
        char err[800];
        reset_libs();
        put_lib(&DIR_, "t.fac", big, NULL, 0);
        bool ok = world_loads(instances_world(1, "tf:prefab/door"), &m, err, sizeof(err));
        CHECK(ok);    /* 15 links: within 16 */
        if (ok) hta_external_map_free(&m);
        reset_libs();
    }
    /* Stress: 128 instances of a 2-child prefab (256 entities: the most
     * instances a world holds; until X8, 32 -> 64 was the whole world),
     * expanded and loaded; time it. */
    const char *two = "{\"kind\":\"library\",\"package\":{\"id\":\"t.fac\",\"provides\":[\"tf:prefab/pair\"],"
        "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\"]}],\"schema\":1},"
        "\"prefabs\":{\"prefabs\":[{\"children\":["
        "{\"id\":\"a\",\"kind\":\"prop\",\"links\":[],\"model\":\"xs:model/crate\",\"position\":[0,0,0.25]},"
        "{\"id\":\"b\",\"kind\":\"prop\",\"links\":[],\"model\":\"xs:model/crate\",\"position\":[0,1,0.25],\"yaw_degrees\":15}],"
        "\"id\":\"tf:prefab/pair\"}],\"schema\":1},\"scripts\":[]}";
    reset_libs();
    put_lib(&DIR_, "t.fac", two, NULL, 0);
    static hta_external_map m;
    static hta_world_entities w;
    char err[800];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    bool ok = true;
    for (int rep = 0; rep < 20 && ok; rep++) {
        ok = world_loads(instances_world(128, "tf:prefab/pair"), &m, err, sizeof(err)) && hta_went_load(&w, &m.world_defs, err, sizeof(err));
        if (ok) { CHECK(m.world_defs.count == 256 && !strcmp(m.world_defs.entity[255].id, "t:entity/i127__b")); hta_went_free(&w); hta_external_map_free(&m); }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (!ok) fprintf(stderr, "stress: %s\n", err);
    CHECK(ok);
    double ms = ((double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6) / 20.0;
    reset_libs();
    printf("  limits: 1040 entities, 129 instances, 17 children refused; a 15-link chain loads; stress: 128 instances -> 256 entities "
           "loaded, expanded and collision built in %.2f ms each (20 runs): ok\n", ms);
}

/* ---- the world key -------------------------------------------------------------- */

static uint64_t key_with(const char *fac, const char *world)
{
    static hta_external_map m;
    char err[800];
    reset_libs();
    if (fac) put_lib(&DIR_, "t.fac", fac, NULL, 0);
    bool ok = world_loads(world ? world : WORLD, &m, err, sizeof(err));
    if (!ok) { fprintf(stderr, "key: %s\n", err); reset_libs(); return 0; }
    uint64_t d = m.digest;
    hta_external_map_free(&m);
    reset_libs();
    return d;
}

static void world_key(void)
{
    uint64_t base = key_with(NULL, NULL);
    CHECK(base && base == key_with(NULL, NULL));
    /* Prefab content changes: a child's place, a reference, a link, a number. */
    CHECK(key_with(edit(FAC, "[0,-0.9,0.25]", "[0,-0.8,0.25]"), NULL) != base);
    CHECK(key_with(edit(FAC, "\"input\":\"toggle\"", "\"input\":\"open\""), NULL) != base);
    CHECK(key_with(edit(FAC, "\"speed\":1.3", "\"speed\":1.4"), NULL) != base);
    CHECK(key_with(edit(FAC, ",\"sound\":\"xs:sound/impact\"", ""), NULL) != base);
    CHECK(key_with(edit(FAC, "log('used')", "log('pressed')"), NULL) != base);
    /* The instance's identity and transform are the world's bytes. */
    CHECK(key_with(NULL, edit(WORLD, "\"id\":\"north\"", "\"id\":\"nort\"")) != base);
    CHECK(key_with(NULL, edit(WORLD, "\"yaw_degrees\":-90", "\"yaw_degrees\":90")) != base);
    /* A changed texel of the art library, which the world never imported. */
    PAYLOAD[MESH_BYTES + 200] ^= 1;
    CHECK(key_with(NULL, NULL) != base);
    PAYLOAD[MESH_BYTES + 200] ^= 1;
    CHECK(key_with(NULL, NULL) == base);
    /* Provenance only: admitted (the same key). */
    CHECK(key_with(edit(FAC, "\"creator\":\"test\"", "\"creator\":\"someone else, elsewhere\""), NULL) == base);
    printf("  world key: prefab child place, link, number, sound, its script, an instance's ID and yaw, the art library's "
           "texel change it; provenance does not (base %016llx): ok\n", (unsigned long long)base);
}

/* ---- an older library's digest, and an X5 engine's view -------------------------- */

static void compatibility(void)
{
    /* A library without "prefabs" digests exactly as before X6: the
     * digest's member list only gained a name. */
    size_t n;
    uint8_t *b = oala(ART, PAYLOAD, sizeof(PAYLOAD), &n);
    hta_pkg_dep d;
    char err[400];
    CHECK(hta_library_load(b, n, &d, err, sizeof(err)));
    CHECK(d.prefabs && d.prefabs->count == 0);
    hta_pkg_dep_free(&d);
    free(b);
    /* What an X5 engine sees: 'prefab' was reserved there, so the provides
     * of any X6 prefab library holds a type it refuses on sight. */
    CHECK(strstr(FAC, "\"provides\":[\"tf:prefab/door\""));
    puts("  compatibility: a library without prefabs loads and digests as before; an X6 library's provides names the type "
         "an X5 engine reserves: ok");
}

/* ---- hostile input and lifetimes -------------------------------------------------- */

static uint32_t rng = 0x6f1d2a1u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static void hostile(void)
{
    unsigned ll = 0, lr = 0, wl = 0, wr = 0, pl = 0, pr = 0;
    size_t flen = strlen(FAC), wlen = strlen(WORLD);
    static char mf[sizeof(FAC)], mw[sizeof(WORLD)];
    static const char J[] = "{}[]\",:0123456789.-/_ aeprodfbutnkisyxlm";
    for (int i = 0; i < 4000; i++) {
        memcpy(mf, FAC, flen + 1);
        int flips = 1 + (int)(rnd() % 4);
        for (int k = 0; k < flips; k++) {
            size_t at = rnd() % flen;
            mf[at] = rnd() % 3 ? J[rnd() % (sizeof(J) - 1)] : (char)(1 + rnd() % 254);
        }
        size_t n;
        uint8_t *b = oala(mf, NULL, 0, &n);
        hta_pkg_dep d1, d2;
        char e1[480], e2[480];
        bool a = hta_library_load(b, n, &d1, e1, sizeof(e1)), c = hta_library_load(b, n, &d2, e2, sizeof(e2));
        CHECK(a == c);
        if (a) { CHECK(d1.digest == d2.digest); ll++; hta_pkg_dep_free(&d1); hta_pkg_dep_free(&d2); }
        else { CHECK(!strcmp(e1, e2) && printable(e1) && e1[0]); lr++; }
        free(b);
        /* The same library in a set, under the world. */
        static hta_external_map m1, m2;
        char w1[800], w2[800];
        reset_libs();
        put_lib(&DIR_, "t.fac", mf, NULL, 0);
        a = world_loads(WORLD, &m1, w1, sizeof(w1));
        c = world_loads(WORLD, &m2, w2, sizeof(w2));
        CHECK(a == c);
        if (a && c) { CHECK(m1.digest == m2.digest && m1.world_defs.count == m2.world_defs.count); pl++; }
        if (!a) { CHECK(!strcmp(w1, w2) && printable(w1) && w1[0]); pr++; }
        if (a) hta_external_map_free(&m1);
        if (c) hta_external_map_free(&m2);
    }
    reset_libs();
    for (int i = 0; i < 3000; i++) {
        memcpy(mw, WORLD, wlen + 1);
        int flips = 1 + (int)(rnd() % 3);
        for (int k = 0; k < flips; k++) {
            size_t at = rnd() % wlen;
            mw[at] = rnd() % 3 ? J[rnd() % (sizeof(J) - 1)] : (char)(1 + rnd() % 254);
        }
        static hta_external_map m1, m2;
        char w1[800], w2[800];
        bool a = world_loads(mw, &m1, w1, sizeof(w1)), c = world_loads(mw, &m2, w2, sizeof(w2));
        CHECK(a == c);
        if (a && c) {
            CHECK(m1.digest == m2.digest);
            static hta_world_entities w;
            CHECK(hta_went_load(&w, &m1.world_defs, w1, sizeof(w1)));
            for (int s = 0; s < 10; s++) hta_went_step(&w, 1.0f / 30.0f);
            hta_went_free(&w);
            wl++;
        }
        if (!a) { CHECK(!strcmp(w1, w2) && printable(w1) && w1[0]); wr++; }
        if (a) hta_external_map_free(&m1);
        if (c) hta_external_map_free(&m2);
    }
    /* Random local IDs never crash, never pass with a forbidden byte. */
    unsigned valid = 0;
    for (int i = 0; i < 20000; i++) {
        char p[40], why[128];
        size_t len = rnd() % 30;
        static const char L[] = "ab_9A-.:/ \x01z";
        for (size_t k = 0; k < len; k++) p[k] = L[rnd() % (sizeof(L) - 1)];
        p[len] = 0;
        bool ok = hta_prefab_local_valid(p, "local id", why, sizeof(why));
        if (ok) {
            valid++;
            CHECK(len <= HTA_PREFAB_LOCAL_MAX && !strstr(p, "__") && p[len - 1] != '_' && p[0] >= 'a' && p[0] <= 'z');
            for (size_t k = 0; k < len; k++) CHECK((p[k] >= 'a' && p[k] <= 'z') || (p[k] >= '0' && p[k] <= '9') || p[k] == '_');
        } else CHECK(printable(why) && why[0]);
    }
    printf("  hostile input: 4000 mutated prefab libraries (%u loaded, %u refused; under a world %u loaded, %u refused), "
           "3000 mutated worlds (%u loaded and stepped, %u refused), each twice alike; 20000 local IDs (%u valid): ok\n",
           ll, lr, pl, pr, wl, wr, valid);
}

static void lifetimes(void)
{
    static hta_external_map m;
    static hta_world_entities w;
    char err[800];
    for (int i = 0; i < 200; i++) {
        reset_libs();
        const char *world = i % 4 == 0 ? WORLD : i % 4 == 1 ? edit(WORLD, "\"prefab\":\"tf:prefab/door\"}", "\"prefab\":\"tf:prefab/dor\"}") :
                            i % 4 == 2 ? edit(WORLD, "\"yaw_degrees\":-90", "\"scale\":4") : WORLD;
        if (i % 4 == 3) put_lib(&DIR_, "t.fac", edit(FAC, "{\"id\":\"post\"", "{\"id\":\"door\""), NULL, 0);
        bool ok = world_loads(world, &m, err, sizeof(err));
        CHECK(ok == (i % 4 == 0));
        if (ok) {
            CHECK(hta_went_load(&w, &m.world_defs, err, sizeof(err)));
            hta_went_step(&w, 0.1f);
            hta_went_free(&w);
            hta_external_map_free(&m);
        }
    }
    reset_libs();
    puts("  lifetimes: 200 loads and frees -- good, a missing prefab, a limit failing mid-expansion, a bad library (ASan/LSan in build-asan): ok");
}

int main(void)
{
    cube(PAYLOAD, 0.25f);
    for (int i = 0; i < 100; i++) { int16_t s = (int16_t)(8000.0 * sin(i * 0.3)); PAYLOAD[MESH_BYTES + i * 2] = (uint8_t)s; PAYLOAD[MESH_BYTES + i * 2 + 1] = (uint8_t)((uint16_t)s >> 8); }
    for (int i = 0; i < 16; i++) { uint8_t *p = PAYLOAD + MESH_BYTES + 200 + i * 4; p[0] = 200; p[1] = 30; p[2] = 30; p[3] = 255; }
    (void)FAC_MINIMAL_HEAD;
    good_world();
    turned_props();
    refusals();
    limits();
    world_key();
    compatibility();
    hostile();
    lifetimes();
    dir_free(&DIR_);
    if (failures) { fprintf(stderr, "prefab: %d failures\n", failures); return 1; }
    puts("prefab: all ok");
    return 0;
}
