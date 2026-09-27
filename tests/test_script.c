/* Host-side Lua scripting (X3, docs/SCRIPTING.md): package parsing,
 * sandbox, budgets, handles, and the two proofs at unit level -- a
 * scripted button opening a door through the world-event queue, and a
 * scripted ability hurting through the native damage pipeline. The worlds
 * here are written as Open Asset Lab writes them (schema 3); the authored
 * x3_script_lab comes from OAL (scripts/test_x3.sh). */
#include "asset/world_def.h"
#include "engine/world_entities.h"
#include "game/game.h"
#include "script/script.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- a world with scripts, as JSON ---- */

enum { BUTTON_S, DOOR_A, DOOR_B };

static char log_lines[64][320];
static int log_n;
static void on_log(void *ctx, const char *line)
{
    (void)ctx;
    snprintf(log_lines[log_n % 64], sizeof(log_lines[0]), "%s", line);
    log_n++;
    if (getenv("SCRIPT_TEST_VERBOSE")) printf("    %s\n", line);
}
static bool logged(const char *what)
{
    for (int i = 0; i < log_n && i < 64; i++) if (strstr(log_lines[i], what)) return true;
    return false;
}

/* JSON-escape Lua source into a string literal's body. */
static void esc(char *out, size_t cap, const char *src)
{
    size_t n = 0;
    for (; *src && n + 3 < cap; src++) {
        if (*src == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
        else if (*src == '"' || *src == '\\') { out[n++] = '\\'; out[n++] = *src; }
        else out[n++] = *src;
    }
    out[n] = 0;
}

/* A schema 3 world: a scripted button, two doors of one definition; the
 * button's script is `button_src` (declares `button_cbs`), and the ability
 * script `ability_src` when not NULL. `extra` goes into the scripts list. */
static const char *world_json(const char *button_src, const char *button_cbs, const char *ability_src, const char *extra)
{
    static char json[32768], a[8192], b[8192];
    esc(a, sizeof(a), button_src);
    esc(b, sizeof(b), ability_src ? ability_src : "");
    char ability[9000] = "";
    if (ability_src)
        snprintf(ability, sizeof(ability), ",{\"api\":\"megamod.v1\",\"callbacks\":[\"on_ability\"],\"id\":\"x3:script/pulse_ability\",\"source\":\"%s\"}", b);
    snprintf(json, sizeof(json),
        "{\"world_entities\":{%s\"entities\":["
        "{\"id\":\"x3:entity/button_script\",\"kind\":\"interactable\",\"links\":[],\"position\":[-0.14,-4.2,0.55],\"reach\":1.0,\"script\":\"x3:script/button_logic\"},"
        "{\"definition\":\"x3:mover/slide_door\",\"id\":\"x3:entity/door_a\",\"kind\":\"mover\",\"links\":[],\"position\":[0.0,-3.0,0.55]},"
        "{\"definition\":\"x3:mover/slide_door\",\"id\":\"x3:entity/door_b\",\"kind\":\"mover\",\"links\":[],\"position\":[0.0,0.0,0.55]}"
        "],\"mover_definitions\":[{\"id\":\"x3:mover/slide_door\",\"move\":[0.0,1.25,0.0],\"size\":[0.1,1.2,1.1],\"speed\":1.0}],"
        "\"schema\":3,\"scripts\":[{\"api\":\"megamod.v1\",\"callbacks\":[%s],\"id\":\"x3:script/button_logic\",\"source\":\"%s\"}%s%s]}}",
        ability_src ? "\"ability_script\":\"x3:script/pulse_ability\"," : "", button_cbs, a, ability, extra ? extra : "");
    return json;
}

static bool parse(const char *json, hta_world_defs *d, char *err, size_t n)
{
    return hta_world_defs_parse((const uint8_t *)json, strlen(json), d, err, n);
}

static const char *BUTTON =
    "local door = world.entity('x3:entity/door_b')\n"
    "presses = 0\n"
    "function on_used(entity, player)\n"
    "  presses = presses + 1\n"
    "  if world.state(door) == 'closed' then world.send(door, 'open', player) end\n"
    "  log('pressed', presses, tostring(entity), tostring(player))\n"
    "end\n";

static const char *PULSE =
    "function on_ability(player)\n"
    "  for _, t in ipairs(game.near(player, 3.0)) do game.damage(t, 40, player) end\n"
    "end\n";

/* ---- a small game: three named players, alive, placed ---- */

static void game_setup(hta_game *g)
{
    memset(g, 0, sizeof(*g));
    const char *names[] = { "A", "B", "Far" };
    float pos[3][2] = { { -1.0f, -4.2f }, { -0.5f, -4.0f }, { -4.0f, 3.0f } };
    for (int i = 0; i < 3; i++) {
        int32_t u = hta_game_add(g, HTA_UNIT_BOT, names[i], HTA_TEAM_AUTO);
        assert(u == i);
        hta_unit *un = &g->units[u];
        un->alive = true;
        un->vitals.health = un->vitals.max_health = 1000.0f;
        un->vitals.shield = un->vitals.max_shield = 0.0f;
        un->body.pos[0] = pos[i][0]; un->body.pos[1] = pos[i][1];
    }
}

static hta_world_defs defs, before;
static hta_world_entities went;
static hta_game game;

static hta_script_host *load(const char *json, char *err, size_t n)
{
    char e[256];
    if (!parse(json, &defs, e, sizeof(e))) { fprintf(stderr, "parse: %s\n", e); assert(0); }
    assert(hta_went_load(&went, &defs, e, sizeof(e)));
    game_setup(&game);
    return hta_script_create(&defs, &went, &game, on_log, NULL, err, n);
}

static void press(void)
{
    const float eye[3] = { -0.6f, -4.2f, 0.62f }, fwd[3] = { 1, 0, 0 };
    assert(hta_went_interact(&went, 0, eye, fwd) == BUTTON_S);
}

/* ---- package: parse and load failures ---- */

static void expect_parse_fail(const char *json, const char *want)
{
    static hta_world_defs d;
    char err[256];
    bool ok = parse(json, &d, err, sizeof(err));
    if (ok || !strstr(err, want)) { fprintf(stderr, "wanted '%s', got %s '%s'\n", want, ok ? "success" : "failure", err); assert(0); }
}

static void test_package(void)
{
    static hta_world_defs d;
    char err[256];
    const char *ok = world_json(BUTTON, "\"on_used\"", PULSE, NULL);
    assert(parse(ok, &d, err, sizeof(err)) || (fprintf(stderr, "%s\n", err), 0));
    assert(d.schema == 3 && d.script_count == 2 && d.entity[BUTTON_S].script == 1 && d.ability_script == 2);
    assert(d.script[0].callbacks == HTA_WCB_ON_USED && d.script[1].callbacks == HTA_WCB_ON_ABILITY);
    assert(d.script[0].len == strlen(BUTTON) && !memcmp(d.pool + d.script[0].at, BUTTON, d.script[0].len));
    /* The button has no link: only its script can open anything. */
    assert(d.entity[BUTTON_S].link_count == 0);
    char buf[32768];
    snprintf(buf, sizeof(buf), "%s", ok);
    #define PATCH(from, to, want) do { static char p[32768]; const char *at = strstr(buf, from); assert(at); \
        snprintf(p, sizeof(p), "%.*s%s%s", (int)(at - buf), buf, to, at + strlen(from)); expect_parse_fail(p, want); } while (0)
    PATCH("\"script\":\"x3:script/button_logic\"", "\"script\":\"x3:script/button_logik\"",
          "x3:entity/button_script references missing script x3:script/button_logik");
    PATCH("\"script\":\"x3:script/button_logic\"", "\"script\":\"x3:entity/door_a\"",
          "x3:entity/button_script: script x3:entity/door_a is a placed entity, expected a script");
    PATCH("\"script\":\"x3:script/button_logic\"", "\"script\":\"x3:weapon/button_logic\"",
          "x3:entity/button_script: script 'x3:weapon/button_logic' is not a script ID");
    PATCH("\"id\":\"x3:script/pulse_ability\"", "\"id\":\"x3:script/button_logic\"", "x3:script/button_logic: duplicate script ID");
    PATCH("\"id\":\"x3:script/pulse_ability\"", "\"id\":\"x3:script/Pulse\"", "'x3:script/Pulse': malformed script ID");
    PATCH("\"callbacks\":[\"on_ability\"]", "\"callbacks\":[\"on_tick\"]", "unknown callback 'on_tick'");
    PATCH("\"callbacks\":[\"on_ability\"]", "\"callbacks\":[\"on_used\"]", "ability_script: script x3:script/pulse_ability does not declare on_ability");
    PATCH("\"api\":\"megamod.v1\",\"callbacks\":[\"on_used\"]", "\"api\":\"megamod.v2\",\"callbacks\":[\"on_used\"]",
          "x3:script/button_logic: unsupported script API 'megamod.v2' (this engine has megamod.v1)");
    PATCH("\"schema\":3", "\"schema\":2", "scripts need schema 3");
    PATCH("\"kind\":\"interactable\",\"links\":[]", "\"kind\":\"relay\",\"links\":[]", "only an interactable takes a script");
    PATCH("\"source\":\"local", "\"source\":\"\\u001bLua", "source is not ASCII text");
    PATCH("\"source\":\"local", "\"source\":\"\\u00e9 local", "source is not ASCII text");
    PATCH("\"id\":\"x3:script/pulse_ability\"", "\"id\":\"zz:script/pulse_ability\"", "zz:script/pulse_ability: not in the world's namespace");
    PATCH("\"links\":[],\"position\":[-0.14", "\"links\":[{\"event\":\"used\",\"input\":\"open\",\"target\":\"x3:script/button_logic\"}],\"position\":[-0.14",
          "link target x3:script/button_logic is a script, expected a placed entity");
    /* Every truncation fails cleanly. */
    for (size_t n = 0; n < strlen(ok); n += 7) assert(!hta_world_defs_parse((const uint8_t *)ok, n, &d, err, sizeof(err)));
    /* Load-time failures: syntax, a declared callback it never defines, a
     * failing chunk, a load-time budget. Each names the script. */
    hta_script_host *h;
    h = load(world_json("function on_used(\n", "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(!h && strstr(err, "x3:script/button_logic: syntax error") && strstr(err, "x3:script/button_logic:"));
    h = load(world_json("function on_other() end\n", "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(!h && strstr(err, "x3:script/button_logic: declares on_used but defines no function on_used"));
    h = load(world_json("error('boom at load')\n", "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(!h && strstr(err, "boom at load"));
    h = load(world_json("while true do end\n", "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(!h && strstr(err, "exceeded instruction budget"));
    h = load(world_json("local d = world.entity('x3:entity/nope')\nfunction on_used() end\n", "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(!h && strstr(err, "unknown entity 'x3:entity/nope'"));
    /* world.entity is a typed reference too (X4): a script ID, a malformed
     * ID and a reserved type are each named for what they are. */
    h = load(world_json("local d = world.entity('x3:script/button_logic')\nfunction on_used() end\n", "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(!h && strstr(err, "world.entity: 'x3:script/button_logic' is a script ID, expected a placed entity"));
    h = load(world_json("local d = world.entity('X3:entity/door_a')\nfunction on_used() end\n", "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(!h && strstr(err, "world.entity: 'X3:entity/door_a' is not a resource ID: namespace has capital 'X'"));
    h = load(world_json("local d = world.entity('x3:model/door_a')\nfunction on_used() end\n", "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(!h && strstr(err, "resource type 'model' is reserved"));
    /* A world without scripts makes no Lua state at all. */
    static const char *plain = "{\"world_entities\":{\"entities\":[{\"id\":\"x1:entity/r\",\"kind\":\"relay\",\"links\":[]}],\"schema\":1}}";
    assert(parse(plain, &defs, err, sizeof(err)) && hta_went_load(&went, &defs, err, sizeof(err)));
    assert(!hta_script_create(&defs, &went, &game, on_log, NULL, err, sizeof(err)) && !err[0]);
    printf("  package: parse, references, refusals: ok\n");
}

/* ---- the sandbox ---- */

static const char *SANDBOX =
    "assert(os == nil and io == nil and package == nil and require == nil and debug == nil)\n"
    "assert(load == nil and loadstring == nil and dofile == nil and loadfile == nil)\n"
    "assert(collectgarbage == nil and pcall == nil and xpcall == nil and print == nil)\n"
    "assert(setmetatable == nil and getmetatable == nil and rawset == nil and rawget == nil)\n"
    "assert(coroutine == nil and utf8 == nil and _G == nil)\n"
    "assert(string.dump == nil and string.find == nil and string.gsub == nil and string.match == nil)\n"
    "assert(('x'):rep(3) == 'xxx' and ('x').find == nil and ('x').dump == nil)\n"
    "assert(math.random == nil and math.randomseed == nil and math.floor(2.5) == 2)\n"
    "assert(megamod.api == 'megamod.v1' and type(world.send) == 'function' and type(game.damage) == 'function')\n"
    "mine = 1\n"
    "function on_used(entity, player)\n"
    "  if mine == 1 then mine = 2; math.pi = 3 end\n"          /* read-only: an error */
    "end\n";

static const char *OTHER =
    "function on_ability(p)\n"
    "  assert(mine == nil, 'another script sees my globals')\n"
    "  assert(math.pi > 3.14, 'a shared library was changed')\n"
    "  log('other ok')\n"
    "end\n";

static void test_sandbox(void)
{
    char err[256];
    hta_script_host *h = load(world_json(SANDBOX, "\"on_used\"", OTHER, NULL), err, sizeof(err));
    assert(h || (fprintf(stderr, "%s\n", err), 0));
    press();
    hta_script_phase(h, 0.016f);
    assert(hta_script_get_stats(h)->errors == 1 && logged("engine and library tables are read-only"));
    assert(hta_script_ability(h, 1));
    hta_script_phase(h, 0.016f);
    assert(hta_script_get_stats(h)->errors == 1 && logged("x3:script/pulse_ability: other ok"));
    assert(hta_script_destroy(h) == 0);
    printf("  sandbox: no os/io/package/debug/load/pcall/metatables, read-only shared tables, isolated globals: ok\n");
}

/* ---- budgets and errors ---- */

static void test_budgets(void)
{
    char err[256];
    const char *loop =
        "calls = 0\n"
        "function on_used(e, p) calls = calls + 1; if calls <= 9 then while true do end end; log('recovered') end\n";
    hta_script_host *h = load(world_json(loop, "\"on_used\"", PULSE, NULL), err, sizeof(err));
    assert(h || (fprintf(stderr, "%s\n", err), 0));
    press();
    hta_script_phase(h, 0.016f);
    const hta_script_stats *st = hta_script_get_stats(h);
    assert(st->budget_aborts == 1 && logged("x3:script/button_logic on_used(x3:entity/button_script, unit 0): exceeded instruction budget"));
    /* The host goes on: the other script still runs, the world still steps. */
    assert(hta_script_ability(h, 0));
    hta_script_phase(h, 0.016f);
    assert(game.units[1].vitals.health < 1000.0f);
    hta_went_step(&went, 0.016f);
    /* Eight failures in a round: disabled until the next one. */
    for (int i = 0; i < 8; i++) { went.st[BUTTON_S].cooldown = 0; press(); hta_script_phase(h, 0.016f); }
    assert(st->budget_aborts == 8 && logged("x3:script/button_logic disabled for the rest of the round after 8 errors"));
    uint64_t cb = st->callbacks;
    went.st[BUTTON_S].cooldown = 0; press(); hta_script_phase(h, 0.016f);
    assert(st->callbacks == cb);                              /* disabled: not called */
    /* A new round: back, with fresh state (calls = 0 again, so it loops once more). */
    hta_went_reset(&went);
    assert(hta_script_reset(h));
    went.st[BUTTON_S].cooldown = 0; press(); hta_script_phase(h, 0.016f);
    assert(st->callbacks == cb + 1 && st->budget_aborts == 9);
    /* Too many requests in one callback: stopped at 16; they stay bounded
     * by the world queue as well. */
    hta_script_destroy(h);
    const char *flood =
        "local d = world.entity('x3:entity/door_a')\n"
        "function on_used(e, p) for i = 1, 100 do world.send(d, 'toggle') end end\n";
    h = load(world_json(flood, "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(h);
    press();
    hta_script_phase(h, 0.016f);
    assert(logged("world.send: more than 16 engine requests in one callback") && went.count == 16);
    hta_went_step(&went, 0.016f);
    assert(went.count == 0 && went.stats.dispatched == 16);
    hta_script_destroy(h);
    /* Memory: a runaway allocation fails the callback, not the host. */
    const char *hog = "t = {}\nfunction on_used() for i = 1, 1e7 do t[i] = ('x'):rep(64) .. i end end\n";
    h = load(world_json(hog, "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(h);
    press();
    hta_script_phase(h, 0.016f);
    st = hta_script_get_stats(h);
    assert(st->errors == 1 && st->memory_peak <= HTA_SCRIPT_MEMORY);
    assert(logged("out of memory") || logged("exceeded instruction budget"));
    assert(hta_script_destroy(h) == 0);
    /* Runtime errors abort that callback only. */
    h = load(world_json("function on_used() error('oops') end\n", "\"on_used\"", NULL, NULL), err, sizeof(err));
    press(); hta_script_phase(h, 0.016f);
    assert(logged("x3:script/button_logic on_used(x3:entity/button_script, unit 0): x3:script/button_logic:1: oops"));
    assert(hta_script_destroy(h) == 0);
    printf("  budgets: instructions, requests, memory, errors, disable and round re-enable: ok\n");
}

/* ---- handles ---- */

static void test_handles(void)
{
    char err[256];
    const char *src =
        "local door = world.entity('x3:entity/door_b')\n"
        "mode = 'send'\n"
        "function on_used(e, p)\n"
        "  keep = p\n"
        "  if mode == 'send' then world.send(door, 'open')\n"
        "  elseif mode == 'number' then world.send(5, 'open')\n"
        "  elseif mode == 'table' then world.send({}, 'open')\n"
        "  elseif mode == 'player' then world.send(p, 'open')\n"
        "  elseif mode == 'input' then world.send(door, 'explode_universe')\n"
        "  elseif mode == 'accept' then world.send(e, 'open')\n"
        "  elseif mode == 'write' then p.health = 0 end\n"
        "end\n";
    hta_script_host *h;
    struct { const char *mode, *want; } bad[] = {
        { "number", "argument 1: expected an entity handle, got number" },
        { "table", "argument 1: expected an entity handle, got table" },
        { "player", "argument 1: expected an entity handle, got userdata" },
        { "input", "requested invalid input 'explode_universe'" },
        { "accept", "x3:entity/button_script (interactable) does not accept 'open'" },
        { "write", "attempt to index a megamod.handle value" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char s2[2048];
        snprintf(s2, sizeof(s2), "%s", src);
        char *m = strstr(s2, "mode = 'send'");
        assert(m);
        char line[64]; snprintf(line, sizeof(line), "mode = '%s'", bad[i].mode);
        char s3[2048];
        snprintf(s3, sizeof(s3), "%.*s%s%s", (int)(m - s2), s2, line, m + strlen("mode = 'send'"));
        h = load(world_json(s3, "\"on_used\"", NULL, NULL), err, sizeof(err));
        assert(h);
        log_n = 0;
        press(); hta_script_phase(h, 0.016f);
        if (!logged(bad[i].want)) { fprintf(stderr, "wanted '%s' (%s): %s\n", bad[i].want, bad[i].mode, log_lines[0]); assert(0); }
        assert(went.count == 0);                         /* nothing reached the queue */
        hta_script_destroy(h);
    }
    /* Stale: the world resets under a script that kept its handle. */
    h = load(world_json(src, "\"on_used\"", NULL, NULL), err, sizeof(err));
    memcpy(&before, &defs, sizeof(defs));
    hta_went_reset(&went);                               /* generations move on; the script keeps `door` */
    log_n = 0;
    press(); hta_script_phase(h, 0.016f);
    assert(logged("stale entity handle x3:entity/door_b (from an earlier round?)") && went.count == 0);
    /* A player handle kept across the game's round restart is stale too. */
    const char *keep_src =
        "function on_used(e, p) if kept then game.damage(kept, 10) else kept = p end end\n";
    hta_script_destroy(h);
    h = load(world_json(keep_src, "\"on_used\"", NULL, NULL), err, sizeof(err));
    press(); hta_script_phase(h, 0.016f);                  /* kept = player 0 */
    hta_game_start(&game);                                 /* new round: every unit's generation moves on */
    for (int i = 0; i < 3; i++) { game.units[i].alive = true; game.units[i].protect = 0; game.units[i].vitals.health = 1000.0f; }
    went.st[BUTTON_S].cooldown = 0;
    log_n = 0;
    press(); hta_script_phase(h, 0.016f);
    assert(logged("stale player handle (slot 0: left, or an earlier round)") && game.units[0].vitals.health == 1000.0f);
    /* A player who left: the slot's handle is stale even before any reset. */
    hta_script_destroy(h);
    h = load(world_json(keep_src, "\"on_used\"", NULL, NULL), err, sizeof(err));
    memcpy(&before, &defs, sizeof(defs));
    press(); hta_script_phase(h, 0.016f);
    hta_game_remove(&game, 0);
    hta_game_add(&game, HTA_UNIT_BOT, "Newcomer", HTA_TEAM_AUTO);  /* takes slot 0 */
    game.units[0].alive = true; game.units[0].vitals.health = 1000.0f;
    hta_went_call c = { BUTTON_S, 1 };
    went.calls[0] = c; went.call_count = 1;
    log_n = 0;
    hta_script_phase(h, 0.016f);
    assert(logged("stale player handle") && game.units[0].vitals.health == 1000.0f);
    /* The definitions were never written. */
    assert(!memcmp(&before, &defs, sizeof(defs)));
    hta_script_destroy(h);
    printf("  handles: forged, wrong kind, invalid input, stale entity, stale player (round, slot reuse): ok\n");
}

/* ---- proof A: button -> Lua -> world.send -> queue -> door ---- */

static void test_world_proof(void)
{
    char err[256];
    hta_script_host *h = load(world_json(BUTTON, "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(h || (fprintf(stderr, "%s\n", err), 0));
    memcpy(&before, &defs, sizeof(defs));
    /* Without the script phase the press opens nothing (the button has no links). */
    press();
    assert(went.call_count == 1 && went.count == 0);
    for (int i = 0; i < 60; i++) hta_went_step(&went, 1.0f / 60.0f);
    assert(went.st[DOOR_B].phase == HTA_MOVER_CLOSED && went.st[DOOR_A].phase == HTA_MOVER_CLOSED);
    /* The phase: on_used runs, requests `open` on door B, the queue carries it. */
    hta_script_phase(h, 1.0f / 60.0f);
    assert(went.call_count == 0 && went.count == 1 && went.queue[went.head].input == HTA_WIN_OPEN);
    assert(went.st[DOOR_B].phase == HTA_MOVER_CLOSED);             /* nothing moved yet: a request */
    hta_went_step(&went, 1.0f / 60.0f);
    assert(went.stats.dispatched == 1 && went.st[DOOR_B].phase == HTA_MOVER_OPENING);
    for (int i = 0; i < 120; i++) hta_went_step(&went, 1.0f / 60.0f);
    assert(went.st[DOOR_B].phase == HTA_MOVER_OPEN && went.st[DOOR_A].phase == HTA_MOVER_CLOSED && went.st[DOOR_A].t == 0.0f);
    assert(logged("x3:script/button_logic on_used(x3:entity/button_script, unit 0) phase host.world.script tick 1: ok, 1 requests"));
    assert(logged("x3:script/button_logic: pressed 1 entity x3:entity/button_script player 0"));
    /* Replication is the world's own: a client applying the host's
     * snapshot sees B open, A shut, with no script of its own. */
    static hta_world_entities client;
    assert(hta_went_load(&client, &defs, err, sizeof(err)));
    client.remote = true;
    hta_went_mover_state ms[8];
    uint32_t n = hta_went_snapshot(&went, ms, 8);
    for (uint32_t k = 0; k < n; k++) assert(hta_went_apply(&client, &ms[k], true));
    assert(client.st[DOOR_B].phase == HTA_MOVER_OPEN && client.st[DOOR_A].phase == HTA_MOVER_CLOSED);
    /* Second press: the script sees B open and asks nothing. */
    went.st[BUTTON_S].cooldown = 0;
    press(); hta_script_phase(h, 1.0f / 60.0f);
    assert(went.count == 0);
    assert(!memcmp(&before, &defs, sizeof(defs)));
    hta_went_free(&client);
    assert(hta_script_destroy(h) == 0);
    printf("  proof A: button -> on_used -> world.send -> queue -> door B (A untouched), replicated: ok\n");
}

/* ---- proof B: ability -> Lua -> game.damage -> native damage ---- */

static void test_ability_proof(void)
{
    char err[256];
    hta_script_host *h = load(world_json(BUTTON, "\"on_used\"", PULSE, NULL), err, sizeof(err));
    assert(h || (fprintf(stderr, "%s\n", err), 0));
    hta_game_event e;
    while (hta_game_pop(&game, &e)) {}
    assert(hta_script_ability(h, 0));
    assert(game.units[1].vitals.health == 1000.0f);                  /* queued, not run */
    hta_script_phase(h, 1.0f / 60.0f);
    /* B (0.54 wu away) took the native hit, credited to A; Far did not. */
    assert(game.units[1].vitals.health < 1000.0f && game.units[1].last_attacker == 0);
    assert(game.units[2].vitals.health == 1000.0f && game.units[0].vitals.health == 1000.0f);
    bool hit = false;
    while (hta_game_pop(&game, &e)) if (e.kind == HTA_EV_HIT_UNIT && e.a == 1 && e.b == 0) hit = true;
    assert(hit);
    /* Native rules still apply: spawn protection, then the postgame. */
    float hp = game.units[1].vitals.health;
    game.units[1].protect = 2.0f;
    hta_script_phase(h, 1.1f);                                    /* cooldown over */
    assert(hta_script_ability(h, 0));
    hta_script_phase(h, 1.0f / 60.0f);
    assert(game.units[1].vitals.health == hp);
    game.units[1].protect = 0.0f;
    /* One press a second: a second press at once does nothing. */
    hta_script_phase(h, 1.1f);
    assert(hta_script_ability(h, 0) && hta_script_ability(h, 0));
    hta_script_phase(h, 1.0f / 60.0f);
    float after_one = game.units[1].vitals.health;
    assert(after_one < hp);
    hta_script_phase(h, 1.0f / 60.0f);
    assert(game.units[1].vitals.health == after_one);
    /* A dead activator does nothing. */
    hta_script_phase(h, 1.1f);
    game.units[0].alive = false;
    assert(hta_script_ability(h, 0));
    hta_script_phase(h, 1.0f / 60.0f);
    assert(game.units[1].vitals.health == after_one);
    assert(hta_script_destroy(h) == 0);
    /* No ability script: the press is not the script's. */
    h = load(world_json(BUTTON, "\"on_used\"", NULL, NULL), err, sizeof(err));
    assert(!hta_script_ability(h, 0));
    hta_script_destroy(h);
    printf("  proof B: ability -> on_ability -> game.near + game.damage -> hta_game_hurt (credit, protection, cooldown): ok\n");
}

static void test_lifecycle(void)
{
    char err[256];
    for (int i = 0; i < 25; i++) {
        hta_script_host *h = load(world_json(BUTTON, "\"on_used\"", PULSE, NULL), err, sizeof(err));
        assert(h);
        press(); hta_script_phase(h, 0.016f);
        assert(hta_script_reset(h) && hta_script_reset(h));
        assert(hta_script_ability(h, 0)); hta_script_phase(h, 0.016f);
        assert(hta_script_destroy(h) == 0);
    }
    printf("  lifecycle: 25 load/run/reset/destroy cycles, every byte returned: ok\n");
}

static void test_api_json(void)
{
    size_t n = hta_script_api_json(NULL, 0);
    char *buf = malloc(n + 1);
    assert(buf && hta_script_api_json(buf, n + 1) == n && strlen(buf) == n);
    assert(strstr(buf, "\"api\": \"megamod.v1\"") && strstr(buf, "\"name\": \"world.send\"") &&
           strstr(buf, "\"name\": \"game.damage\"") && strstr(buf, "\"name\": \"on_ability\"") &&
           strstr(buf, "\"instructions_per_callback\": 200000") && strstr(buf, "host.world.script"));
    free(buf);
    printf("  api json: ok\n");
}

int main(void)
{
    test_package();
    test_sandbox();
    test_budgets();
    test_handles();
    test_world_proof();
    test_ability_proof();
    test_lifecycle();
    test_api_json();
    hta_went_free(&went);
    printf("script: all ok\n");
    return 0;
}
