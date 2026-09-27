/* Event bindings (X7, docs/EVENT_BINDINGS.md): events, conditions and
 * actions as data. Through the real package-set loader: a library's
 * prefab (schema 2) whose bindings name its own children and its art
 * library's sound, placed twice; a world (schema 6) with its own bindings,
 * one of them naming a prefab child. Then: compiled indices, every
 * refusal, the runtime (conditions, chains, instance isolation, damage,
 * teleport, sound, relay state, round reset, a joiner that never runs
 * one), loops and storms bounded by the chain and cascade budgets, links
 * and bindings on one event in a fixed order, the world key over binding
 * bytes, hostile input, lifetimes (ASan/UBSan in build-asan) and the cost
 * of dispatch. No game data. */
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

/* ---- the art library: a cube model, its texture, a sound (test_prefab.c's) ---- */

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

/* The facility: a powered security door. Children button, door, power (a
 * relay), power_button, post; bindings, by local ID:
 *   chime        door opened                         -> play_sound at door
 *   locked       button used,  power inactive        -> play_sound (at the button)
 *   power_off    power_button used, power active     -> deactivate power
 *   power_on     power_button used, power inactive   -> activate power
 *   powered      power activated                     -> open door
 *   toggle_door  button used,  power active          -> toggle door
 *   unpowered    power deactivated                   -> close door */
#define B_(id, ev, src, conds, acts) \
    "{\"actions\":[" acts "],\"conditions\":[" conds "],\"event\":\"" ev "\",\"id\":\"" id "\",\"source\":\"" src "\"}"
#define SOUND_AT(at) "{\"action\":\"play_sound\",\"at\":\"" at "\",\"sound\":\"xs:sound/impact\"}"
#define SOUND "{\"action\":\"play_sound\",\"sound\":\"xs:sound/impact\"}"
#define ACT(a, t) "{\"action\":\"" a "\",\"target\":\"" t "\"}"
#define RELAY_IS(e, v) "{\"condition\":\"relay_state\",\"entity\":\"" e "\",\"is\":\"" v "\"}"
static const char FAC[] =
    "{\"kind\":\"library\",\"package\":{\"id\":\"t.fac\",\"provides\":[\"tf:prefab/door\"],"
    "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:model/crate\",\"xs:sound/impact\"]}],\"schema\":1},"
    "\"prefabs\":{\"prefabs\":[{\"bindings\":["
    B_("chime", "opened", "door", "", SOUND_AT("door")) ","
    B_("locked", "used", "button", RELAY_IS("power", "inactive"), SOUND) ","
    B_("power_off", "used", "power_button", RELAY_IS("power", "active"), ACT("deactivate", "power")) ","
    B_("power_on", "used", "power_button", RELAY_IS("power", "inactive"), ACT("activate", "power")) ","
    B_("powered", "activated", "power", "", ACT("open", "door")) ","
    B_("toggle_door", "used", "button", RELAY_IS("power", "active"), ACT("toggle", "door")) ","
    B_("unpowered", "deactivated", "power", "", ACT("close", "door"))
    "],\"children\":["
    "{\"id\":\"button\",\"kind\":\"interactable\",\"links\":[],\"position\":[-0.2,-0.7,0.9],\"reach\":1.2},"
    "{\"id\":\"door\",\"kind\":\"mover\",\"links\":[],\"model\":\"xs:model/crate\",\"move\":[0,1.3,0],\"position\":[0,0,0.6],"
    "\"size\":[0.1,1.2,1.2],\"speed\":2.6},"
    "{\"id\":\"post\",\"kind\":\"prop\",\"links\":[],\"model\":\"xs:model/crate\",\"position\":[0,-0.9,0.25]},"
    "{\"id\":\"power\",\"kind\":\"relay\",\"links\":[]},"
    "{\"id\":\"power_button\",\"kind\":\"interactable\",\"links\":[],\"position\":[-0.2,0.7,0.9],\"reach\":1.2}],"
    "\"id\":\"tf:prefab/door\"}],\"schema\":2},"
    "\"provenance\":{\"tf:prefab/door\":{\"creator\":\"test\"}},\"scripts\":[]}";

/* The world: two instances, a loop of relays, a shock pad, a link and a
 * binding on one button, and a binding naming a prefab child. */
static const char WORLD[] =
    "{\"id\":\"t:world/bind\",\"package\":{\"id\":\"t.world\",\"provides\":[\"t:world/bind\"],"
    "\"requires\":[{\"package\":\"t.art\",\"resources\":[\"xs:sound/impact\"]},"
    "{\"package\":\"t.fac\",\"resources\":[\"tf:prefab/door\"]}],\"schema\":1},"
    "\"world_entities\":{\"bindings\":["
    B_("both", "used", "t:entity/mixed", "", SOUND) ","
    B_("loop_a", "activated", "t:entity/relay_a", "", ACT("activate", "t:entity/relay_b")) ","
    B_("loop_b", "activated", "t:entity/relay_b", "", ACT("activate", "t:entity/relay_a")) ","
    B_("loop_start", "used", "t:entity/loop_button", "", ACT("activate", "t:entity/relay_a")) ","
    B_("north_watch", "closed", "t:entity/north__door", "", SOUND_AT("t:entity/pad")) ","
    B_("press_north", "activated", "t:entity/relay_c", "", ACT("use", "t:entity/north__power_button")) ","
    B_("shock", "entered", "t:entity/pad", "",
       "{\"action\":\"damage\",\"amount\":25.0}," ACT("teleport", "t:entity/dest") "," SOUND)
    "],\"entities\":["
    "{\"id\":\"t:entity/dest\",\"kind\":\"teleport\",\"links\":[],\"position\":[6,6,0.1],\"yaw_degrees\":0.0},"
    "{\"id\":\"t:entity/loop_button\",\"kind\":\"interactable\",\"links\":[],\"position\":[6,-6,0.9],\"reach\":1.0},"
    "{\"id\":\"t:entity/mixed\",\"kind\":\"interactable\",\"links\":[{\"event\":\"used\",\"input\":\"activate\",\"target\":\"t:entity/relay_c\"}],"
    "\"position\":[-6,-6,0.9],\"reach\":1.0},"
    "{\"bounds\":{\"max\":[4,1,1],\"min\":[3,0,-0.1]},\"id\":\"t:entity/pad\",\"kind\":\"trigger\",\"links\":[]},"
    "{\"id\":\"t:entity/relay_a\",\"kind\":\"relay\",\"links\":[]},"
    "{\"id\":\"t:entity/relay_b\",\"kind\":\"relay\",\"links\":[]},"
    "{\"id\":\"t:entity/relay_c\",\"kind\":\"relay\",\"links\":[]}],"
    "\"prefab_instances\":["
    "{\"id\":\"north\",\"position\":[0,3,0],\"prefab\":\"tf:prefab/door\"},"
    "{\"id\":\"south\",\"position\":[-3,-2,0],\"prefab\":\"tf:prefab/door\",\"yaw_degrees\":-90.0}],\"schema\":6}}";

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

