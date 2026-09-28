/* X8: world state vNext (docs/WORLD_STATE.md) on synthetic worlds built as
 * manifests in C -- runtime objects past the old 64, host-only objects that
 * cost no replicated slot, the deterministic runtime -> spatial / logical
 * mapping, relays replicated as logical flags, late join from one message,
 * a press, a binding, a trigger and a Lua handle on objects above index 63,
 * the limits (1024 objects, 256 movers) and their refusals, and what it
 * costs (load, export, encode, decode, apply). The authored proofs -- Night
 * Shift restored, the stress world, real joiners -- are scripts/test_x8.sh. */
#include "asset/world_def.h"
#include "asset/world_repl.h"
#include "engine/world_entities.h"
#include "game/net_world_state.h"
#include "net/protocol.h"
#include "script/script.h"
#include "game/game.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

/* ---- a manifest writer ------------------------------------------------------------ */

typedef struct { char *p; size_t n, cap; } sb;
static void put(sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void put(sb *b, const char *fmt, ...)
{
    if (!b->cap) { b->cap = 65536; b->p = malloc(b->cap); assert(b->p); }
    va_list a;
    for (;;) {
        va_start(a, fmt);
        int k = vsnprintf(b->p + b->n, b->cap - b->n, fmt, a);
        va_end(a);
        if (k >= 0 && b->n + (size_t)k < b->cap) { b->n += (size_t)k; return; }
        b->cap = b->cap ? b->cap * 2 : 65536;
        b->p = realloc(b->p, b->cap);
        assert(b->p);
    }
}

static const char *LUA =
    "local door = world.entity('t8:entity/door_hi')\n"
    "function on_used(entity, player)\n"
    "  if world.state(door) == 'open' then world.send(door, 'close', player) end\n"
    "  log('lua', tostring(door))\n"
    "end\n";

/* `fill` host-only objects and relays first (so every interesting object
 * lands above index 63), then the objects under test:
 *   button_hi  (interactable) --used [power_hi inactive]--> activate power_hi
 *   power_hi   (relay)        --activated--> open door_hi
 *   door_hi    (mover)
 *   lua_hi     (interactable, Lua: closes door_hi when it is open)
 *   zone_hi    (trigger)      --entered--> teleport dest_hi
 *   dest_hi    (teleport)
 * `movers` extra movers and `relays` extra relays after the filler. */
static char *world(unsigned fill, unsigned movers, unsigned relays, bool specials)
{
    static sb b;
    b.n = 0;
    put(&b, "{\"id\":\"t8:world/big\",\"world_entities\":{\"bindings\":[");
    if (specials)
        put(&b, "{\"actions\":[{\"action\":\"activate\",\"target\":\"t8:entity/power_hi\"}],"
                "\"conditions\":[{\"condition\":\"relay_state\",\"entity\":\"t8:entity/power_hi\",\"is\":\"inactive\"}],"
                "\"event\":\"used\",\"id\":\"b_button\",\"source\":\"t8:entity/button_hi\"},"
                "{\"actions\":[{\"action\":\"open\",\"target\":\"t8:entity/door_hi\"}],\"conditions\":[],"
                "\"event\":\"activated\",\"id\":\"b_power\",\"source\":\"t8:entity/power_hi\"},"
                "{\"actions\":[{\"action\":\"teleport\",\"target\":\"t8:entity/dest_hi\"}],\"conditions\":[],"
                "\"event\":\"entered\",\"id\":\"b_zone\",\"source\":\"t8:entity/zone_hi\"}");
    put(&b, "],\"entities\":[");
    const char *sep = "";
    for (unsigned i = 0; i < fill; i++, sep = ",") {
        switch (i % 4) {
        case 0: put(&b, "%s{\"bounds\":{\"max\":[%u.5,-90.5,1.0],\"min\":[%u.0,-91.0,0.0]},\"id\":\"t8:entity/f%04u\",\"kind\":\"trigger\",\"links\":[]}",
                    sep, 100 + i, 100 + i, i); break;
        case 1: put(&b, "%s{\"id\":\"t8:entity/f%04u\",\"kind\":\"relay\",\"links\":[]}", sep, i); break;
        case 2: put(&b, "%s{\"id\":\"t8:entity/f%04u\",\"kind\":\"interactable\",\"links\":[],\"position\":[%u.0,-95.0,0.5],\"reach\":1.0}",
                    sep, i, 100 + i); break;
        default: put(&b, "%s{\"id\":\"t8:entity/f%04u\",\"kind\":\"teleport\",\"links\":[],\"position\":[%u.0,-99.0,0.05],\"yaw_degrees\":0.0}",
                     sep, i, 100 + i); break;
        }
    }
    for (unsigned i = 0; i < movers; i++, sep = ",")
        put(&b, "%s{\"definition\":\"t8:mover/door\",\"id\":\"t8:entity/m%04u\",\"kind\":\"mover\",\"links\":[],\"position\":[%u.0,-80.0,0.55]}",
            sep, i, 100 + i * 2);
    for (unsigned i = 0; i < relays; i++, sep = ",") put(&b, "%s{\"id\":\"t8:entity/r%04u\",\"kind\":\"relay\",\"links\":[]}", sep, i);
    if (specials)
        put(&b, "%s{\"id\":\"t8:entity/button_hi\",\"kind\":\"interactable\",\"links\":[],\"position\":[-0.14,-1.3,0.55],\"reach\":1.0},"
                "{\"id\":\"t8:entity/power_hi\",\"kind\":\"relay\",\"links\":[]},"
                "{\"definition\":\"t8:mover/door\",\"id\":\"t8:entity/door_hi\",\"kind\":\"mover\",\"links\":[],\"position\":[0.0,0.0,0.55]},"
                "{\"id\":\"t8:entity/lua_hi\",\"kind\":\"interactable\",\"links\":[],\"position\":[-0.14,1.3,0.55],\"reach\":1.0,"
                "\"script\":\"t8:script/lua_hi\"},"
                "{\"bounds\":{\"max\":[3.5,0.5,1.2],\"min\":[2.5,-0.5,-0.1]},\"id\":\"t8:entity/zone_hi\",\"kind\":\"trigger\",\"links\":[]},"
                "{\"id\":\"t8:entity/dest_hi\",\"kind\":\"teleport\",\"links\":[],\"position\":[-4.25,2.4,0.4],\"yaw_degrees\":90.0}", sep);
    put(&b, "],\"mover_definitions\":[{\"id\":\"t8:mover/door\",\"move\":[0.0,1.25,0.0],\"size\":[0.1,1.2,1.1],\"speed\":2.0}],"
            "\"schema\":6,\"scripts\":[");
    if (specials) {
        put(&b, "{\"api\":\"megamod.v1\",\"callbacks\":[\"on_used\"],\"id\":\"t8:script/lua_hi\",\"source\":\"");
        for (const char *c = LUA; *c; c++) {
            if (*c == '\n') put(&b, "\\n");
            else if (*c == '\'' ) put(&b, "'");
            else put(&b, "%c", *c);
        }
        put(&b, "\"}");
    }
    put(&b, "]}}");
    return b.p;
}

static bool parse(const char *json, hta_world_defs *d, char *err, size_t n)
{
    return hta_world_defs_parse((const uint8_t *)json, strlen(json), d, err, n);
}

static int32_t find(const hta_world_defs *d, const char *name)
{
    char id[64];
    snprintf(id, sizeof(id), "t8:entity/%s", name);
    return hta_world_defs_find(d, id);
}

/* The host's replicated state as host_net.c builds it: every entry. */
static void export_state(const hta_world_entities *w, hta_net_world_state *ws)
{
    static hta_went_mover_state ms[HTA_WREP_MAX_SPATIAL];
    memset(ws, 0, sizeof(*ws));
    uint32_t n = hta_went_snapshot(w, ms, HTA_NET_WSTATE_MAX_SPATIAL);
    ws->spatial_total = (uint16_t)n;
    for (uint32_t i = 0; i < n; i++) { ws->phase[i] = ms[i].phase; ws->t[i] = ms[i].t_q; hta_net_bit_set(ws->spatial_has, i, true); }
    uint32_t f = hta_went_flags(w, ws->flag, HTA_NET_WSTATE_MAX_FLAGS);
    ws->flag_total = (uint16_t)f;
    for (uint32_t i = 0; i < f; i++) hta_net_bit_set(ws->flag_has, i, true);
}

/* Host -> wire -> a client: encode, decode, apply, as a joiner does. */
static uint32_t deliver(const hta_world_entities *host, hta_net_client *net, hta_net_wstate_sync *y, hta_world_entities *client,
                        size_t *bytes)
{
    static hta_net_world_state ws;
    static uint8_t buf[HTA_NET_MAX_PACKET];
    size_t n = 0;
    char err[160];
    export_state(host, &ws);
    assert(hta_net_world_state_pack(buf, sizeof(buf), &ws, &n));
    if (bytes) *bytes = n;
    assert(hta_net_world_state_unpack(buf, n, &net->world_state, err, sizeof(err)));
    net->connected = true;
    net->have_world_state = true;
    net->last_world_state_tick++;
    return hta_net_wstate_apply(y, net, client);
}

static hta_world_defs defs, defs2;
static hta_world_entities host, cl_a, cl_b;
static hta_net_client net_a, net_b;
static hta_net_wstate_sync sync_a, sync_b;

static void step(hta_world_entities *w, int n) { for (int i = 0; i < n; i++) hta_went_step(w, 1.0f / 60.0f); }

/* ---- 1. identity: over 64, host-only objects free, a deterministic map ------------ */

static void identity(void)
{
    char err[400];
    double t0 = now_ms();
    bool ok = parse(world(80, 0, 0, true), &defs, err, sizeof(err));
    if (!ok) fprintf(stderr, "parse: %s\n", err);
    CHECK(ok);
    double t1 = now_ms();
    CHECK(defs.count == 86);
    int32_t button = find(&defs, "button_hi"), power = find(&defs, "power_hi"), door = find(&defs, "door_hi"),
            lua = find(&defs, "lua_hi"), zone = find(&defs, "zone_hi"), dest = find(&defs, "dest_hi");
    CHECK(button == 80 && power == 81 && door == 82 && lua == 83 && zone == 84 && dest == 85);
    static hta_wrep_map m, m2;
    CHECK(hta_wrep_build(&m, &defs, err, sizeof(err)));
    /* 20 filler relays + power_hi are the logical flags; door_hi the only
     * mover; everything else -- 64 of 86 -- is host-only: no slot, no byte. */
    uint32_t sp, fl, ho;
    hta_wrep_count(&defs, &sp, &fl, &ho);
    CHECK(sp == 1 && fl == 21 && ho == 64 && m.spatial_count == 1 && m.flag_count == 21);
    CHECK(m.index[door] == 0 && m.spatial[0] == door && m.index[power] == 20 && m.flag[20] == power);
    CHECK(m.index[button] == HTA_WREP_NONE && m.index[zone] == HTA_WREP_NONE && m.index[dest] == HTA_WREP_NONE);
    for (uint32_t k = 1; k < m.flag_count; k++) CHECK(m.flag[k] > m.flag[k - 1]);      /* runtime order */
    /* The same bytes give the same map (and nothing else enters it). */
    CHECK(parse(world(80, 0, 0, true), &defs2, err, sizeof(err)) && hta_wrep_build(&m2, &defs2, err, sizeof(err)));
    CHECK(!memcmp(&m, &m2, sizeof(m)));
    /* Handles keep 16-bit slots: the highest objects resolve, stale ones do not. */
    CHECK(hta_went_load(&host, &defs, err, sizeof(err)));
    hta_went_handle h = hta_went_handle_of(&host, (uint32_t)dest);
    CHECK(h && hta_went_resolve(&host, h) == dest);
    hta_went_reset(&host);
    CHECK(hta_went_resolve(&host, h) == -1 && hta_went_resolve(&host, hta_went_handle_of(&host, (uint32_t)dest)) == dest);
    char line[256];
    CHECK(hta_went_describe(&host, (uint32_t)power, line, sizeof(line)) &&
          !strcmp(line, "t8:entity/power_hi relay runtime 81 logical 20 inactive"));
    CHECK(hta_went_describe(&host, (uint32_t)zone, line, sizeof(line)) && !strcmp(line, "t8:entity/zone_hi trigger runtime 84 host-only"));
    hta_went_free(&host);
    printf("  identity: 86 runtime objects (1 spatial, 21 logical, 64 host-only), the map deterministic, handles above 63 "
           "(parsed and checked in %.2f ms): ok\n", t1 - t0);
}

/* ---- 2. play above 63, replicated to a joiner and a late joiner ------------------- */

static void play(void)
{
    char err[400];
    CHECK(parse(world(80, 0, 0, true), &defs, err, sizeof(err)));
    CHECK(hta_went_load(&host, &defs, err, sizeof(err)));
    CHECK(hta_went_load(&cl_a, &defs, err, sizeof(err)) && hta_went_load(&cl_b, &defs, err, sizeof(err)));
    cl_a.remote = cl_b.remote = true;
    int32_t button = find(&defs, "button_hi"), power = find(&defs, "power_hi"), door = find(&defs, "door_hi"),
            lua = find(&defs, "lua_hi"), zone = find(&defs, "zone_hi");
    /* A joins now. */
    hta_net_wstate_reset(&sync_a);
    memset(&net_a, 0, sizeof(net_a));
    size_t bytes = 0;
    deliver(&host, &net_a, &sync_a, &cl_a, &bytes);
    CHECK(sync_a.synced && !cl_a.st[power].active && cl_a.st[door].phase == HTA_MOVER_CLOSED);
    /* A press at the button (runtime 80): found by the host's reach test,
     * never named by a client. */
    const float eye[3] = { -0.9f, -1.3f, 0.62f }, fwd[3] = { 1, 0, 0 };
    CHECK(hta_went_interact(&host, 3, eye, fwd) == button);
    CHECK(hta_went_interact(&cl_a, 3, eye, fwd) == -1);             /* a joiner never dispatches */
    CHECK(hta_went_request(&cl_a, HTA_WACT_OPEN, (uint32_t)door, 3) == HTA_WENT_REFUSED_HERE);
    step(&host, 2);
    /* The binding on runtime 80 activated runtime 81 (a relay), whose binding opened 82 (a mover). */
    CHECK(host.st[power].active && host.st[door].phase == HTA_MOVER_OPENING);
    CHECK(host.stats.bindings_matched >= 2);
    deliver(&host, &net_a, &sync_a, &cl_a, &bytes);
    CHECK(cl_a.st[power].active && cl_a.st[door].phase == HTA_MOVER_OPENING);
    CHECK(cl_a.stats.bindings_matched == 0 && cl_a.stats.dispatched == 0);  /* nothing evaluated on the client */
    step(&host, 60);
    step(&cl_a, 60);
    deliver(&host, &net_a, &sync_a, &cl_a, &bytes);
    CHECK(host.st[door].phase == HTA_MOVER_OPEN && cl_a.st[door].phase == HTA_MOVER_OPEN && cl_a.st[door].t == 1.0f);
    /* B joins late: one message, the current state; no history, no binding,
     * no sound (the snap is silent). */
    hta_net_wstate_reset(&sync_b);
    memset(&net_b, 0, sizeof(net_b));
    deliver(&host, &net_b, &sync_b, &cl_b, &bytes);
    step(&cl_b, 1);
    CHECK(sync_b.synced && cl_b.st[power].active && cl_b.st[door].phase == HTA_MOVER_OPEN && cl_b.st[door].t == 1.0f);
    CHECK(cl_b.cue_count == 0 && cl_b.stats.dispatched == 0 && cl_b.stats.bindings_matched == 0);
    /* Deactivate and reactivate (a request, as a binding or Lua makes it):
     * both joiners follow each change. */
    CHECK(hta_went_request(&host, HTA_WACT_DEACTIVATE, (uint32_t)power, HTA_WENT_NO_ACTOR) == HTA_WENT_QUEUED);
    step(&host, 1);
    deliver(&host, &net_a, &sync_a, &cl_a, NULL); deliver(&host, &net_b, &sync_b, &cl_b, NULL);
    CHECK(!host.st[power].active && !cl_a.st[power].active && !cl_b.st[power].active);
    CHECK(hta_went_request(&host, HTA_WACT_ACTIVATE, (uint32_t)power, HTA_WENT_NO_ACTOR) == HTA_WENT_QUEUED);
    step(&host, 1);
    deliver(&host, &net_a, &sync_a, &cl_a, NULL); deliver(&host, &net_b, &sync_b, &cl_b, NULL);
    CHECK(cl_a.st[power].active && cl_b.st[power].active);
    /* The trigger at runtime 84: host evaluation, teleport to runtime 85. */
    const float feet[3] = { 3.0f, 0.0f, 0.0f };
    hta_went_sense(&host, 5, feet, true);
    step(&host, 1);
    CHECK(host.teleport_count == 1 && host.teleports[0].actor == 5 && fabsf(host.teleports[0].pos[0] + 4.25f) < 1e-4f);
    hta_went_sense(&cl_a, 5, feet, true);
    step(&cl_a, 1);
    CHECK(cl_a.teleport_count == 0);                                  /* host-only */
    (void)zone;
    /* Lua: world.entity resolved door_hi (runtime 82) at load; pressing
     * lua_hi (83) runs on_used, which closes it. */
    static hta_game game;
    memset(&game, 0, sizeof(game));
    hta_script_host *s = hta_script_create(&defs, &host, &game, NULL, NULL, err, sizeof(err));
    if (!s) fprintf(stderr, "script: %s\n", err);
    CHECK(s);
    if (s) {
        const float eye2[3] = { -0.9f, 1.3f, 0.62f };
        for (int i = 0; i < 40; i++) hta_went_step(&host, 1.0f / 60.0f);
        CHECK(hta_went_interact(&host, 2, eye2, fwd) == lua);
        step(&host, 1);
        hta_script_phase(s, 1.0f / 60.0f);
        step(&host, 1);
        CHECK(host.st[door].phase == HTA_MOVER_CLOSING);
        CHECK(hta_script_get_stats(s)->requests == 1 && hta_script_get_stats(s)->errors == 0);
        deliver(&host, &net_b, &sync_b, &cl_b, NULL);
        CHECK(cl_b.st[door].phase == HTA_MOVER_CLOSING);
        CHECK(hta_script_destroy(s) == 0);
    }
    /* A client whose world differs (a mover fewer) refuses the message whole. */
    static hta_world_entities other;
    CHECK(parse(world(80, 0, 0, false), &defs2, err, sizeof(err)) && hta_went_load(&other, &defs2, err, sizeof(err)));
    other.remote = true;
    static hta_net_wstate_sync y;
    static hta_net_client n;
    hta_net_wstate_reset(&y); memset(&n, 0, sizeof(n));
    CHECK(deliver(&host, &n, &y, &other, NULL) == 0 && y.refused == 1 &&
          !strcmp(y.error, "WORLD_STATE describes 1 movers and 21 logical flags; this world has 0 and 20"));
    hta_went_free(&other);
    hta_went_free(&host); hta_went_free(&cl_a); hta_went_free(&cl_b);
    printf("  play above 63: press (runtime 80) -> binding -> relay (81) -> door (82); trigger (84) -> teleport (85); Lua handle "
           "on 82 from 83; joiner and late joiner by state only (%zu bytes), no client evaluation; a different world refused: ok\n", bytes);
}

/* ---- 3. the limits: the most a world may hold, and one more -------------------------- */

static void limits(void)
{
    char err[400];
    /* 1024 objects: 256 movers (the spatial limit), 768 relays (logical). */
    double t0 = now_ms();
    bool ok = parse(world(0, 256, 768, false), &defs, err, sizeof(err));
    double t1 = now_ms();
    if (!ok) fprintf(stderr, "max: %s\n", err);
    CHECK(ok && defs.count == 1024);
    CHECK(hta_went_load(&host, &defs, err, sizeof(err)));
    double t2 = now_ms();
    CHECK(host.rep.spatial_count == 256 && host.rep.flag_count == 768 && host.rep.spatial[255] == 255 && host.rep.flag[767] == 1023);
    hta_went_handle h = hta_went_handle_of(&host, 1023);
    CHECK(hta_went_resolve(&host, h) == 1023);
    /* every mover moving, every relay on: the largest message, one packet */
    for (uint32_t i = 0; i < 256; i++) CHECK(hta_went_request(&host, HTA_WACT_OPEN, i, HTA_WENT_NO_ACTOR) == HTA_WENT_QUEUED);
    for (uint32_t i = 256; i < 1024; i += 2) hta_went_request(&host, HTA_WACT_ACTIVATE, i, HTA_WENT_NO_ACTOR);
    step(&host, 4);
    CHECK(hta_went_load(&cl_a, &defs, err, sizeof(err)));
    cl_a.remote = true;
    hta_net_wstate_reset(&sync_a); memset(&net_a, 0, sizeof(net_a));
    size_t bytes = 0;
    deliver(&host, &net_a, &sync_a, &cl_a, &bytes);
    CHECK(sync_a.synced && bytes == hta_net_world_state_bytes(256, 256, 768) && bytes + HTA_NET_HEADER <= HTA_NET_MAX_PACKET);
    bool same = true;
    for (uint32_t i = 0; i < 1024; i++) same &= host.st[i].phase == cl_a.st[i].phase && host.st[i].active == cl_a.st[i].active;
    CHECK(same);
    /* what it costs, per message, on this machine */
    static hta_net_world_state ws;
    static uint8_t buf[HTA_NET_MAX_PACKET];
    size_t n = 0;
    const int reps = 2000;
    double a = now_ms();
    for (int i = 0; i < reps; i++) export_state(&host, &ws);
    double b = now_ms();
    for (int i = 0; i < reps; i++) hta_net_world_state_pack(buf, sizeof(buf), &ws, &n);
    double c = now_ms();
    for (int i = 0; i < reps; i++) hta_net_world_state_unpack(buf, n, &net_a.world_state, err, sizeof(err));
    double d = now_ms();
    for (int i = 0; i < reps; i++) { net_a.last_world_state_tick++; hta_net_wstate_apply(&sync_a, &net_a, &cl_a); }
    double e = now_ms();
    for (int i = 0; i < reps; i++) hta_went_step(&host, 1.0f / 60.0f);
    double f = now_ms();
    printf("  cost, 1024 objects (256 movers, 768 relays): parse+check %.2f ms, load %.2f ms; per message export %.1f us, "
           "encode %.1f us (%zu bytes), decode %.1f us, apply %.1f us; host step %.1f us\n",
           t1 - t0, t2 - t1, (b - a) * 1e3 / reps, (c - b) * 1e3 / reps, n, (d - c) * 1e3 / reps, (e - d) * 1e3 / reps,
           (f - e) * 1e3 / reps);
    printf("  memory: world definitions %zu bytes, world runtime %zu bytes (fixed; X7: ~106 KB and ~135 KB)\n",
           sizeof(hta_world_defs), sizeof(hta_world_entities));
    hta_went_free(&host); hta_went_free(&cl_a);
    /* One past each limit: refused, in words. */
    CHECK(!parse(world(0, 256, 769, false), &defs, err, sizeof(err)) &&
          strstr(err, "world has more than 1024 runtime objects (world entities), exceeding limit 1024"));
    CHECK(!parse(world(0, 257, 0, false), &defs, err, sizeof(err)) &&
          strstr(err, "world has 257 movers, exceeds the spatial replication limit 256"));
    printf("  limits: 1024 objects load (256 movers, 768 relays: %zu-byte snapshot); 1025 objects and 257 movers refused: ok\n", bytes);
}

/* A world with only host-side objects still has a complete, empty state.
 * The header lets a late joiner mark that world synced. */
static void host_only(void)
{
    char err[400];
    CHECK(parse(world(1, 0, 0, false), &defs, err, sizeof(err)));
    CHECK(hta_went_load(&host, &defs, err, sizeof(err)));
    CHECK(hta_went_load(&cl_a, &defs, err, sizeof(err)));
    cl_a.remote = true;
    CHECK(host.rep.runtime_count == 1 && !host.rep.spatial_count && !host.rep.flag_count);
    hta_net_wstate_reset(&sync_a); memset(&net_a, 0, sizeof(net_a));
    size_t bytes = 0;
    CHECK(deliver(&host, &net_a, &sync_a, &cl_a, &bytes) == 0);
    CHECK(bytes == HTA_NET_WSTATE_HEADER && sync_a.synced && !sync_a.refused);
    hta_went_free(&host); hta_went_free(&cl_a);
    puts("  host-only world: one runtime object, no replication slots, five-byte state, late join synced: ok");
}

int main(void)
{
    identity();
    play();
    host_only();
    limits();
    if (failures) { printf("world state: %d failures\n", failures); return 1; }
    puts("world state: all ok");
    return 0;
}
