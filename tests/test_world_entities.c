/* World entities (X1): definitions parsed and checked, and the runtime's
 * handles, bounded queue, relay, mover collision, trigger and teleport --
 * on a synthetic world built in C (the runtime-side fixture; the authored
 * X1 world comes from Open Asset Lab: docs/WORLD_ENTITIES.md). */
#include "asset/world_def.h"
#include "engine/world_entities.h"
#include "engine/player.h"
#include "gfx/scene_visual.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the C fixture: button -> relay -> door, trigger -> teleport ---- */

static uint32_t add(hta_world_defs *d, const char *name, uint8_t kind)
{
    hta_wdef *e = &d->entity[d->count];
    memset(e, 0, sizeof(*e));
    snprintf(e->id, sizeof(e->id), "x1:entity/%s", name);
    e->kind = kind;
    e->first_link = (uint16_t)d->link_count;
    e->def = HTA_WDEF_NO_DEF;
    return d->count++;
}

static void link(hta_world_defs *d, uint32_t from, uint8_t event, uint32_t to, uint8_t input)
{
    /* Links of one entity are contiguous: build each entity's in one go. */
    assert(d->entity[from].first_link + d->entity[from].link_count == d->link_count);
    d->link[d->link_count++] = (hta_wdef_link){ event, input, (uint16_t)to };
    d->entity[from].link_count++;
}

enum { BUTTON, RELAY, DOOR, TRIGGER, DEST };

static void x1_fixture(hta_world_defs *d)
{
    memset(d, 0, sizeof(*d));
    uint32_t b = add(d, "button_main", HTA_WDEF_INTERACTABLE);
    d->entity[b].pos[0] = -0.14f; d->entity[b].pos[1] = -1.3f; d->entity[b].pos[2] = 0.55f;
    d->entity[b].reach = 1.0f;
    link(d, b, HTA_WEV_USED, RELAY, HTA_WIN_ACTIVATE);
    uint32_t r = add(d, "relay_main", HTA_WDEF_RELAY);
    link(d, r, HTA_WEV_FIRED, DOOR, HTA_WIN_OPEN);
    uint32_t door = add(d, "door_main", HTA_WDEF_MOVER);
    hta_wdef *dd = &d->entity[door];
    dd->pos[2] = 0.55f;                   /* the closed box -0.05..0.05, -0.6..0.6, 0..1.1 */
    hta_wmover_def *md = &d->mover_def[d->mover_def_count];
    md->size[0] = 0.1f; md->size[1] = 1.2f; md->size[2] = 1.1f;
    md->move[1] = 1.25f; md->speed = 1.0f;
    dd->def = (uint16_t)d->mover_def_count++;
    uint32_t t = add(d, "teleport_trigger", HTA_WDEF_TRIGGER);
    hta_wdef *tt = &d->entity[t];
    tt->min[0] = 2.5f; tt->min[1] = -0.5f; tt->min[2] = -0.1f;
    tt->max[0] = 3.5f; tt->max[1] = 0.5f; tt->max[2] = 1.2f;
    link(d, t, HTA_WEV_ENTERED, DEST, HTA_WIN_TELEPORT);
    uint32_t dst = add(d, "teleport_destination", HTA_WDEF_TELEPORT);
    d->entity[dst].pos[0] = -4.25f; d->entity[dst].pos[1] = 2.4f; d->entity[dst].pos[2] = 0.4f;
    d->entity[dst].yaw = 0.5f;
}

/* What Open Asset Lab writes for the same world (assetlab/fixtures.py),
 * among other manifest keys -- one of them a string that mentions the
 * section's name, which must not be mistaken for it. */
static const char *MANIFEST =
    "{\"bounds\":{\"max\":[5,3,1.6],\"min\":[-5,-3,-0.2]},\"display_name\":\"X1 \\\"Event\\\" Lab\","
    "\"id\":\"x1:world/event_lab\",\"notes\":[\"\\\"world_entities\\\":{broken\",{\"a\":[1,2,{}]},null,true,-1.5e3],"
    "\"world_entities\":{\"entities\":["
    "{\"id\":\"x1:entity/button_main\",\"kind\":\"interactable\",\"links\":[{\"event\":\"used\",\"input\":\"activate\",\"target\":\"x1:entity/relay_main\"}],\"position\":[-0.14,-1.3,0.55],\"reach\":1.0},"
    "{\"id\":\"x1:entity/relay_main\",\"kind\":\"relay\",\"links\":[{\"event\":\"fired\",\"input\":\"open\",\"target\":\"x1:entity/door_main\"}]},"
    "{\"bounds\":{\"max\":[0.05,0.6,1.1],\"min\":[-0.05,-0.6,0.0]},\"id\":\"x1:entity/door_main\",\"kind\":\"mover\",\"links\":[],\"move\":[0.0,1.25,0.0],\"speed\":1.0},"
    "{\"bounds\":{\"max\":[3.5,0.5,1.2],\"min\":[2.5,-0.5,-0.1]},\"id\":\"x1:entity/teleport_trigger\",\"kind\":\"trigger\",\"links\":[{\"event\":\"entered\",\"input\":\"teleport\",\"target\":\"x1:entity/teleport_destination\"}]},"
    "{\"id\":\"x1:entity/teleport_destination\",\"kind\":\"teleport\",\"links\":[],\"position\":[-4.25,2.4,0.4],\"yaw_degrees\":90.0}"
    "],\"schema\":1},\"zzz\":{\"world_entities\":1}}";

static bool parses(const char *json, char *err, size_t n)
{
    static hta_world_defs d;
    return hta_world_defs_parse((const uint8_t *)json, strlen(json), &d, err, n);
}

/* MANIFEST with `from` replaced by `to` once. */
static const char *patch(const char *from, const char *to)
{
    static char buf[8192];
    const char *at = strstr(MANIFEST, from);
    assert(at);
    snprintf(buf, sizeof(buf), "%.*s%s%s", (int)(at - MANIFEST), MANIFEST, to, at + strlen(from));
    return buf;
}

static void expect_fail(const char *json, const char *want)
{
    char err[256];
    bool ok = parses(json, err, sizeof(err));
    if (ok || !strstr(err, want)) {
        fprintf(stderr, "wanted failure '%s', got %s '%s'\n", want, ok ? "success" : "failure", err);
        assert(0);
    }
}