static void dir_free(lib_dir *d)
{
    for (unsigned i = 0; i < d->count; i++) free(d->f[i].data);
    memset(d, 0, sizeof(*d));
}

static char *edit(const char *base, const char *from, const char *to)
{
    static char out[4][32768];
    static int k;
    char *o = out[k++ & 3];
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

/* A press at interactable `i`: the eye half a wu in front of it. */
static int32_t press(hta_world_entities *w, uint32_t i, uint8_t actor)
{
    const float *p = w->defs->entity[i].pos;
    float eye[3] = { p[0] - 0.5f, p[1], p[2] }, fwd[3] = { 1, 0, 0 };
    for (int k = 0; k < 20; k++) hta_went_step(w, 0.05f);       /* past any cooldown */
    return hta_went_interact(w, actor, eye, fwd);
}

static void steps(hta_world_entities *w, int n) { for (int k = 0; k < n; k++) hta_went_step(w, 0.05f); }

static uint32_t binding_cues(const hta_world_entities *w, uint32_t entity)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < w->cue_count; k++) n += w->cues[k].binding && w->cues[k].entity == entity;
    return n;
}

/* Steps until something sounds at `entity` (a binding's cue), at most n. */
static bool sounds_within(hta_world_entities *w, uint32_t entity, int n)
{
    for (int k = 0; k < n; k++) { hta_went_step(w, 0.05f); if (binding_cues(w, entity)) return true; }
    return false;
}

/* ---- the good world ------------------------------------------------------------ */

static hta_external_map GOOD;

static void load_good(void)
{
    char err[800];
    reset_libs();
    bool ok = world_loads(WORLD, &GOOD, err, sizeof(err));
    if (!ok) fprintf(stderr, "good world: %s\n", err);
    assert(ok);
}

static void compiled(void)
{
    const hta_world_defs *d = &GOOD.world_defs;
    CHECK(d->schema == 6 && d->count == 7 + 10 && d->prefab_instance_count == 2);
    /* the world's six (canonical order), then north's seven, then south's */
    CHECK(d->binding_count == 7 + 7 + 7);
    static const char *const OWN[7] = { "both", "loop_a", "loop_b", "loop_start", "north_watch", "press_north", "shock" };
    for (int i = 0; i < 7; i++) CHECK(!strcmp(d->binding[i].id, OWN[i]) && !d->binding[i].instance);
    static const char *const PB[7] = { "chime", "locked", "power_off", "power_on", "powered", "toggle_door", "unpowered" };
    for (int i = 0; i < 7; i++) {
        CHECK(!strcmp(d->binding[7 + i].id, PB[i]) && d->binding[7 + i].instance == 1);
        CHECK(!strcmp(d->binding[14 + i].id, PB[i]) && d->binding[14 + i].instance == 2);
    }
    /* Local references became each instance's own entities. */
    int32_t nb = find(d, "t:entity/north__button"), nd = find(d, "t:entity/north__door"), np = find(d, "t:entity/north__power");
    int32_t sb = find(d, "t:entity/south__button"), sd = find(d, "t:entity/south__door"), sp = find(d, "t:entity/south__power");
    CHECK(nb >= 0 && nd >= 0 && np >= 0 && sb >= 0 && sd >= 0 && sp >= 0);
    const hta_wbinding *nt = &d->binding[7 + 5], *st = &d->binding[14 + 5];
    CHECK(nt->source == nb && d->action[nt->first_action].target == nd && d->cond[nt->first_cond].entity == np);
    CHECK(st->source == sb && d->action[st->first_action].target == sd && d->cond[st->first_cond].entity == sp);
    CHECK(d->action[nt->first_action].op == HTA_WACT_TOGGLE && d->action[nt->first_action].input == HTA_WIN_TOGGLE);
    CHECK(d->cond[nt->first_cond].kind == HTA_WCOND_RELAY_STATE && d->cond[nt->first_cond].value == HTA_WRELAY_ACTIVE);
    /* The sound: the art library's, resolved from the prefab's own view,
     * and the world's own import of the same resource: one asset. */
    const hta_wbinding *chime = &d->binding[7];
    CHECK(d->action[chime->first_action].sound == 1 && d->action[chime->first_action].at == nd);
    CHECK(d->action[d->binding[0].first_action].sound == 1 && d->action[d->binding[0].first_action].at == HTA_WDEF_NO_DEF);
    CHECK(GOOD.assets.sound_count == 1);
    /* A world binding naming a prefab child by its placed ID. */
    CHECK(d->binding[4].source == nd && d->binding[4].event == HTA_WEV_CLOSED);
    /* The shock: three actions, in authored order. */
    const hta_wbinding *sh = &d->binding[6];
    CHECK(sh->action_count == 3 && d->action[sh->first_action].op == HTA_WACT_DAMAGE && d->action[sh->first_action].amount == 25.0f &&
          d->action[sh->first_action + 1].op == HTA_WACT_TELEPORT && d->action[sh->first_action + 2].op == HTA_WACT_PLAY_SOUND);
    /* The dispatch index: north's button's `used` has exactly its two. */
    static hta_world_entities w;
    char err[256];
    CHECK(hta_went_load(&w, d, err, sizeof(err)));
    CHECK(w.bind_n[nb][HTA_WEV_USED] == 2 && w.bind_list[w.bind_first[nb][HTA_WEV_USED]] == 7 + 1 &&
          w.bind_list[w.bind_first[nb][HTA_WEV_USED] + 1] == 7 + 5);
    CHECK(w.bind_n[sb][HTA_WEV_USED] == 2 && w.bind_n[nd][HTA_WEV_OPENED] == 1 && w.bind_n[nd][HTA_WEV_CLOSED] == 1);
    hta_went_free(&w);
    puts("  compiled: 21 bindings, local references per instance, one sound asset, indexed by (source, event): ok");
}

