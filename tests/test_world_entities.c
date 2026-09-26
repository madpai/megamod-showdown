/* World entities (X1): definitions parsed and checked, and the runtime's
 * handles, bounded queue, relay, mover collision, trigger and teleport --
 * on a synthetic world built in C (the runtime-side fixture; the authored
 * X1 world comes from Open Asset Lab: docs/WORLD_ENTITIES.md). */
#include "asset/world_def.h"
#include "engine/world_entities.h"
#include "engine/player.h"
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
    dd->min[0] = -0.05f; dd->min[1] = -0.6f; dd->min[2] = 0.0f;
    dd->max[0] = 0.05f; dd->max[1] = 0.6f; dd->max[2] = 1.1f;
    dd->move[1] = 1.25f; dd->speed = 1.0f;
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
    assert(fabsf(d.entity[2].move[1] - 1.25f) < 1e-6f && d.entity[2].speed == 1.0f);
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
                "x1:entity/button_main references missing target x1:entity/relay_mian");
    expect_fail(patch("\"id\":\"x1:entity/door_main\"", "\"id\":\"x1:entity/Door-Main\""), "'x1:entity/Door-Main': malformed placed ID");
    expect_fail(patch("\"id\":\"x1:entity/relay_main\",", "\"id\":\"x1:weapon/relay_main\","), "malformed placed ID");
    expect_fail(patch("{\"bounds\":{\"max\":[0.05", "{\"id\":\"other:entity/relay_z\",\"kind\":\"relay\"},{\"bounds\":{\"max\":[0.05"),
                "not in the world's namespace");
    expect_fail(patch("\"input\":\"open\"", "\"input\":\"explode\""), "unknown input 'explode'");
    expect_fail(patch("\"input\":\"open\"", "\"input\":\"teleport\""), "target does not accept x1:entity/door_main.teleport");
    expect_fail(patch("\"event\":\"fired\"", "\"event\":\"used\""), "does not emit 'used'");
    expect_fail(patch("\"kind\":\"relay\"", "\"kind\":\"logic_relay\""), "unknown kind 'logic_relay'");
    expect_fail(patch("\"schema\":1", "\"schema\":2"), "unsupported schema");
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

int main(void)
{
    test_parse();
    test_button_relay_door();
    test_trigger_teleport();
    test_handles();
    test_bounds();
    test_replication();
    printf("world entities: all ok\n");
    return 0;
}