static void test_parse(void)
{
    static hta_world_defs d;
    char err[256];
    assert(hta_world_defs_parse((const uint8_t *)MANIFEST, strlen(MANIFEST), &d, err, sizeof(err)) || (fprintf(stderr, "%s\n", err), 0));
    assert(d.count == 5 && d.link_count == 3);
    assert(!strcmp(d.entity[0].id, "x1:entity/button_main") && d.entity[0].kind == HTA_WDEF_INTERACTABLE);
    assert(d.link[0].target == 1 && d.link[0].event == HTA_WEV_USED && d.link[0].input == HTA_WIN_ACTIVATE);
    assert(d.link[1].target == 2 && d.link[1].input == HTA_WIN_OPEN);
    assert(d.link[2].target == 4 && d.link[2].input == HTA_WIN_TELEPORT);
    /* Schema 1: the door's inline parameters became an unnamed definition. */
    assert(d.schema == 1 && d.mover_def_count == 1 && d.entity[2].def == 0 && !d.mover_def[0].id[0]);
    assert(fabsf(d.mover_def[0].move[1] - 1.25f) < 1e-6f && d.mover_def[0].speed == 1.0f);
    assert(fabsf(d.mover_def[0].size[1] - 1.2f) < 1e-6f && fabsf(d.entity[2].pos[2] - 0.55f) < 1e-6f);
    assert(d.entity[0].def == HTA_WDEF_NO_DEF && hta_wdef_mover(&d, 0) == NULL && hta_wdef_mover(&d, 2) == &d.mover_def[0]);
    assert(fabsf(d.entity[4].yaw - 1.5707963f) < 1e-5f);
    assert(hta_world_defs_find(&d, "x1:entity/door_main") == 2);
    /* No section: nothing, and fine. */
    assert(hta_world_defs_parse((const uint8_t *)"{\"a\":1}", 7, &d, err, sizeof(err)) && d.count == 0);
    /* Reordered placements keep their links (resolved by ID, not position). */
    static char re[8192];
    const char *b0 = strstr(MANIFEST, "{\"id\":\"x1:entity/button_main\""), *b1 = strstr(MANIFEST, "{\"id\":\"x1:entity/relay_main\"");
    const char *end = strstr(MANIFEST, "],\"schema\"");
    snprintf(re, sizeof(re), "%.*s%.*s,%.*s%s", (int)(b0 - MANIFEST), MANIFEST, (int)(end - b1), b1,
             (int)(b1 - b0 - 1), b0, end);
    assert(hta_world_defs_parse((const uint8_t *)re, strlen(re), &d, err, sizeof(err)) || (fprintf(stderr, "%s\n", err), 0));
    int32_t button = hta_world_defs_find(&d, "x1:entity/button_main"), relay = hta_world_defs_find(&d, "x1:entity/relay_main");
    assert(button == 4 && relay == 0);
    assert(d.link[d.entity[button].first_link].target == (uint16_t)relay);
    assert(d.entity[d.link[d.entity[relay].first_link].target].kind == HTA_WDEF_MOVER);

    expect_fail(patch("\"id\":\"x1:entity/relay_main\"", "\"id\":\"x1:entity/button_main\""), "duplicate placed ID");
    expect_fail(patch("\"target\":\"x1:entity/relay_main\"", "\"target\":\"x1:entity/relay_mian\""),
                "x1:entity/button_main references missing placed entity x1:entity/relay_mian");
    expect_fail(patch("\"id\":\"x1:entity/door_main\"", "\"id\":\"x1:entity/Door-Main\""), "'x1:entity/Door-Main': malformed placed ID");
    expect_fail(patch("\"id\":\"x1:entity/relay_main\",", "\"id\":\"x1:weapon/relay_main\","), "malformed placed ID");
    expect_fail(patch("{\"bounds\":{\"max\":[0.05", "{\"id\":\"other:entity/relay_z\",\"kind\":\"relay\"},{\"bounds\":{\"max\":[0.05"),
                "not in the world's namespace");
    expect_fail(patch("\"input\":\"open\"", "\"input\":\"explode\""), "unknown input 'explode'");
    expect_fail(patch("\"input\":\"open\"", "\"input\":\"teleport\""), "target does not accept x1:entity/door_main.teleport");
    expect_fail(patch("\"event\":\"fired\"", "\"event\":\"used\""), "does not emit 'used'");
    expect_fail(patch("\"kind\":\"relay\"", "\"kind\":\"logic_relay\""), "unknown kind 'logic_relay'");
    expect_fail(patch("\"schema\":1", "\"schema\":10"), "unsupported schema");
    expect_fail(patch("\"links\":[],\"move\"", "\"definition\":\"x1:mover/door\",\"links\":[],\"move\""),
                "x1:entity/door_main: mover definitions need world_entities schema 2");
    expect_fail(patch("\"schema\":1}", "\"mover_definitions\":[],\"schema\":1}"), "mover_definitions need schema 2");
    expect_fail(patch(",\"speed\":1.0}", "}"), "a schema 1 mover needs bounds, move and speed");
    expect_fail(patch("\"kind\":\"relay\",", "\"kind\":\"relay\",\"speed\":2,"), "x1:entity/relay_main: only a mover takes move and speed");
    expect_fail(patch("\"speed\":1.0", "\"speed\":1.0,\"flags\":7"), "unknown field 'flags'");
    expect_fail(patch("\"speed\":1.0", "\"speed\":-1.0"), "mover speed out of range");
    expect_fail(patch("\"move\":[0.0,1.25,0.0]", "\"move\":[0.0,0.0,0.0]"), "mover move out of range");
    expect_fail(patch("\"speed\":1.0", "\"speed\":1e999"), "malformed 'speed'");
    expect_fail(patch("\"max\":[3.5,0.5,1.2]", "\"max\":[2.5,0.5,1.2]"), "bounds are empty");
    expect_fail(patch("\"position\":[-4.25,2.4,0.4]", "\"position\":[3.0,0.0,0.5]"), "destination is inside trigger");
    expect_fail(patch("\"position\":[-4.25,2.4,0.4]", "\"position\":[-4.25,2.4]"), "malformed 'position'");
    expect_fail(patch("\"links\":[],\"position\":[-4.25", "\"links\":[{\"event\":\"fired\",\"input\":\"activate\",\"target\":\"x1:entity/relay_main\"}],\"position\":[-4.25"),
                "does not emit 'fired'");
    /* A cycle: the relay activates... a second relay that activates it. */
    expect_fail(patch("{\"bounds\":{\"max\":[0.05",
                      "{\"id\":\"x1:entity/relay_b\",\"kind\":\"relay\",\"links\":[{\"event\":\"fired\",\"input\":\"activate\",\"target\":\"x1:entity/relay_c\"}]},"
                      "{\"id\":\"x1:entity/relay_c\",\"kind\":\"relay\",\"links\":[{\"event\":\"fired\",\"input\":\"activate\",\"target\":\"x1:entity/relay_b\"}]},"
                      "{\"bounds\":{\"max\":[0.05"), "link cycle");
    expect_fail(patch("\"target\":\"x1:entity/door_main\"", "\"target\":\"x1:entity/relay_main\""), "cannot target itself");
    expect_fail(patch("\"zzz\":{\"world_entities\":1}}", "\"zzz\":1} trailing"), "trailing bytes");
    expect_fail(patch("\"zzz\":{\"world_entities\":1}}", "\"zzz\":{\"world_entities\":1}"), "malformed manifest");
    expect_fail(patch("\"schema\":1}", "\"schema\":1},\"world_entities\":{\"entities\":[],\"schema\":1}"), "appears twice");
    /* Every truncation fails cleanly (the parser never reads past the end). */
    for (size_t n = 0; n < strlen(MANIFEST); n++) {
        static hta_world_defs t;
        assert(!hta_world_defs_parse((const uint8_t *)MANIFEST, n, &t, err, sizeof(err)));
    }
    printf("  parse: ok\n");
}