static void runtime(void)
{
    const hta_world_defs *d = &GOOD.world_defs;
    static hta_world_entities w;
    char err[256];
    CHECK(hta_went_load(&w, d, err, sizeof(err)));
    uint32_t nb = (uint32_t)find(d, "t:entity/north__button"), nd = (uint32_t)find(d, "t:entity/north__door");
    uint32_t npb = (uint32_t)find(d, "t:entity/north__power_button"), np = (uint32_t)find(d, "t:entity/north__power");
    uint32_t sb = (uint32_t)find(d, "t:entity/south__button"), sd = (uint32_t)find(d, "t:entity/south__door");
    uint32_t sp = (uint32_t)find(d, "t:entity/south__power");
    uint32_t pad = (uint32_t)find(d, "t:entity/pad");
    /* 1. Condition: unpowered, the button only sounds locked. */
    CHECK(press(&w, nb, 1) == (int32_t)nb);
    hta_went_step(&w, 0.05f);
    CHECK(binding_cues(&w, nb) == 1 && w.st[nd].phase == HTA_MOVER_CLOSED);
    CHECK(w.stats.bindings_skipped == 1 && w.stats.bindings_matched == 1);
    /* 2. The chain: power_button used -> power.activate -> activated ->
     *    door.open (the same step) -> ... opened -> the chime (a later step). */
    CHECK(press(&w, npb, 1) == (int32_t)npb);
    hta_went_step(&w, 0.05f);
    CHECK(hta_went_relay_active(&w, np) && w.st[nd].phase == HTA_MOVER_OPENING);
    CHECK(sounds_within(&w, nd, 40) && w.st[nd].phase == HTA_MOVER_OPEN);
    /* north only */
    CHECK(!hta_went_relay_active(&w, sp) && w.st[sd].phase == HTA_MOVER_CLOSED);
    /* 3. The same event, a different result: powered, the button toggles. */
    CHECK(press(&w, nb, 2) == (int32_t)nb);
    hta_went_step(&w, 0.05f);
    CHECK(w.st[nd].phase == HTA_MOVER_CLOSING && !binding_cues(&w, nb));
    /* ... and the world's binding on north's door closing sounds at the pad */
    CHECK(sounds_within(&w, pad, 40) && w.st[nd].phase == HTA_MOVER_CLOSED);
    /* 4. Power off: deactivate -> deactivated -> close (already closed: no event). */
    CHECK(press(&w, nb, 2) == (int32_t)nb);
    steps(&w, 1);
    CHECK(w.st[nd].phase == HTA_MOVER_OPENING);
    CHECK(press(&w, npb, 2) == (int32_t)npb);
    steps(&w, 1);
    CHECK(!hta_went_relay_active(&w, np) && w.st[nd].phase == HTA_MOVER_CLOSING);
    steps(&w, 40);
    CHECK(w.st[nd].phase == HTA_MOVER_CLOSED);
    /* 5. South is its own: its power, its door, its chime. */
    CHECK(press(&w, (uint32_t)find(d, "t:entity/south__power_button"), 3) >= 0);
    CHECK(sounds_within(&w, sd, 40) && w.st[sd].phase == HTA_MOVER_OPEN && hta_went_relay_active(&w, sp));
    CHECK(w.st[nd].phase == HTA_MOVER_CLOSED && !hta_went_relay_active(&w, np));
    CHECK(press(&w, sb, 3) == (int32_t)sb);
    steps(&w, 1);
    CHECK(w.st[sd].phase == HTA_MOVER_CLOSING && w.st[nd].phase == HTA_MOVER_CLOSED);
    /* 6. The pad: damage, teleport and sound, once per entry, the actor's. */
    const hta_wdef *pd = &d->entity[pad];
    float in[3] = { (pd->min[0] + pd->max[0]) * 0.5f, (pd->min[1] + pd->max[1]) * 0.5f, 0.0f }, out[3] = { -7, -7, 0 };
    hta_went_sense(&w, 5, out, true);
    hta_went_sense(&w, 5, in, true);
    hta_went_step(&w, 0.05f);
    CHECK(w.hurt_count == 1 && w.hurts[0].actor == 5 && w.hurts[0].amount == 25.0f && w.hurts[0].source == pad);
    CHECK(w.teleport_count == 1 && w.teleports[0].actor == 5 && w.teleports[0].pos[0] == 6.0f);
    CHECK(binding_cues(&w, pad) == 1);
    hta_went_sense(&w, 5, in, true);                 /* staying: nothing */
    hta_went_step(&w, 0.05f);
    CHECK(w.hurt_count == 0 && w.teleport_count == 0);
    /* 7. A link and a binding on one event: the link's input queues first. */
    uint32_t mixed = (uint32_t)find(d, "t:entity/mixed"), rc = (uint32_t)find(d, "t:entity/relay_c");
    steps(&w, 20);
    CHECK(hta_went_interact(&w, 1, (float[3]){ d->entity[mixed].pos[0] - 0.5f, d->entity[mixed].pos[1], d->entity[mixed].pos[2] },
                            (float[3]){ 1, 0, 0 }) == (int32_t)mixed);
    CHECK(w.count == 2 && w.queue[w.head].binding == 0 && w.queue[w.head].input == HTA_WIN_ACTIVATE &&
          w.queue[(w.head + 1) % HTA_WENT_QUEUE].binding == 1 && w.queue[(w.head + 1) % HTA_WENT_QUEUE].input == HTA_WIN_SOUND);
    hta_went_step(&w, 0.05f);
    CHECK(hta_went_relay_active(&w, rc) && binding_cues(&w, mixed) == 1);
    /* ... and relay_c's binding USES north's power button: the same path a
     * press takes (cooldown, used, its bindings) -- so the power chain runs. */
    CHECK(hta_went_relay_active(&w, np) && w.st[nd].phase == HTA_MOVER_OPENING);
    CHECK(w.st[npb].cooldown > 0.0f);
    /* The action seam: validated against the target's capabilities. */
    CHECK(hta_went_request(&w, HTA_WACT_OPEN, np, 1) == HTA_WENT_REJECTED);          /* a relay affords no open */
    CHECK(hta_went_request(&w, HTA_WACT_USE, nd, 1) == HTA_WENT_REJECTED);           /* a door affords no use */
    CHECK(hta_went_request(&w, HTA_WACT_DAMAGE, nb, 1) == HTA_WENT_REJECTED);        /* damage has no target */
    CHECK(hta_went_request(&w, HTA_WACT_CLOSE, nd, 1) == HTA_WENT_QUEUED);
    CHECK(hta_wdef_affords(HTA_WDEF_INTERACTABLE, HTA_WACT_USE) && hta_wdef_affords(HTA_WDEF_MOVER, HTA_WACT_TOGGLE) &&
          !hta_wdef_affords(HTA_WDEF_PROP, HTA_WACT_USE));
    hta_went_step(&w, 0.05f);
    CHECK(w.st[nd].phase == HTA_MOVER_CLOSED);
    /* 8. A round reset: relays inactive, doors closed, nothing queued. */
    CHECK(press(&w, npb, 1) >= 0);
    steps(&w, 1);
    hta_went_reset(&w);
    CHECK(!hta_went_relay_active(&w, np) && !hta_went_relay_active(&w, rc) && w.st[nd].phase == HTA_MOVER_CLOSED && !w.count);
    steps(&w, 5);
    CHECK(w.st[nd].phase == HTA_MOVER_CLOSED && !w.cue_count);
    /* 9. A joiner never evaluates a binding: no use, no arrival event. */
    static hta_world_entities j;
    CHECK(hta_went_load(&j, d, err, sizeof(err)));
    j.remote = true;
    CHECK(press(&j, npb, 1) == -1);
    CHECK(hta_went_request(&j, HTA_WACT_OPEN, nd, 1) == HTA_WENT_REFUSED_HERE);
    hta_went_mover_state ms = { (uint8_t)nd, HTA_MOVER_OPEN, 65535 };
    CHECK(hta_went_apply(&j, &ms, false));
    steps(&j, 3);
    CHECK(!j.count && !j.stats.bindings_matched && !binding_cues(&j, nd) && !hta_went_relay_active(&j, np));
    hta_went_free(&j);
    hta_went_free(&w);
    puts("  runtime: conditions, the power chain (used -> activate -> activated -> open -> opened -> sound), per-instance state, "
         "damage + teleport + sound once per entry, link before binding, reset, no joiner evaluation: ok");
}

/* ---- loops and storms ------------------------------------------------------------ */

static void loops(void)
{
    const hta_world_defs *d = &GOOD.world_defs;
    static hta_world_entities w;
    char err[256];
    CHECK(hta_went_load(&w, d, err, sizeof(err)));
    uint32_t lb = (uint32_t)find(d, "t:entity/loop_button"), ra = (uint32_t)find(d, "t:entity/relay_a");
    uint32_t nb = (uint32_t)find(d, "t:entity/north__button");
    CHECK(press(&w, lb, 1) == (int32_t)lb);
    uint64_t before = w.stats.dispatched;
    for (int k = 0; k < 20 && w.count; k++) hta_went_step(&w, 0.05f);
    CHECK(!w.count);
    /* A activates B activates A...: the chain limit ends it (17 hops),
     * named: the binding it stopped at. */
    CHECK(w.stats.dropped_depth == 1 && w.stats.dispatched - before == HTA_WDEF_MAX_CHAIN);
    CHECK(strstr(w.diag, "chain too long (a cycle?) at binding loop_") && printable(w.diag));
    CHECK(hta_went_relay_active(&w, ra));
    /* The world goes on: the next press works. */
    CHECK(press(&w, nb, 1) == (int32_t)nb);
    hta_went_step(&w, 0.05f);
    CHECK(binding_cues(&w, nb) == 1);
    hta_went_free(&w);

    /* A storm: 16 relays, each activating 8 others on activated (a cycle
     * everywhere, fan-out 8). One use starts it; the cascade budget ends
     * it, naming the binding; the queue never overflows past its bound and
     * the work is bounded. */
    static hta_world_defs s;
    memset(&s, 0, sizeof(s));
    s.schema = 6;
    for (uint32_t i = 0; i < 17; i++) {
        hta_wdef *e = &s.entity[s.count++];
        snprintf(e->id, sizeof(e->id), "st:entity/%s%u", i ? "r" : "button", i);
        e->kind = i ? HTA_WDEF_RELAY : HTA_WDEF_INTERACTABLE;
        e->def = HTA_WDEF_NO_DEF;
        if (!i) { e->reach = 1.0f; e->pos[2] = 1.0f; }
    }
    for (uint32_t i = 0; i < 17; i++) {
        hta_wbinding *b = &s.binding[s.binding_count++];
        snprintf(b->id, sizeof(b->id), "b%02u", i);
        b->source = (uint16_t)i;
        b->event = i ? HTA_WEV_ACTIVATED : HTA_WEV_USED;
        b->first_action = (uint16_t)s.action_count;
        b->action_count = 8;
        for (uint32_t k = 0; k < 8; k++)
            s.action[s.action_count++] = (hta_waction){ HTA_WACT_ACTIVATE, HTA_WIN_ACTIVATE, (uint16_t)(1 + (i + k) % 16), 0, HTA_WDEF_NO_DEF, 0 };
    }
    CHECK(hta_went_load(&w, &s, err, sizeof(err)) || (fprintf(stderr, "storm: %s\n", err), 0));
    float eye[3] = { -0.5f, 0, 1 }, fwd[3] = { 1, 0, 0 };
    CHECK(hta_went_interact(&w, 0, eye, fwd) == 0);
    clock_t t0 = clock();
    int n = 0;
    while (w.count && n < 100) { hta_went_step(&w, 0.05f); n++; }
    double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    CHECK(!w.count && n < 10);
    CHECK(w.stats.actions_queued == HTA_WENT_CASCADE_BUDGET && w.stats.dropped_budget > 0 && !w.stats.dropped_full);
    CHECK(w.stats.dispatched <= HTA_WENT_CASCADE_BUDGET);
    CHECK(strstr(w.diag, "the cascade from st:entity/button0 used exceeded 128 binding actions (a loop?); binding b") && printable(w.diag));
    /* the next use gets a fresh budget */
    uint64_t q = w.stats.actions_queued;
    steps(&w, 20);
    CHECK(hta_went_interact(&w, 0, eye, fwd) == 0);
    while (w.count) hta_went_step(&w, 0.05f);
    CHECK(w.stats.actions_queued - q == HTA_WENT_CASCADE_BUDGET);
    printf("  loops: A<->B stopped after %u hops at the chain limit; a fan-out-8 storm stopped at %u binding actions per root "
           "(%llu dropped) in %d steps, %.2f ms; the next use runs: ok\n", HTA_WDEF_MAX_CHAIN, HTA_WENT_CASCADE_BUDGET,
           (unsigned long long)w.stats.dropped_budget, n, ms);
    hta_went_free(&w);
}