/* A floor for the grid the movers' instances ride on. */
static void floor_grid(hta_collision *c, hta_bsp_mesh *m)
{
    static hta_vertex v[4];
    static uint32_t ix[6] = { 0, 1, 2, 0, 2, 3 };
    float p[4][2] = { { -6, -4 }, { 6, -4 }, { 6, 4 }, { -6, 4 } };
    for (int i = 0; i < 4; i++) { v[i].pos[0] = p[i][0]; v[i].pos[1] = p[i][1]; v[i].pos[2] = 0; v[i].normal[2] = 1; }
    memset(m, 0, sizeof(*m));
    m->vertices = v; m->vertex_count = 4; m->indices = ix; m->index_count = 6;
    m->bounds_min[0] = -6; m->bounds_min[1] = -4; m->bounds_max[0] = 6; m->bounds_max[1] = 4; m->bounds_max[2] = 2;
    assert(hta_collision_build(c, m));
}

static bool door_blocks(hta_world_entities *w, hta_collision *c)
{
    static hta_collision_instance inst[8];
    c->instances = inst;
    c->instance_count = hta_went_instances(w, inst, 8);
    const float from[3] = { -1.0f, 0.0f, 0.5f }, dir[3] = { 1, 0, 0 };
    float t, hit[3], nrm[3];
    return hta_collision_ray(c, from, dir, 2.0f, &t, hit, nrm) && fabsf(hit[0] + 0.05f) < 0.01f;
}

static void run(hta_world_entities *w, float seconds)
{
    for (int i = 0; i < (int)(seconds * 60.0f + 0.5f); i++) hta_went_step(w, 1.0f / 60.0f);
}

static void test_button_relay_door(void)
{
    static hta_world_defs d;
    static hta_world_entities w;
    char err[256];
    x1_fixture(&d);
    assert(hta_world_defs_check(&d, err, sizeof(err)) || (fprintf(stderr, "%s\n", err), 0));
    assert(hta_went_load(&w, &d, err, sizeof(err)));
    hta_collision c; hta_bsp_mesh m;
    floor_grid(&c, &m);
    assert(w.st[DOOR].phase == HTA_MOVER_CLOSED && door_blocks(&w, &c));
    /* Looking away from the button, or out of reach: nothing. */
    const float eye[3] = { -0.6f, -1.3f, 0.62f }, fwd[3] = { 1, 0, 0 }, back[3] = { -1, 0, 0 };
    const float far_eye[3] = { -2.0f, -1.3f, 0.62f };
    assert(hta_went_interact(&w, 3, eye, back) < 0);
    assert(hta_went_interact(&w, 3, far_eye, fwd) < 0);
    assert(hta_went_can_interact(&w, eye, fwd) == BUTTON);
    assert(hta_went_interact(&w, 3, eye, fwd) == BUTTON);
    /* Queued, not run: the chain runs in the next step, all of it (links
     * are zero-delay): button -> relay (activate) -> door (open). */
    assert(w.count == 1 && w.queue[w.head].input == HTA_WIN_ACTIVATE && w.st[DOOR].phase == HTA_MOVER_CLOSED);
    hta_went_step(&w, 1.0f / 60.0f);
    assert(w.stats.dispatched == 2 && w.count == 0 && w.st[DOOR].phase == HTA_MOVER_OPENING);
    assert(door_blocks(&w, &c));                      /* barely moved */
    run(&w, 0.6f);
    float off[3];
    hta_went_offset(&w, DOOR, off);
    assert(off[1] > 0.5f && off[1] < 1.0f && off[0] == 0.0f && off[2] == 0.0f);
    run(&w, 1.0f);
    assert(w.st[DOOR].phase == HTA_MOVER_OPEN && w.st[DOOR].t == 1.0f);
    hta_went_offset(&w, DOOR, off);
    assert(fabsf(off[1] - 1.25f) < 1e-6f);
    /* Collision followed it: the doorway is clear, the door is where it went. */
    assert(!door_blocks(&w, &c));
    static hta_collision_instance inst[8];
    c.instances = inst; c.instance_count = hta_went_instances(&w, inst, 8);
    float t, hit[3], nrm[3];
    const float from2[3] = { -1.0f, 1.25f, 0.5f }, dir[3] = { 1, 0, 0 };
    assert(hta_collision_ray(&c, from2, dir, 2.0f, &t, hit, nrm) && fabsf(hit[0] + 0.05f) < 0.01f);
    /* Pressing again: open stays open, nothing else happens. */
    uint64_t dispatched = w.stats.dispatched;
    run(&w, 1.0f);
    assert(hta_went_interact(&w, 3, eye, fwd) == BUTTON);
    run(&w, 1.0f);
    assert(w.stats.dispatched == dispatched + 2 && w.st[DOOR].phase == HTA_MOVER_OPEN);
    /* A press inside the cooldown is consumed but does nothing. */
    assert(hta_went_interact(&w, 3, eye, fwd) == BUTTON && w.count == 1);
    assert(hta_went_interact(&w, 4, eye, fwd) == BUTTON && w.count == 1);
    run(&w, 0.1f);
    /* A new round: closed, collision back, the old events gone. */
    hta_went_reset(&w);
    hta_went_step(&w, 0.0f);
    assert(w.st[DOOR].phase == HTA_MOVER_CLOSED && door_blocks(&w, &c));
    hta_collision_free(&c);
    hta_went_free(&w);
    printf("  button -> relay -> door: ok\n");
}