/* ---- refusals ------------------------------------------------------------------- */

static void refusals(void)
{
    const char *W = WORLD, *F = FAC;
    /* the world's side */
    CHECK(refused(edit(W, "\"schema\":6}}", "\"schema\":5}}"), NULL, "world_entities: bindings need schema 6"));
    CHECK(refused(edit(W, "\"event\":\"entered\"", "\"event\":\"left\""), NULL,
                  "binding shock: unknown event 'left' (a binding listens to used, activated, entered, deactivated, opened, closed)"));
    CHECK(refused(edit(W, "\"event\":\"closed\",\"id\":\"north_watch\",\"source\":\"t:entity/north__door\"",
                       "\"event\":\"closed\",\"id\":\"north_watch\",\"source\":\"t:entity/pad\""), NULL,
                  "binding north_watch: event 'closed' is not supported by trigger entity t:entity/pad (mover emits it)"));
    CHECK(refused(edit(W, "\"source\":\"t:entity/loop_button\"", "\"source\":\"t:entity/loop_buton\""), NULL,
                  "binding loop_start references missing placed entity t:entity/loop_buton"));
    CHECK(refused(edit(W, "\"source\":\"t:entity/loop_button\"", "\"source\":\"xs:sound/impact\""), NULL,
                  "binding loop_start: binding source xs:sound/impact is a sound, expected a placed entity"));
    CHECK(refused(edit(W, ACT("activate", "t:entity/relay_b"), ACT("open", "t:entity/relay_b")), NULL,
                  "binding loop_a: action open targets t:entity/relay_b, a relay, which does not afford open (open needs a mover)"));
    CHECK(refused(edit(W, ACT("teleport", "t:entity/dest"), ACT("teleport", "t:entity/pad")), NULL,
                  "binding shock: action teleport targets t:entity/pad, a trigger, which does not afford teleport (teleport needs a teleport)"));
    CHECK(refused(edit(W, "{\"action\":\"damage\",\"amount\":25.0}", "{\"action\":\"damage\",\"amount\":0.0}"), NULL,
                  "binding shock: action damage: amount must be in (0, 500]"));
    CHECK(refused(edit(W, "{\"action\":\"damage\",\"amount\":25.0}", "{\"action\":\"damage\",\"amount\":501.0}"), NULL,
                  "binding shock: action damage: amount must be in (0, 500]"));
    CHECK(refused(edit(W, "{\"action\":\"damage\",\"amount\":25.0}", "{\"action\":\"damage\",\"amount\":1e999}"), NULL,
                  "binding shock: action 0: malformed 'amount'"));
    CHECK(refused(edit(W, "{\"action\":\"damage\",\"amount\":25.0}", "{\"action\":\"damage\"}"), NULL,
                  "binding shock: action damage needs 'amount'"));
    CHECK(refused(edit(W, "{\"action\":\"damage\",\"amount\":25.0}", "{\"action\":\"damage\",\"amount\":25.0,\"target\":\"t:entity/pad\"}"),
                  NULL, "binding shock: action damage does not take 'target'"));
    CHECK(refused(edit(W, "{\"action\":\"damage\",\"amount\":25.0}", "{\"action\":\"explode\",\"amount\":25.0}"), NULL,
                  "binding shock: unknown action 'explode' (open, close, toggle, activate, deactivate, teleport, damage, play_sound, use)"));
    CHECK(refused(edit(W, SOUND_AT("t:entity/pad"), SOUND_AT("t:entity/relay_a")), NULL,
                  "binding north_watch: action play_sound at t:entity/relay_a: a relay has no position (name one with 'at')"));
    CHECK(refused(edit(W, SOUND_AT("t:entity/pad"), "{\"action\":\"damage\",\"amount\":5.0}"), NULL,
                  "binding north_watch: action damage acts on the event's actor, and 'closed' never carries one"));
    CHECK(refused(edit(W, SOUND_AT("t:entity/pad"), "{\"action\":\"play_sound\",\"sound\":\"xs:sound/impakt\"}"), NULL,
                  "binding north_watch references missing sound xs:sound/impakt"));
    CHECK(refused(edit(W, SOUND_AT("t:entity/pad"), "{\"action\":\"play_sound\",\"sound\":\"xs:model/crate\"}"), NULL,
                  "binding north_watch: sound xs:model/crate is a model, expected a sound"));
    CHECK(refused(edit(W, "\"id\":\"loop_b\"", "\"id\":\"loop_a\""), NULL, "binding loop_a appears twice"));
    CHECK(refused(edit(W, "\"id\":\"loop_b\"", "\"id\":\"a_first\""), NULL,
                  "world_entities: bindings are not in canonical (byte) order of id at a_first"));
    CHECK(refused(edit(W, "\"id\":\"loop_b\"", "\"id\":\"Loop_b\""), NULL, "binding 'Loop_b': binding id has capital 'L'"));
    CHECK(refused(edit(W, "\"conditions\":[],\"event\":\"used\",\"id\":\"both\"",
                       "\"conditions\":[" RELAY_IS("t:entity/relay_a", "on") "],\"event\":\"used\",\"id\":\"both\""), NULL,
                  "binding both: condition relay_state: unknown value 'on' (inactive, active)"));
    CHECK(refused(edit(W, "\"conditions\":[],\"event\":\"used\",\"id\":\"both\"",
                       "\"conditions\":[{\"condition\":\"mover_state\",\"entity\":\"t:entity/relay_a\",\"is\":\"open\"}],\"event\":\"used\",\"id\":\"both\""),
                  NULL, "binding both: condition mover_state reads t:entity/relay_a, a relay (mover_state applies to a mover)"));
    CHECK(refused(edit(W, "\"conditions\":[],\"event\":\"used\",\"id\":\"both\"",
                       "\"conditions\":[{\"condition\":\"health\",\"entity\":\"t:entity/relay_a\",\"is\":\"open\"}],\"event\":\"used\",\"id\":\"both\""),
                  NULL, "binding both: unknown condition 'health' (mover_state, relay_state)"));
    CHECK(refused(edit(W, "\"conditions\":[],\"event\":\"used\",\"id\":\"both\"",
                       "\"conditions\":[" RELAY_IS("t:entity/relay_a", "active") "," RELAY_IS("t:entity/relay_a", "active") ","
                       RELAY_IS("t:entity/relay_a", "active") "," RELAY_IS("t:entity/relay_a", "active") ","
                       RELAY_IS("t:entity/relay_a", "active") "],\"event\":\"used\",\"id\":\"both\""), NULL,
                  "binding both: more than 4 conditions"));
    CHECK(refused(edit(W, "\"actions\":[" SOUND "],\"conditions\":[],\"event\":\"used\",\"id\":\"both\"",
                       "\"actions\":[" SOUND "," SOUND "," SOUND "," SOUND "," SOUND "," SOUND "," SOUND "," SOUND "," SOUND
                       "],\"conditions\":[],\"event\":\"used\",\"id\":\"both\""), NULL, "binding both: more than 8 actions"));
    CHECK(refused(edit(W, "\"actions\":[" SOUND "],\"conditions\":[],\"event\":\"used\",\"id\":\"both\"",
                       "\"actions\":[],\"conditions\":[],\"event\":\"used\",\"id\":\"both\""), NULL, "binding both: has no actions"));
    CHECK(refused(edit(W, "\"id\":\"both\",", "\"id\":\"both\",\"delay\":1.0,"), NULL,
                  "binding both: unknown field 'delay' (a binding has actions, conditions, event, id, source)"));
    CHECK(refused(edit(W, "\"id\":\"both\",", ""), NULL, "binding ?: a binding needs actions, conditions, event, id and source"));
    /* the prefab's side */
    CHECK(refused(NULL, edit(F, "\"schema\":2}", "\"schema\":1}"), "t.fac: prefab tf:prefab/door: bindings need prefab schema 2"));
    CHECK(refused(NULL, edit(F, "\"source\":\"door\"", "\"source\":\"dor\""), "prefab tf:prefab/door binding chime references missing child 'dor'"));
    CHECK(refused(NULL, edit(F, ACT("toggle", "door"), ACT("toggle", "power")),
                  "prefab tf:prefab/door binding toggle_door: action toggle targets 'power', a relay, which does not afford toggle"));
    CHECK(refused(NULL, edit(F, "\"event\":\"opened\",\"id\":\"chime\",\"source\":\"door\"", "\"event\":\"opened\",\"id\":\"chime\",\"source\":\"post\""),
                  "prefab tf:prefab/door binding chime: event 'opened' is not supported by prop entity 'post' (mover emits it)"));
    CHECK(refused(NULL, edit(F, "\"id\":\"locked\"", "\"id\":\"chime\""), "prefab tf:prefab/door contains duplicate binding id 'chime'"));
    CHECK(refused(NULL, edit(F, SOUND_AT("door"), "{\"action\":\"play_sound\",\"at\":\"door\",\"sound\":\"xs:sound/hiss\"}"),
                  "prefab tf:prefab/door binding chime references missing sound xs:sound/hiss"));
    CHECK(refused(NULL, edit(F, "\"xs:model/crate\",\"xs:sound/impact\"]", "\"xs:model/crate\"]"),
                  "prefab tf:prefab/door binding chime: sound xs:sound/impact is provided by package t.art, which package t.fac requires but does not import it from"));
    CHECK(refused(NULL, edit(F, "\"schema\":2}", "\"schema\":3}"), "t.fac: unsupported prefab schema 3 (this engine has 2)"));
    /* one instance too many: a limit on bindings (7 each) is reached before entities (5 each) */
    puts("  refusals: 37 cases, world and prefab, each with the engine's words: ok");
}

/* ---- the world key -------------------------------------------------------------- */

static uint32_t key_of(const char *world, const char *fac)
{
    static hta_external_map m;
    char err[800];
    reset_libs();
    if (fac) put_lib(&DIR_, "t.fac", fac, NULL, 0);
    bool ok = world_loads(world, &m, err, sizeof(err));
    reset_libs();
    if (!ok) { fprintf(stderr, "key_of: %s\n", err); return 0; }
    uint32_t k = m.key;
    hta_external_map_free(&m);
    return k;
}

static void world_key(void)
{
    uint32_t k = key_of(WORLD, NULL);
    CHECK(k && k == key_of(WORLD, NULL));
    /* gameplay: the event, an action, its target, an amount, a condition's value, the order of actions */
    CHECK(key_of(edit(WORLD, ACT("activate", "t:entity/relay_b"), ACT("deactivate", "t:entity/relay_b")), NULL) != k);
    CHECK(key_of(edit(WORLD, "\"amount\":25.0", "\"amount\":26.0"), NULL) != k);
    CHECK(key_of(edit(WORLD, "\"target\":\"t:entity/relay_a\"}],\"conditions\":[],\"event\":\"used\",\"id\":\"loop_start\"",
                      "\"target\":\"t:entity/relay_c\"}],\"conditions\":[],\"event\":\"used\",\"id\":\"loop_start\""), NULL) != k);
    CHECK(key_of(NULL ? NULL : WORLD, edit(FAC, ACT("toggle", "door"), ACT("open", "door"))) != k);
    CHECK(key_of(WORLD, edit(FAC, RELAY_IS("power", "active") "],\"event\":\"used\",\"id\":\"toggle_door\"",
                             RELAY_IS("power", "inactive") "],\"event\":\"used\",\"id\":\"toggle_door\"")) != k);
    CHECK(key_of(edit(WORLD, "{\"action\":\"damage\",\"amount\":25.0}," ACT("teleport", "t:entity/dest"),
                      ACT("teleport", "t:entity/dest") ",{\"action\":\"damage\",\"amount\":25.0}"), NULL) != k);
    /* provenance: nothing */
    CHECK(key_of(WORLD, edit(FAC, "\"creator\":\"test\"", "\"creator\":\"someone else\"")) == k);
    puts("  world key: event, action, target, amount, condition value, action order change it; provenance does not: ok");
}