static void test_trigger_teleport(void)
{
    static hta_world_defs d;
    static hta_world_entities w;
    x1_fixture(&d);
    assert(hta_went_load(&w, &d, NULL, 0));
    const float outside[3] = { 1.5f, 0.0f, 0.0f }, inside[3] = { 3.0f, 0.0f, 0.0f };
    hta_went_sense(&w, 5, outside, true);
    hta_went_step(&w, 1.0f / 60.0f);
    assert(w.teleport_count == 0);
    hta_went_sense(&w, 5, inside, true);             /* outside -> inside */
    hta_went_step(&w, 1.0f / 60.0f);
    assert(w.teleport_count == 1 && w.teleports[0].actor == 5);
    assert(w.teleports[0].pos[0] == -4.25f && w.teleports[0].pos[2] == 0.4f && w.teleports[0].yaw == 0.5f);
    for (int i = 0; i < 30; i++) {                   /* standing in it: no more */
        hta_went_sense(&w, 5, inside, true);
        hta_went_step(&w, 1.0f / 60.0f);
        assert(w.teleport_count == 0);
    }
    /* Another actor entering fires for them alone. */
    hta_went_sense(&w, 6, inside, true);
    hta_went_step(&w, 1.0f / 60.0f);
    assert(w.teleport_count == 1 && w.teleports[0].actor == 6);
    /* Leaving (or dying) re-arms it. */
    hta_went_sense(&w, 5, inside, false);
    hta_went_sense(&w, 5, inside, true);
    hta_went_step(&w, 1.0f / 60.0f);
    assert(w.teleport_count == 1 && w.teleports[0].actor == 5);
    /* One teleport per actor per step, however many chains ask. */
    hta_went_send(&w, DEST, HTA_WIN_TELEPORT, 7);
    hta_went_send(&w, DEST, HTA_WIN_TELEPORT, 7);
    hta_went_step(&w, 1.0f / 60.0f);
    assert(w.teleport_count == 1);
    /* Non-finite positions never count as inside. */
    const float bad[3] = { NAN, 0, 0 };
    hta_went_sense(&w, 9, bad, true);
    hta_went_step(&w, 1.0f / 60.0f);
    assert(w.teleport_count == 0);
    hta_went_free(&w);
    printf("  trigger -> teleport: ok\n");
}

static void test_handles(void)
{
    static hta_world_defs d;
    static hta_world_entities w;
    x1_fixture(&d);
    assert(hta_went_load(&w, &d, NULL, 0));
    hta_went_handle h = hta_went_handle_of(&w, DOOR);
    assert(h && hta_went_resolve(&w, h) == DOOR);
    assert(hta_went_resolve(&w, 0) < 0);
    assert(hta_went_resolve(&w, h + 100) < 0);                 /* no such slot */
    assert(hta_went_resolve(&w, (h & 0xFFFFu) | (7u << 16)) < 0);
    /* Queued before a reset, dispatched after: stale, dropped. */
    const float eye[3] = { -0.6f, -1.3f, 0.62f }, fwd[3] = { 1, 0, 0 };
    assert(hta_went_interact(&w, 1, eye, fwd) == BUTTON);
    hta_world_entities saved = w;
    hta_went_reset(&w);
    assert(hta_went_resolve(&w, h) < 0 && hta_went_resolve(&w, hta_went_handle_of(&w, DOOR)) == DOOR);
    /* Put the old queue back: its handle names the previous generation. */
    w.queue[0] = saved.queue[saved.head]; w.head = 0; w.count = 1;
    hta_went_step(&w, 1.0f / 60.0f);
    assert(w.stats.dropped_stale == 1 && w.stats.dispatched == 0 && w.diag_count == 1);
    assert(strstr(w.diag, "stale target") && strstr(w.diag, "x1:entity/button_main"));
    hta_went_step(&w, 1.0f);
    assert(w.st[DOOR].phase == HTA_MOVER_CLOSED);
    hta_went_free(&w);
    printf("  stale handles: ok\n");
}

static void test_bounds(void)
{
    static hta_world_defs d;
    static hta_world_entities w;
    /* A corrupt graph the loader would refuse: two relays feeding each
     * other, planted after the check. It must end, and be reported. */
    x1_fixture(&d);
    assert(hta_went_load(&w, &d, NULL, 0));
    d.link[1].target = RELAY; d.link[1].input = HTA_WIN_ACTIVATE;   /* relay -> itself */
    w.link_target[1] = hta_went_handle_of(&w, RELAY);
    assert(hta_went_send(&w, RELAY, HTA_WIN_ACTIVATE, 1));
    for (int i = 0; i < 40; i++) hta_went_step(&w, 1.0f / 60.0f);
    assert(w.count == 0 && w.stats.dropped_depth == 1);
    assert(w.stats.dispatched == HTA_WDEF_MAX_CHAIN + 1);
    assert(strstr(w.diag, "chain too long"));
    hta_went_free(&w);
    /* Fan-out that fills the queue: bounded per step, the rest waits, and
     * what does not fit is counted, not lost silently. */
    x1_fixture(&d);
    assert(hta_went_load(&w, &d, NULL, 0));
    for (uint32_t i = 0; i < HTA_WENT_QUEUE + 10; i++) hta_went_send(&w, RELAY, HTA_WIN_ACTIVATE, 1);
    assert(w.count == HTA_WENT_QUEUE && w.stats.dropped_full == 10);
    hta_went_step(&w, 1.0f / 60.0f);
    assert(w.stats.dispatched == HTA_WENT_BUDGET && w.stats.deferred > 0);
    for (int i = 0; i < 10; i++) hta_went_step(&w, 1.0f / 60.0f);
    assert(w.count == 0 && w.st[DOOR].phase != HTA_MOVER_CLOSED);
    /* An input the target does not take (corrupt data): dropped, named. */
    d.link[1].input = HTA_WIN_TELEPORT;
    assert(hta_went_send(&w, RELAY, HTA_WIN_ACTIVATE, 1));
    hta_went_step(&w, 1.0f / 60.0f);
    hta_went_step(&w, 1.0f / 60.0f);
    assert(w.stats.dropped_input == 1 && strstr(w.diag, "x1:entity/door_main"));
    hta_went_free(&w);
    /* The loader refuses the corrupt forms outright. */
    x1_fixture(&d);
    d.link[1].target = BUTTON;
    char err[256];
    assert(!hta_went_load(&w, &d, err, sizeof(err)) && strstr(err, "does not accept"));
    x1_fixture(&d);
    d.link[1].target = 99;
    assert(!hta_went_load(&w, &d, err, sizeof(err)) && strstr(err, "out of range"));
    printf("  bounded queue and cycles: ok\n");
}

static void test_replication(void)
{
    static hta_world_defs d;
    static hta_world_entities host, client, late;
    x1_fixture(&d);
    assert(hta_went_load(&host, &d, NULL, 0) && hta_went_load(&client, &d, NULL, 0) && hta_went_load(&late, &d, NULL, 0));
    client.remote = late.remote = true;
    /* A client never runs the chain itself. */
    const float eye[3] = { -0.6f, -1.3f, 0.62f }, fwd[3] = { 1, 0, 0 }, inside[3] = { 3, 0, 0 };
    assert(hta_went_interact(&client, 1, eye, fwd) < 0 && client.count == 0);
    hta_went_sense(&client, 1, inside, true);
    assert(client.count == 0 && !hta_went_send(&client, DOOR, HTA_WIN_OPEN, 1));
    /* Host opens the door; the client follows its snapshots. */
    assert(hta_went_interact(&host, 1, eye, fwd) == BUTTON);
    hta_went_mover_state ms[8];
    for (int i = 0; i < 120; i++) {
        hta_went_step(&host, 1.0f / 60.0f);
        hta_went_step(&client, 1.0f / 60.0f);
        if (i % 3 == 0) {
            uint32_t n = hta_went_snapshot(&host, ms, 8);
            assert(n == 1 && ms[0].index == DOOR);
            for (uint32_t k = 0; k < n; k++) assert(hta_went_apply(&client, &ms[k], false));
        }
        assert(fabsf(client.st[DOOR].t - host.st[DOOR].t) < 0.1f);
    }
    assert(host.st[DOOR].phase == HTA_MOVER_OPEN && client.st[DOOR].phase == HTA_MOVER_OPEN && client.st[DOOR].t == 1.0f);
    /* A late joiner: one snapshot of the current state, no history. */
    uint32_t n = hta_went_snapshot(&host, ms, 8);
    assert(hta_went_apply(&late, &ms[0], true));
    hta_went_step(&late, 1.0f / 60.0f);
    float a[3], b[3];
    hta_went_offset(&host, DOOR, a); hta_went_offset(&late, DOOR, b);
    assert(n == 1 && late.st[DOOR].phase == HTA_MOVER_OPEN && fabsf(a[1] - b[1]) < 1e-4f);
    /* Nonsense from the wire changes nothing. */
    hta_went_mover_state bad = { RELAY, HTA_MOVER_OPEN, 100 };
    assert(!hta_went_apply(&late, &bad, true));
    bad.index = 200; assert(!hta_went_apply(&late, &bad, true));
    bad.index = DOOR; bad.phase = 9; assert(!hta_went_apply(&late, &bad, true));
    assert(late.st[DOOR].phase == HTA_MOVER_OPEN && late.st[DOOR].t == 1.0f);
    hta_went_free(&host); hta_went_free(&client); hta_went_free(&late);
    printf("  replication and late join: ok\n");
}

/* ---- X2: one reusable mover definition, three placed doors ---- */

/* What Open Asset Lab writes for x2_definition_lab's entities (schema 2),
 * less its trigger and teleport (X1 covers those): button_a -> relay_a ->
 * door_a, button_b -> relay_b -> door_b, and door_c, never linked. All
 * three doors name x2:mover/basic_slide_door. */
static const char *X2 =
    "{\"id\":\"x2:world/definition_lab\",\"source_provenance\":\"ours\",\"world_entities\":{\"entities\":["
    "{\"id\":\"x2:entity/button_a\",\"kind\":\"interactable\",\"links\":[{\"event\":\"used\",\"input\":\"activate\",\"target\":\"x2:entity/relay_a\"}],\"position\":[-0.14,-4.2,0.55],\"reach\":1.0},"
    "{\"id\":\"x2:entity/relay_a\",\"kind\":\"relay\",\"links\":[{\"event\":\"fired\",\"input\":\"open\",\"target\":\"x2:entity/door_a\"}]},"
    "{\"definition\":\"x2:mover/basic_slide_door\",\"id\":\"x2:entity/door_a\",\"kind\":\"mover\",\"links\":[],\"position\":[0.0,-3.0,0.55]},"
    "{\"id\":\"x2:entity/button_b\",\"kind\":\"interactable\",\"links\":[{\"event\":\"used\",\"input\":\"activate\",\"target\":\"x2:entity/relay_b\"}],\"position\":[-0.14,-1.3,0.55],\"reach\":1.0},"
    "{\"id\":\"x2:entity/relay_b\",\"kind\":\"relay\",\"links\":[{\"event\":\"fired\",\"input\":\"open\",\"target\":\"x2:entity/door_b\"}]},"
    "{\"definition\":\"x2:mover/basic_slide_door\",\"id\":\"x2:entity/door_b\",\"kind\":\"mover\",\"links\":[],\"position\":[0.0,0.0,0.55]},"
    "{\"definition\":\"x2:mover/basic_slide_door\",\"id\":\"x2:entity/door_c\",\"kind\":\"mover\",\"links\":[],\"position\":[0.0,3.0,0.55]}"
    "],\"mover_definitions\":[{\"id\":\"x2:mover/basic_slide_door\",\"move\":[0.0,1.25,0.0],\"size\":[0.1,1.2,1.1],\"speed\":1.0}],"
    "\"schema\":2}}";

enum { BUTTON_A, RELAY_A, DOOR_A, BUTTON_B, RELAY_B, DOOR_B, DOOR_C };

static const char *x2_patch(const char *from, const char *to)
{
    static char buf[8192];
    const char *at = strstr(X2, from);
    assert(at);
    snprintf(buf, sizeof(buf), "%.*s%s%s", (int)(at - X2), X2, to, at + strlen(from));
    return buf;
}

static void x2_load(hta_world_defs *d)
{
    char err[256];
    if (!hta_world_defs_parse((const uint8_t *)X2, strlen(X2), d, err, sizeof(err))) { fprintf(stderr, "%s\n", err); assert(0); }
}

/* A ray across doorway `y` (x from -1 to +1 at knee height): blocked? */
static bool doorway_blocked(hta_world_entities *w, hta_collision *c, float y)
{
    static hta_collision_instance inst[8];
    c->instances = inst;
    c->instance_count = hta_went_instances(w, inst, 8);
    const float from[3] = { -1.0f, y, 0.5f }, dir[3] = { 1, 0, 0 };
    float t, hit[3], nrm[3];
    return hta_collision_ray(c, from, dir, 2.0f, &t, hit, nrm) && fabsf(hit[0] + 0.05f) < 0.01f;
}