/* ---- hostile input, lifetimes, cost ---------------------------------------------- */

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void hostile(void)
{
    static char buf[2][32768];
    static hta_external_map m;
    char e1[800], e2[800];
    int loaded = 0, refused_n = 0;
    const char *bases[2] = { WORLD, FAC };
    for (int iter = 0; iter < 3000; iter++) {
        int which = iter & 1;
        const char *base = bases[which];
        size_t n = strlen(base);
        memcpy(buf[0], base, n + 1);
        int muts = 1 + (int)(rnd() % 3);
        static const char *const TOK[] = { "\"opened\"", "\"closed\"", "\"used\"", "\"activated\"", "\"deactivated\"", "\"entered\"",
                                           "\"toggle\"", "\"damage\"", "\"teleport\"", "\"play_sound\"", "\"relay_state\"",
                                           "\"mover_state\"", "\"active\"", "\"open\"", "1e999", "-0.0", "500.0", "\"\"", "[]", "{}",
                                           "\"power\"", "\"door\"", "\"t:entity/pad\"", "\"at\"", "\"x\"", "null", "99999999999" };
        for (int k = 0; k < muts; k++) {
            size_t at = rnd() % n;
            int kind = (int)(rnd() % 4);
            if (kind == 0) buf[0][at] = (char)(rnd() & 0x7F);
            else if (kind == 1 && n > 2) { memmove(buf[0] + at, buf[0] + at + 1, n - at); n--; }
            else {
                const char *t = TOK[rnd() % (sizeof(TOK) / sizeof(TOK[0]))];
                size_t tl = strlen(t);
                /* replace the token at the next quote, if any */
                char *q = strchr(buf[0] + at, '"');
                if (!q || (size_t)(q - buf[0]) + tl + 64 >= sizeof(buf[0]) - n) continue;
                char *q2 = strchr(q + 1, '"');
                if (!q2) continue;
                size_t old = (size_t)(q2 - q) + 1;
                memmove(q + tl, q + old, n - (size_t)(q - buf[0]) - old + 1);
                memcpy(q, t, tl);
                n = n - old + tl;
            }
            buf[0][n] = 0;
        }
        memcpy(buf[1], buf[0], n + 1);
        reset_libs();
        if (which) put_lib(&DIR_, "t.fac", buf[0], NULL, 0);
        bool a = world_loads(which ? WORLD : buf[0], &m, e1, sizeof(e1));
        if (a) {
            /* play it a little: presses everywhere, triggers, steps */
            static hta_world_entities w;
            char err[256];
            CHECK(hta_went_load(&w, &m.world_defs, err, sizeof(err)));
            for (int s = 0; s < 60; s++) {
                uint32_t i = rnd() % (m.world_defs.count ? m.world_defs.count : 1);
                if (m.world_defs.count && m.world_defs.entity[i].kind == HTA_WDEF_INTERACTABLE) press(&w, i, (uint8_t)(rnd() % 8));
                float p[3] = { (float)(rnd() % 16) - 8.0f, (float)(rnd() % 16) - 8.0f, 0 };
                hta_went_sense(&w, (uint8_t)(rnd() % 8), p, true);
                hta_went_step(&w, 0.05f);
                CHECK(w.count <= HTA_WENT_QUEUE && w.hurt_count <= HTA_WENT_MAX_HURTS && w.cue_count <= HTA_WENT_MAX_CUES);
            }
            hta_went_free(&w);
            hta_external_map_free(&m);
            loaded++;
        } else refused_n++;
        /* the same bytes, the same verdict and words */
        reset_libs();
        if (which) put_lib(&DIR_, "t.fac", buf[1], NULL, 0);
        bool b = world_loads(which ? WORLD : buf[1], &m, e2, sizeof(e2));
        if (b) hta_external_map_free(&m);
        CHECK(a == b);
        if (!a && !b) { CHECK(!strcmp(e1, e2)); CHECK(printable(e1) && e1[0]); }
    }
    reset_libs();
    /* Random binding graphs built in C (bypassing the parser, then checked
     * by hta_went_load's own check): whatever loads, runs bounded. */
    static hta_world_defs d;
    int ran = 0;
    for (int iter = 0; iter < 2000; iter++) {
        memset(&d, 0, sizeof(d));
        d.schema = 6;
        uint32_t ne = 2 + rnd() % 30;
        for (uint32_t i = 0; i < ne; i++) {
            hta_wdef *e = &d.entity[d.count++];
            snprintf(e->id, sizeof(e->id), "f:entity/e%u", i);
            e->kind = (uint8_t)(rnd() % 3 == 0 ? HTA_WDEF_INTERACTABLE : HTA_WDEF_RELAY);
            e->def = HTA_WDEF_NO_DEF;
            e->reach = 1.0f;
            e->pos[0] = (float)(rnd() % 8); e->pos[2] = 1.0f;
        }
        uint32_t nb = rnd() % 64;
        for (uint32_t b = 0; b < nb && d.action_count + 8 <= HTA_WDEF_MAX_ACTIONS; b++) {
            hta_wbinding *x = &d.binding[d.binding_count++];
            snprintf(x->id, sizeof(x->id), "b%03u", b);
            x->source = (uint16_t)(rnd() % ne);
            x->event = (uint8_t)(1 + rnd() % (HTA_WEV_COUNT - 1));
            x->first_cond = (uint16_t)d.cond_count;
            x->cond_count = (uint8_t)(rnd() % 3);
            for (uint32_t c = 0; c < x->cond_count; c++)
                d.cond[d.cond_count++] = (hta_wcond){ HTA_WCOND_RELAY_STATE, (uint8_t)(rnd() & 1), (uint16_t)(rnd() % ne) };
            x->first_action = (uint16_t)d.action_count;
            x->action_count = (uint8_t)(1 + rnd() % 8);
            for (uint32_t a = 0; a < x->action_count; a++) {
                bool on = rnd() & 1;
                d.action[d.action_count++] = (hta_waction){ on ? HTA_WACT_ACTIVATE : HTA_WACT_DEACTIVATE, on ? HTA_WIN_ACTIVATE : HTA_WIN_DEACTIVATE,
                                                           (uint16_t)(rnd() % ne), 0, HTA_WDEF_NO_DEF, 0 };
            }
        }
        static hta_world_entities w;
        char err[256];
        if (!hta_went_load(&w, &d, err, sizeof(err))) { CHECK(printable(err)); continue; }
        ran++;
        for (int s = 0; s < 40; s++) {
            uint32_t i = rnd() % ne;
            if (d.entity[i].kind == HTA_WDEF_INTERACTABLE) {
                float eye[3] = { d.entity[i].pos[0] - 0.5f, 0, 1 }, fwd[3] = { 1, 0, 0 };
                hta_went_interact(&w, (uint8_t)(rnd() % 4), eye, fwd);
            } else if (rnd() % 4 == 0) hta_went_send(&w, i, HTA_WIN_ACTIVATE, 0);
            hta_went_step(&w, 0.05f);
            CHECK(w.count <= HTA_WENT_QUEUE);
            for (uint32_t c = 0; c < HTA_WENT_CASCADES; c++) CHECK(w.cascade[c].ops <= HTA_WENT_CASCADE_BUDGET);
        }
        hta_went_free(&w);
    }
    printf("  hostile: 3000 mutated worlds/prefabs (%d loaded and played, %d refused, each twice with the same verdict and "
           "printable words); 2000 random binding graphs (%d ran, every cascade within budget): ok\n", loaded, refused_n, ran);
}

static void lifetimes(void)
{
    static hta_external_map m;
    static hta_world_entities w;
    char err[800];
    for (int i = 0; i < 200; i++) {
        reset_libs();
        const char *world = i % 3 == 2 ? edit(WORLD, "\"t:entity/relay_b\"}", "\"t:entity/relay_x\"}") : WORLD;
        if (i % 5 == 4) put_lib(&DIR_, "t.fac", edit(FAC, "\"source\":\"door\"", "\"source\":\"dor\""), NULL, 0);
        bool ok = world_loads(world, &m, err, sizeof(err));
        if (ok) {
            CHECK(hta_went_load(&w, &m.world_defs, err, sizeof(err)));
            press(&w, (uint32_t)find(&m.world_defs, "t:entity/loop_button"), 0);
            steps(&w, 25);
            hta_went_free(&w);
            hta_external_map_free(&m);
        }
    }
    reset_libs();
    puts("  lifetimes: 200 load/play/free cycles, failures mid-binding (ASan/LSan in build-asan): ok");
}

static void cost(void)
{
    /* The most a world holds: 64 entities, 128 bindings (on 32 sources,
     * one event each), 512 actions. Idle steps look at nothing; an event
     * looks only at its own run. */
    static hta_world_defs d;
    memset(&d, 0, sizeof(d));
    d.schema = 6;
    for (uint32_t i = 0; i < 64; i++) {
        hta_wdef *e = &d.entity[d.count++];
        snprintf(e->id, sizeof(e->id), "p:entity/e%u", i);
        e->kind = i < 32 ? HTA_WDEF_INTERACTABLE : HTA_WDEF_RELAY;
        e->def = HTA_WDEF_NO_DEF;
        e->reach = 0.4f; e->pos[0] = (float)i; e->pos[2] = 1.0f;
    }
    for (uint32_t b = 0; b < 128; b++) {
        hta_wbinding *x = &d.binding[d.binding_count++];
        snprintf(x->id, sizeof(x->id), "b%03u", b);
        x->source = (uint16_t)(b % 32);
        x->event = HTA_WEV_USED;
        x->first_cond = (uint16_t)d.cond_count;
        x->cond_count = 2;
        d.cond[d.cond_count++] = (hta_wcond){ HTA_WCOND_RELAY_STATE, HTA_WRELAY_INACTIVE, (uint16_t)(32 + b % 32) };
        d.cond[d.cond_count++] = (hta_wcond){ HTA_WCOND_RELAY_STATE, HTA_WRELAY_INACTIVE, (uint16_t)(33 + b % 31) };
        x->first_action = (uint16_t)d.action_count;
        x->action_count = 4;
        for (uint32_t a = 0; a < 4; a++)
            d.action[d.action_count++] = (hta_waction){ HTA_WACT_ACTIVATE, HTA_WIN_ACTIVATE, (uint16_t)(32 + (b + a) % 32), 0, HTA_WDEF_NO_DEF, 0 };
    }
    static hta_world_entities w;
    char err[256];
    CHECK(hta_went_load(&w, &d, err, sizeof(err)) || (fprintf(stderr, "cost: %s\n", err), 0));
    clock_t t0 = clock();
    for (int s = 0; s < 100000; s++) hta_went_step(&w, 1.0f / 60.0f);
    double idle = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / 100000.0;
    t0 = clock();
    uint64_t q0 = w.stats.actions_queued;
    int presses = 0;
    for (int s = 0; s < 20000; s++) {
        if (!(s % 5)) {
            uint32_t i = (uint32_t)(s / 5) % 32;
            w.st[i].cooldown = 0.0f;
            float eye[3] = { (float)i - 0.3f, 0, 1 }, fwd[3] = { 1, 0, 0 };
            presses += hta_went_interact(&w, 0, eye, fwd) >= 0;
        }
        hta_went_step(&w, 1.0f / 60.0f);
        if (!(s % 97)) hta_went_reset(&w);
    }
    double busy = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / 20000.0;
    CHECK(presses == 4000 && w.stats.actions_queued > q0);
    printf("  cost: 64 entities, 128 bindings, 512 actions: an idle step %.2f us, a step with a use every 5th %.2f us "
           "(%llu actions queued): ok\n", idle, busy, (unsigned long long)(w.stats.actions_queued - q0));
    hta_went_free(&w);
}

int main(void)
{
    cube(PAYLOAD, 0.25f);
    for (int i = 0; i < 100; i++) { int16_t s = (int16_t)(8000.0 * sin(i * 0.3)); PAYLOAD[MESH_BYTES + i * 2] = (uint8_t)s; PAYLOAD[MESH_BYTES + i * 2 + 1] = (uint8_t)((uint16_t)s >> 8); }
    for (int i = 0; i < 16; i++) { uint8_t *p = PAYLOAD + MESH_BYTES + 200 + i * 4; p[0] = 200; p[1] = 30; p[2] = 30; p[3] = 255; }
    load_good();
    compiled();
    runtime();
    loops();
    refusals();
    world_key();
    hostile();
    lifetimes();
    cost();
    hta_external_map_free(&GOOD);
    dir_free(&DIR_);
    if (failures) { fprintf(stderr, "bindings: %d failures\n", failures); return 1; }
    puts("bindings: all ok");
    return 0;
}