static void test_x2_parse(void)
{
    static hta_world_defs d;
    char err[256];
    x2_load(&d);
    /* One definition, three placements naming it: resolved to an index once. */
    assert(d.schema == 2 && d.count == 7 && d.mover_def_count == 1);
    assert(!strcmp(d.mover_def[0].id, "x2:mover/basic_slide_door") && d.mover_def[0].speed == 1.0f);
    assert(hta_world_defs_find_mover(&d, "x2:mover/basic_slide_door") == 0 && hta_world_defs_find_mover(&d, "x2:entity/door_a") < 0);
    assert(hta_world_defs_find(&d, "x2:mover/basic_slide_door") < 0);
    for (int i = DOOR_A; i <= DOOR_C; i++) if (d.entity[i].kind == HTA_WDEF_MOVER) assert(d.entity[i].def == 0);
    assert(hta_wdef_mover(&d, DOOR_A) == hta_wdef_mover(&d, DOOR_B) && hta_wdef_mover(&d, DOOR_B) == hta_wdef_mover(&d, DOOR_C));
    assert(d.entity[RELAY_A].def == HTA_WDEF_NO_DEF && d.link[1].target == DOOR_A && d.link[3].target == DOOR_B);
    float lo[3], hi[3];
    hta_wdef_mover_box(&d, DOOR_B, lo, hi);
    assert(fabsf(lo[0] + 0.05f) < 1e-6f && fabsf(lo[1] + 0.6f) < 1e-6f && fabsf(hi[1] - 0.6f) < 1e-6f && fabsf(hi[2] - 1.1f) < 1e-6f);
    /* The list may come before the placements too (resolution waits for both). */
    {
        static char re[8192];
        const char *ents = strstr(X2, "\"entities\":["), *defs = strstr(X2, "\"mover_definitions\":["), *sch = strstr(X2, ",\"schema\"");
        snprintf(re, sizeof(re), "%.*s%.*s,%.*s%s", (int)(ents - X2), X2, (int)(sch - defs), defs, (int)(defs - ents - 1), ents, sch);
        static hta_world_defs r;
        assert(hta_world_defs_parse((const uint8_t *)re, strlen(re), &r, err, sizeof(err)) || (fprintf(stderr, "%s\n", err), 0));
        assert(r.mover_def_count == 1 && r.entity[DOOR_C].def == 0);
    }
    /* Bad references fail, naming the placement, the reference and why. */
    struct { const char *from, *to, *want; } bad[] = {
        { "\"definition\":\"x2:mover/basic_slide_door\",\"id\":\"x2:entity/door_b\"", "\"definition\":\"x2:mover/basic_slide_dor\",\"id\":\"x2:entity/door_b\"",
          "x2:entity/door_b references missing mover definition x2:mover/basic_slide_dor" },
        { "\"definition\":\"x2:mover/basic_slide_door\",\"id\":\"x2:entity/door_a\"", "\"definition\":\"x2:entity/relay_a\",\"id\":\"x2:entity/door_a\"",
          "x2:entity/door_a: definition x2:entity/relay_a is a placed entity, expected a mover definition" },
        { "\"definition\":\"x2:mover/basic_slide_door\",\"id\":\"x2:entity/door_a\"", "\"definition\":\"x2:weapon/basic_slide_door\",\"id\":\"x2:entity/door_a\"",
          "x2:entity/door_a: definition 'x2:weapon/basic_slide_door' is not a mover definition ID" },
        { "\"target\":\"x2:entity/door_a\"", "\"target\":\"x2:mover/basic_slide_door\"",
          "x2:entity/relay_a: link target x2:mover/basic_slide_door is a mover definition, expected a placed entity" },
        { "[{\"id\":\"x2:mover/basic_slide_door\"", "[{\"id\":\"x2:mover/basic_slide_door\",\"move\":[1,0,0],\"size\":[1,1,1],\"speed\":1},{\"id\":\"x2:mover/basic_slide_door\"",
          "x2:mover/basic_slide_door: duplicate mover definition ID" },
        { "[{\"id\":\"x2:mover/basic_slide_door\"", "[{\"id\":\"x2:mover/Basic-Door\",\"move\":[1,0,0],\"size\":[1,1,1],\"speed\":1},{\"id\":\"x2:mover/basic_slide_door\"",
          "'x2:mover/Basic-Door': malformed mover definition ID" },
        { "[{\"id\":\"x2:mover/basic_slide_door\"", "[{\"id\":\"x2:entity/basic_slide_door\"", "malformed mover definition ID" },
        { "[{\"id\":\"x2:mover/basic_slide_door\"", "[{\"id\":\"zz:mover/other\",\"move\":[1,0,0],\"size\":[1,1,1],\"speed\":1},{\"id\":\"x2:mover/basic_slide_door\"",
          "zz:mover/other: not in the world's namespace" },
        { "\"definition\":\"x2:mover/basic_slide_door\",\"id\":\"x2:entity/door_c\"", "\"id\":\"x2:entity/door_c\"", "x2:entity/door_c: mover has no definition" },
        { "\"id\":\"x2:entity/relay_b\",", "\"definition\":\"x2:mover/basic_slide_door\",\"id\":\"x2:entity/relay_b\",",
          "x2:entity/relay_b: only a mover takes a definition (it is a relay)" },
        { "\"links\":[],\"position\":[0.0,0.0,0.55]", "\"links\":[],\"position\":[0.0,0.0,0.55],\"speed\":3",
          "x2:entity/door_b: a mover takes its size, move and speed from its definition (schema 2)" },
        { "\"speed\":1.0}]", "\"speed\":0}]", "x2:mover/basic_slide_door: mover speed out of range" },
        { "\"size\":[0.1,1.2,1.1]", "\"size\":[0.1,0,1.1]", "x2:mover/basic_slide_door: mover size must be finite" },
        { "\"move\":[0.0,1.25,0.0],", "", "x2:mover/basic_slide_door: needs size, move and speed" },
        { "\"speed\":1.0}]", "\"speed\":1.0,\"colour\":1}]", "x2:mover/basic_slide_door: unknown field 'colour'" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        static hta_world_defs t;
        const char *json = x2_patch(bad[i].from, bad[i].to);
        bool ok = hta_world_defs_parse((const uint8_t *)json, strlen(json), &t, err, sizeof(err));
        if (ok || !strstr(err, bad[i].want)) {
            fprintf(stderr, "x2 case %zu: wanted '%s', got %s '%s'\n", i, bad[i].want, ok ? "success" : "failure", err);
            assert(0);
        }
    }
    /* The runtime's own check refuses a corrupted reference too. */
    x2_load(&d);
    d.entity[DOOR_B].def = 5;
    static hta_world_entities w;
    assert(!hta_went_load(&w, &d, err, sizeof(err)) && strstr(err, "x2:entity/door_b: mover has no definition"));
    x2_load(&d);
    d.entity[RELAY_A].def = 0;
    assert(!hta_went_load(&w, &d, err, sizeof(err)) && strstr(err, "x2:entity/relay_a: only a mover takes a definition"));
    for (size_t n = 0; n < strlen(X2); n++) {
        static hta_world_defs t;
        assert(!hta_world_defs_parse((const uint8_t *)X2, n, &t, err, sizeof(err)));
    }
    printf("  x2 definitions parse, resolve, refuse: ok\n");
}

static void test_x2_independent(void)
{
    static hta_world_defs d, before;
    static hta_world_entities w;
    char err[256];
    x2_load(&d);
    memcpy(&before, &d, sizeof(d));
    assert(hta_went_load(&w, &d, err, sizeof(err)) || (fprintf(stderr, "%s\n", err), 0));
    /* One grid for the definition; each door its own instance of it. */
    assert(w.inst[DOOR_A].grid == &w.mover_coll[0] && w.inst[DOOR_B].grid == &w.mover_coll[0] && w.inst[DOOR_C].grid == &w.mover_coll[0]);
    assert(w.inst[DOOR_A].pos[1] == -3.0f && w.inst[DOOR_B].pos[1] == 0.0f && w.inst[DOOR_C].pos[1] == 3.0f);
    static hta_world_defs scribbled;
    memcpy(&scribbled, &d, sizeof(d));
    hta_collision c; hta_bsp_mesh m;
    floor_grid(&c, &m);
    assert(doorway_blocked(&w, &c, -3.0f) && doorway_blocked(&w, &c, 0.0f));
    /* Button A: door A opens; B and C stay shut. */
    const float eye_a[3] = { -0.6f, -4.2f, 0.62f }, eye_b[3] = { -0.6f, -1.3f, 0.62f }, fwd[3] = { 1, 0, 0 };
    assert(hta_went_interact(&w, 1, eye_a, fwd) == BUTTON_A);
    run(&w, 0.3f);
    assert(w.st[DOOR_A].phase == HTA_MOVER_OPENING && w.st[DOOR_A].t > 0.1f);
    assert(w.st[DOOR_B].phase == HTA_MOVER_CLOSED && w.st[DOOR_B].t == 0.0f);
    assert(w.st[DOOR_C].phase == HTA_MOVER_CLOSED && w.st[DOOR_C].t == 0.0f);
    run(&w, 1.5f);
    float off[3];
    hta_went_offset(&w, DOOR_A, off); assert(fabsf(off[1] - 1.25f) < 1e-6f);
    hta_went_offset(&w, DOOR_B, off); assert(off[0] == 0 && off[1] == 0 && off[2] == 0);
    assert(!doorway_blocked(&w, &c, -3.0f) && doorway_blocked(&w, &c, 0.0f));
    /* Then B, on its own button: both open, C still shut. */
    assert(hta_went_interact(&w, 2, eye_b, fwd) == BUTTON_B);
    run(&w, 1.5f);
    assert(w.st[DOOR_A].phase == HTA_MOVER_OPEN && w.st[DOOR_B].phase == HTA_MOVER_OPEN && w.st[DOOR_C].phase == HTA_MOVER_CLOSED);
    assert(!doorway_blocked(&w, &c, -3.0f) && !doorway_blocked(&w, &c, 0.0f));
    /* Closing A leaves B open. */
    assert(hta_went_send(&w, DOOR_A, HTA_WIN_CLOSE, 1));
    run(&w, 1.5f);
    assert(w.st[DOOR_A].phase == HTA_MOVER_CLOSED && w.st[DOOR_B].phase == HTA_MOVER_OPEN && doorway_blocked(&w, &c, -3.0f));
    /* A new round: all shut, handles moved on. */
    hta_went_handle ha = hta_went_handle_of(&w, DOOR_B);
    hta_went_reset(&w);
    hta_went_step(&w, 0.0f);
    for (int i = DOOR_A; i <= DOOR_C; i++) if (d.entity[i].kind == HTA_WDEF_MOVER) assert(w.st[i].phase == HTA_MOVER_CLOSED && w.st[i].t == 0.0f);
    assert(hta_went_resolve(&w, ha) < 0 && doorway_blocked(&w, &c, 0.0f));
    /* The definitions were never written: byte for byte what was loaded. */
    assert(!memcmp(&before, &d, sizeof(d)));
    hta_went_free(&w);
    /* Authored names are load-time only: after loading, scribble every one
     * of them and the world still runs (nothing during play looks anything
     * up by name). */
    assert(hta_went_load(&w, &scribbled, err, sizeof(err)) || (fprintf(stderr, "%s\n", err), 0));
    for (uint32_t i = 0; i < scribbled.count; i++) memset(scribbled.entity[i].id, 'z', 20);
    for (uint32_t i = 0; i < scribbled.mover_def_count; i++) memset(scribbled.mover_def[i].id, 'z', 20);
    assert(hta_went_interact(&w, 1, eye_b, fwd) == BUTTON_B);
    run(&w, 1.5f);
    assert(w.st[DOOR_B].phase == HTA_MOVER_OPEN && w.st[DOOR_A].phase == HTA_MOVER_CLOSED);
    hta_went_free(&w);
    /* Bounded as before: a flood of activations on relay A. */
    assert(hta_went_load(&w, &d, NULL, 0));
    for (uint32_t i = 0; i < HTA_WENT_QUEUE + 3; i++) hta_went_send(&w, RELAY_A, HTA_WIN_ACTIVATE, 1);
    assert(w.count == HTA_WENT_QUEUE && w.stats.dropped_full == 3);
    for (int i = 0; i < 8; i++) hta_went_step(&w, 1.0f / 60.0f);
    assert(w.count == 0 && w.st[DOOR_A].phase != HTA_MOVER_CLOSED && w.st[DOOR_B].phase == HTA_MOVER_CLOSED);
    hta_went_free(&w);
    hta_collision_free(&c);
    printf("  x2 shared definition, independent doors: ok\n");
}

static void test_x2_late_join(void)
{
    static hta_world_defs d;
    static hta_world_entities host, a, late;
    x2_load(&d);
    assert(hta_went_load(&host, &d, NULL, 0) && hta_went_load(&a, &d, NULL, 0) && hta_went_load(&late, &d, NULL, 0));
    a.remote = late.remote = true;
    hta_went_mover_state ms[8];
    uint32_t n = hta_went_snapshot(&host, ms, 8);
    assert(n == 3 && ms[0].index == DOOR_A && ms[1].index == DOOR_B && ms[2].index == DOOR_C);
    for (uint32_t k = 0; k < n; k++) assert(hta_went_apply(&a, &ms[k], true));
    const float eye_a[3] = { -0.6f, -4.2f, 0.62f }, eye_b[3] = { -0.6f, -1.3f, 0.62f }, fwd[3] = { 1, 0, 0 };
    assert(hta_went_interact(&host, 1, eye_a, fwd) == BUTTON_A);
    for (int i = 0; i < 120; i++) {
        hta_went_step(&host, 1.0f / 60.0f); hta_went_step(&a, 1.0f / 60.0f);
        if (i % 3 == 0) { n = hta_went_snapshot(&host, ms, 8); for (uint32_t k = 0; k < n; k++) hta_went_apply(&a, &ms[k], false); }
    }
    assert(a.st[DOOR_A].phase == HTA_MOVER_OPEN && a.st[DOOR_B].phase == HTA_MOVER_CLOSED && a.st[DOOR_B].t == 0.0f);
    /* The late joiner: one snapshot, each door as it is. */
    n = hta_went_snapshot(&host, ms, 8);
    for (uint32_t k = 0; k < n; k++) assert(hta_went_apply(&late, &ms[k], true));
    assert(late.st[DOOR_A].phase == HTA_MOVER_OPEN && late.st[DOOR_A].t == 1.0f);
    assert(late.st[DOOR_B].phase == HTA_MOVER_CLOSED && late.st[DOOR_B].t == 0.0f && late.st[DOOR_C].phase == HTA_MOVER_CLOSED);
    /* Then B opens; everyone converges on A open, B open. */
    assert(hta_went_interact(&host, 2, eye_b, fwd) == BUTTON_B);
    for (int i = 0; i < 120; i++) {
        hta_went_step(&host, 1.0f / 60.0f); hta_went_step(&a, 1.0f / 60.0f); hta_went_step(&late, 1.0f / 60.0f);
        if (i % 3 == 0) {
            n = hta_went_snapshot(&host, ms, 8);
            for (uint32_t k = 0; k < n; k++) { hta_went_apply(&a, &ms[k], false); hta_went_apply(&late, &ms[k], false); }
        }
    }
    for (hta_world_entities *c = &a; c; c = c == &a ? &late : NULL)
        assert(c->st[DOOR_A].phase == HTA_MOVER_OPEN && c->st[DOOR_B].phase == HTA_MOVER_OPEN && c->st[DOOR_C].phase == HTA_MOVER_CLOSED);
    hta_went_free(&host); hta_went_free(&a); hta_went_free(&late);
    printf("  x2 late join sees each door as it is: ok\n");
}

static void test_visual_state(void)
{
    static const char *json =
        "{\"world_entities\":{\"schema\":7,\"entities\":["
        "{\"id\":\"x1:entity/relay_main\",\"kind\":\"relay\",\"links\":[]}],"
        "\"environment\":{\"ambient\":[0.2,0.3,0.4],\"clear\":[0,0,0],"
        "\"fog_color\":[0.1,0.2,0.3],\"fog_density\":0.05,\"fog_start\":2},"
        "\"lights\":[{\"id\":\"lamp\",\"type\":\"point\",\"position\":[0,0,1],"
        "\"color\":[1,0.5,0.2],\"intensity\":3,\"range\":4,"
        "\"relay\":\"x1:entity/relay_main\"}]}}";
    static hta_world_defs d;
    static hta_world_entities host, late;
    char err[256];
    assert(hta_world_defs_parse((const uint8_t *)json, strlen(json), &d, err, sizeof(err)) ||
           (fprintf(stderr, "%s\n", err), 0));
    assert(d.has_environment && d.light_count == 1 && d.light[0].relay == 1);
    /* The engine also rejects hand-edited packages, independently of OAL. */
    {
        char bad[2048], why[256];
        const char *from[] = {"\"range\":4", "\"fog_density\":0.05", "\"relay\":\"x1:entity/relay_main\""};
        const char *to[] = {"\"range\":-1", "\"fog_density\":1e999", "\"relay\":\"x1:entity/missing\""};
        for (uint32_t i = 0; i < 3; i++) {
            const char *at = strstr(json, from[i]);
            assert(at);
            snprintf(bad, sizeof(bad), "%.*s%s%s", (int)(at - json), json, to[i], at + strlen(from[i]));
            assert(!hta_world_defs_parse((const uint8_t *)bad, strlen(bad), &d, why, sizeof(why)));
        }
        assert(hta_world_defs_parse((const uint8_t *)json, strlen(json), &d, why, sizeof(why)));
        /* Mutate schema-7 light/environment bytes. A refusal or a valid
         * parse is fine; identical input must give the same verdict. */
        uint32_t seed = 0x9137u;
        for (uint32_t i = 0; i < 1000; i++) {
            snprintf(bad, sizeof(bad), "%s", json);
            seed = seed * 1664525u + 1013904223u;
            size_t at = (size_t)(seed % strlen(bad));
            bad[at] = (char)(32 + (seed >> 16) % 95);
            bool a = hta_world_defs_parse((const uint8_t *)bad, strlen(bad), &d, why, sizeof(why));
            char why2[256];
            bool b = hta_world_defs_parse((const uint8_t *)bad, strlen(bad), &d, why2, sizeof(why2));
            assert(a == b && (a || !strcmp(why, why2)));
        }
        assert(hta_world_defs_parse((const uint8_t *)json, strlen(json), &d, why, sizeof(why)));
    }
    assert(hta_went_load(&host, &d, NULL, 0) && hta_went_load(&late, &d, NULL, 0));
    hta_scene scene = {0};
    const float eye[3] = {0, 0, 0};
    hta_scene_apply_visual(&scene, &d, &host, eye);
    assert(scene.light_count == 0 && scene.fog_density == 0.05f);
    assert(hta_went_request(&host, HTA_WACT_ACTIVATE, 0, HTA_WENT_NO_ACTOR) == HTA_WENT_QUEUED);
    hta_went_step(&host, 1.0f / 60.0f);
    hta_scene_apply_visual(&scene, &d, &host, eye);
    assert(scene.light_count == 1);
    uint8_t bits[1] = {0};
    assert(hta_went_flags(&host, bits, 1) == 1);
    assert(hta_went_apply_flag(&late, 0, (bits[0] & 1u) != 0));
    hta_scene_apply_visual(&scene, &d, &late, eye);
    assert(scene.light_count == 1);
    hta_went_free(&host); hta_went_free(&late);
    static hta_world_defs many;
    memset(&many, 0, sizeof(many));
    many.has_environment = true;
    many.light_count = HTA_WDEF_MAX_LIGHTS;
    for (uint32_t i = 0; i < many.light_count; i++) {
        many.light[i].position[0] = (float)i;
        many.light[i].range = 64;
        many.light[i].intensity = 1;
    }
    hta_scene_apply_visual(&scene, &many, NULL, eye);
    assert(scene.light_count == HTA_SCENE_MAX_LIGHTS);
    for (uint32_t i = 0; i < scene.light_count; i++)
        assert(scene.lights[i].position[0] == (float)i);
    const float forward[3] = {1, 0, 0};
    hta_scene_add_flashlight(&scene, eye, forward);
    assert(scene.light_count == HTA_SCENE_MAX_LIGHTS && scene.lights[7].outer_cos > 0.0f &&
           scene.lights[7].direction[0] == 1.0f && scene.lights[6].position[0] == 6.0f);
}

int main(void)
{
    test_visual_state();
    test_parse();
    test_button_relay_door();
    test_trigger_teleport();
    test_handles();
    test_bounds();
    test_replication();
    test_x2_parse();
    test_x2_independent();
    test_x2_late_join();
    printf("world entities: all ok\n");
    return 0;
}
