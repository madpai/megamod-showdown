/* Host-side Lua gameplay scripting (script.h, docs/SCRIPTING.md). */
#include "script.h"
#include "asset/resource.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_QUEUE 16u
#define HANDLE_MT "megamod.handle"

enum { H_ENTITY = 1, H_PLAYER = 2 };
typedef struct { uint32_t kind, h; } handle;

struct hta_script_host {
    lua_State *L;
    const hta_world_defs *defs;
    hta_world_entities *w;
    hta_game *g;
    hta_script_log_fn log;
    void *log_ctx;
    int env[HTA_WDEF_MAX_SCRIPTS];          /* registry refs to each script's environment */
    uint32_t errors[HTA_WDEF_MAX_SCRIPTS];   /* this round */
    bool disabled[HTA_WDEF_MAX_SCRIPTS];
    /* the running call */
    int32_t running;                         /* script index, -1 none */
    const char *callback;                    /* "load", "on_used", ... */
    uint64_t spent;
    uint32_t requests, logs;
    /* ability presses waiting for the phase, and per-unit cooldown */
    int32_t ability[MAX_QUEUE];
    uint32_t ability_count;
    float ability_cool[HTA_GAME_MAX_UNITS];
    size_t mem_limit;
    hta_script_stats st;
    char first_err[256];                     /* the last reset's first load failure */
    char last_err[200];                      /* the last callback error */
};

static hta_script_host *host_of(lua_State *L) { return *(hta_script_host **)lua_getextraspace(L); }

static void say(hta_script_host *h, const char *fmt, ...)
{
    char line[320];
    va_list a; va_start(a, fmt); vsnprintf(line, sizeof(line), fmt, a); va_end(a);
    if (h->log) h->log(h->log_ctx, line);
}

static const char *sid(const hta_script_host *h, int32_t i)
{
    return i >= 0 && (uint32_t)i < h->defs->script_count ? h->defs->script[i].id : "?";
}

/* ---- bounded memory: the state's only allocator ---------------------- */

static void *alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    hta_script_host *h = ud;
    if (!ptr) osize = 0;
    if (!nsize) { free(ptr); h->st.memory -= osize; return NULL; }
    if (nsize > osize && h->st.memory - osize + nsize > h->mem_limit) return NULL;
    void *p = realloc(ptr, nsize);
    if (!p) return NULL;
    h->st.memory = h->st.memory - osize + nsize;
    if (h->st.memory > h->st.memory_peak) h->st.memory_peak = h->st.memory;
    return p;
}

/* ---- bounded time: a count hook ---------------------------------------- */

static void hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    hta_script_host *h = host_of(L);
    h->spent += HTA_SCRIPT_HOOK_EVERY;
    if (h->spent > HTA_SCRIPT_INSTRUCTIONS)
        luaL_error(L, "exceeded instruction budget (%d)", (int)HTA_SCRIPT_INSTRUCTIONS);
}

/* ---- handles ------------------------------------------------------------- */

static void push_handle(lua_State *L, uint32_t kind, uint32_t h)
{
    handle *u = lua_newuserdatauv(L, sizeof(handle), 0);
    u->kind = kind; u->h = h;
    luaL_setmetatable(L, HANDLE_MT);
}

static void push_entity(lua_State *L, uint32_t index)
{
    push_handle(L, H_ENTITY, hta_went_handle_of(host_of(L)->w, index));
}

static uint32_t player_handle(const hta_game *g, int32_t unit)
{
    return ((uint32_t)g->units[unit].generation << 16) | (uint32_t)unit;
}

static void push_player(lua_State *L, int32_t unit)
{
    push_handle(L, H_PLAYER, player_handle(host_of(L)->g, unit));
}

static const handle *arg_handle(lua_State *L, int arg, uint32_t kind, const char *what)
{
    const handle *u = luaL_testudata(L, arg, HANDLE_MT);
    if (!u || u->kind != kind)
        luaL_error(L, "argument %d: expected %s handle, got %s", arg, what, luaL_typename(L, arg));
    return u;
}

/* A world entity's index, or a Lua error: never a stale or forged one. */
static uint32_t arg_entity(lua_State *L, int arg)
{
    const handle *u = arg_handle(L, arg, H_ENTITY, "an entity");
    hta_script_host *h = host_of(L);
    int32_t i = hta_went_resolve(h->w, u->h);
    if (i < 0) {
        uint32_t slot = u->h & 0xFFFFu;
        luaL_error(L, "stale entity handle %s (from an earlier round?)",
                   slot < h->defs->count ? h->defs->entity[slot].id : "?");
    }
    return (uint32_t)i;
}

static int32_t resolve_player(const hta_game *g, uint32_t hv)
{
    uint32_t slot = hv & 0xFFFFu;
    uint16_t gen = (uint16_t)(hv >> 16);
    if (!gen || slot >= g->unit_count || g->units[slot].kind == HTA_UNIT_NONE || g->units[slot].generation != gen)
        return -1;
    return (int32_t)slot;
}

static int32_t arg_player(lua_State *L, int arg)
{
    const handle *u = arg_handle(L, arg, H_PLAYER, "a player");
    int32_t i = resolve_player(host_of(L)->g, u->h);
    if (i < 0) luaL_error(L, "stale player handle (slot %d: left, or an earlier round)", (int)(u->h & 0xFFFFu));
    return i;
}

static int handle_tostring(lua_State *L)
{
    const handle *u = luaL_checkudata(L, 1, HANDLE_MT);
    hta_script_host *h = host_of(L);
    uint32_t slot = u->h & 0xFFFFu;
    if (u->kind == H_ENTITY) lua_pushfstring(L, "entity %s", slot < h->defs->count ? h->defs->entity[slot].id : "?");
    else lua_pushfstring(L, "player %d", (int)slot);
    return 1;
}

static int handle_eq(lua_State *L)
{
    const handle *a = luaL_testudata(L, 1, HANDLE_MT), *b = luaL_testudata(L, 2, HANDLE_MT);
    lua_pushboolean(L, a && b && a->kind == b->kind && a->h == b->h);
    return 1;
}

/* ---- the verbs ------------------------------------------------------------ */

static void in_callback(lua_State *L, const char *verb)
{
    hta_script_host *h = host_of(L);
    if (!strcmp(h->callback, "load")) luaL_error(L, "%s is only for callbacks, not at load", verb);
}

static void request(lua_State *L, const char *verb)
{
    hta_script_host *h = host_of(L);
    in_callback(L, verb);
    if (h->requests >= HTA_SCRIPT_REQUESTS)
        luaL_error(L, "%s: more than %d engine requests in one callback", verb, (int)HTA_SCRIPT_REQUESTS);
    h->requests++;
    h->st.requests++;
}

/* world.entity(id): a placed entity by its authored ID. Load time only:
 * names resolve once, then scripts hold handles. */
static int l_entity(lua_State *L)
{
    hta_script_host *h = host_of(L);
    const char *id = luaL_checkstring(L, 1);
    if (strcmp(h->callback, "load")) luaL_error(L, "world.entity is load-time only (resolve '%s' at the top of the script)", id);
    int32_t i = hta_world_defs_find(h->defs, id);
    if (i < 0) {
        /* The one grammar and registry (asset/resource.h): say what is wrong. */
        char why[128];
        hta_rid r;
        int rc = hta_rid_parse(id, &r, why, sizeof(why));
        if (rc == HTA_RID_MALFORMED) luaL_error(L, "world.entity: '%s' is not a resource ID: %s", id, why);
        if (rc != HTA_RID_OK) luaL_error(L, "world.entity: '%s': %s (expected a placed entity)", id, why);
        if (r.type != HTA_RT_ENTITY)
            luaL_error(L, "world.entity: '%s' is a %s ID, expected a placed entity (namespace:entity/name)", id,
                       hta_rtype_get(r.type)->noun);
        luaL_error(L, "unknown entity '%s'", id);
    }
    push_entity(L, (uint32_t)i);
    return 1;
}

/* world.send(entity, input [, player]): an input into the host's bounded
 * world-event queue, exactly as a link would send it. */
static int l_send(lua_State *L)
{
    hta_script_host *h = host_of(L);
    uint32_t i = arg_entity(L, 1);
    const char *name = luaL_checkstring(L, 2);
    uint8_t input = hta_wdef_input_from_name(name);
    if (!input) luaL_error(L, "requested invalid input '%s'", name);
    const hta_wdef *e = &h->defs->entity[i];
    if (!hta_wdef_accepts(e->kind, input))
        luaL_error(L, "%s (%s) does not accept '%s'", e->id, hta_wdef_kind_name(e->kind), name);
    int32_t actor = lua_isnoneornil(L, 3) ? -1 : arg_player(L, 3);
    request(L, "world.send");
    /* X7: the one action seam (its five link inputs are actions of the
     * same names), exactly as a binding or a future agent requests. */
    bool ok = hta_went_request(h->w, hta_waction_from_name(name), i,
                               actor >= 0 && actor < (int32_t)HTA_WENT_MAX_ACTORS ? (uint8_t)actor : HTA_WENT_NO_ACTOR) == HTA_WENT_QUEUED;
    if (!ok) h->st.refused_requests++;
    lua_pushboolean(L, ok);
    return 1;
}

/* world.state(entity): a mover's phase, "closed" .. "closing"; nil for
 * anything else. Read-only. */
static int l_state(lua_State *L)
{
    hta_script_host *h = host_of(L);
    uint32_t i = arg_entity(L, 1);
    static const char *const phase[] = { "closed", "opening", "open", "closing" };
    if (h->defs->entity[i].kind != HTA_WDEF_MOVER) { lua_pushnil(L); return 1; }
    lua_pushstring(L, phase[h->w->st[i].phase & 3u]);
    return 1;
}

/* game.near(player, radius): the other living players within radius (wu)
 * of the player, nearest first, as handles. Read-only. */
static int l_near(lua_State *L)
{
    hta_script_host *h = host_of(L);
    int32_t me = arg_player(L, 1);
    lua_Number r = luaL_checknumber(L, 2);
    if (!(r > 0.0 && r <= HTA_SCRIPT_MAX_RADIUS)) luaL_error(L, "game.near: radius must be in (0, %d]", (int)HTA_SCRIPT_MAX_RADIUS);
    const hta_unit *m = &h->g->units[me];
    int32_t pick[HTA_GAME_MAX_UNITS];
    float dist[HTA_GAME_MAX_UNITS];
    uint32_t n = 0;
    for (uint32_t i = 0; i < h->g->unit_count; i++) {
        const hta_unit *u = &h->g->units[i];
        if ((int32_t)i == me || u->kind == HTA_UNIT_NONE || !u->alive) continue;
        float d[3] = { u->body.pos[0] - m->body.pos[0], u->body.pos[1] - m->body.pos[1], u->body.pos[2] - m->body.pos[2] };
        float dd = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!(dd <= (float)r)) continue;
        uint32_t k = n++;
        while (k && dist[k - 1] > dd) { dist[k] = dist[k - 1]; pick[k] = pick[k - 1]; k--; }
        dist[k] = dd; pick[k] = (int32_t)i;
    }
    lua_createtable(L, (int)n, 0);
    for (uint32_t k = 0; k < n; k++) { push_player(L, pick[k]); lua_rawseti(L, -2, (lua_Integer)k + 1); }
    return 1;
}

/* game.damage(target, amount [, by]): the native damage pipeline --
 * shields, health, protection, teams, death, credit and score are the
 * game's (hta_game_hurt), not the script's. */
static int l_damage(lua_State *L)
{
    hta_script_host *h = host_of(L);
    int32_t victim = arg_player(L, 1);
    lua_Number amount = luaL_checknumber(L, 2);
    if (!(amount > 0.0 && amount <= HTA_SCRIPT_MAX_DAMAGE))
        luaL_error(L, "game.damage: amount must be in (0, %d]", (int)HTA_SCRIPT_MAX_DAMAGE);
    int32_t by = lua_isnoneornil(L, 3) ? -1 : arg_player(L, 3);
    request(L, "game.damage");
    hta_game_hurt(h->g, victim, by, (float)amount, NULL);
    return 0;
}

/* log(...): one line in the host's log, prefixed with the script ID. */
static int l_log(lua_State *L)
{
    hta_script_host *h = host_of(L);
    if (h->logs >= HTA_SCRIPT_LOGS) return 0;
    h->logs++;
    int n = lua_gettop(L);                   /* before the buffer takes a slot */
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++) {
        if (i > 1) luaL_addchar(&b, ' ');
        luaL_tolstring(L, i, NULL);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    say(h, "[script] %s: %.200s", sid(h, h->running), lua_tostring(L, -1));
    return 0;
}

/* The registered API: bindings and megamod-script-api come from here. */
typedef struct {
    const char *name, *args, *returns, *when, *doc;
    lua_CFunction fn;
} verb;
static const verb VERBS[] = {
    { "world.entity", "id: string", "entity handle", "load",
      "a placed entity by its authored ID (namespace:entity/name); load time only", l_entity },
    { "world.send", "entity: entity handle, input: string, [actor: player handle]", "boolean (queued)", "callback",
      "request an input (activate, open, close, toggle, teleport) through the host's bounded world-event queue", l_send },
    { "world.state", "entity: entity handle", "string|nil", "any",
      "a mover's phase: closed, opening, open, closing (nil for other kinds)", l_state },
    { "game.near", "player: player handle, radius: number", "array of player handles", "callback",
      "other living players within radius (wu, at most 16), nearest first", l_near },
    { "game.damage", "target: player handle, amount: number, [by: player handle]", "nothing", "callback",
      "request damage through the native damage pipeline (amount at most 500)", l_damage },
    { "log", "...: any", "nothing", "any", "one line in the host's log, prefixed with the script ID", l_log },
};

typedef struct { const char *name, *args, *when; uint32_t bit; } callback;
static const callback CALLBACKS[] = {
    { "on_used", "entity: entity handle, player: player handle",
      "a player used an interactable whose package names this script", HTA_WCB_ON_USED },
    { "on_ability", "player: player handle",
      "a player with no native (character) ability pressed ability; the world names this script as ability_script",
      HTA_WCB_ON_ABILITY },
};

/* The safe libraries: exactly these names, nothing else. */
static const char *const BASE_KEEP[] = {
    "assert", "error", "ipairs", "next", "pairs", "select", "tonumber", "tostring", "type", "rawequal", "rawlen",
};
static const char *const STRING_KEEP[] = { "byte", "char", "format", "len", "lower", "rep", "reverse", "sub", "upper" };
static const char *const TABLE_KEEP[] = { "concat", "insert", "move", "pack", "remove", "sort", "unpack" };
static const char *const MATH_DROP[] = { "random", "randomseed" };

/* A read-only view of the table on top: an empty table whose metatable
 * reads through and refuses writes. Replaces the table on the stack. */
static int readonly_newindex(lua_State *L) { return luaL_error(L, "engine and library tables are read-only"); }
static void readonly(lua_State *L)
{
    lua_newtable(L);                         /* view */
    lua_createtable(L, 0, 3);                /* its metatable */
    lua_pushvalue(L, -3); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, readonly_newindex); lua_setfield(L, -2, "__newindex");
    lua_pushboolean(L, 0); lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_remove(L, -2);
}

/* Copy the listed fields of the table at `src` into the table on top. */
static void keep(lua_State *L, int src, const char *const *names, size_t n)
{
    for (size_t i = 0; i < n; i++) { lua_getfield(L, src, names[i]); lua_setfield(L, -2, names[i]); }
}

/* The shared, read-only base every environment reads through. Left in the
 * registry as "megamod.base". */
static void build_base(lua_State *L)
{
    lua_pushcfunction(L, luaopen_base); lua_call(L, 0, 1);          /* its _G */
    int g = lua_gettop(L);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 0);
    int str = lua_gettop(L);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 0);
    int tab = lua_gettop(L);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 0);
    int mth = lua_gettop(L);

    lua_newtable(L);                                                  /* base */
    int base = lua_gettop(L);
    keep(L, g, BASE_KEEP, sizeof(BASE_KEEP) / sizeof(BASE_KEEP[0]));
    lua_newtable(L); keep(L, str, STRING_KEEP, sizeof(STRING_KEEP) / sizeof(STRING_KEEP[0]));
    readonly(L);
    /* Strings' own metatable reads the same safe subset ("x"):rep(3). */
    lua_pushliteral(L, "");
    lua_getmetatable(L, -1);
    lua_pushvalue(L, -3); lua_setfield(L, -2, "__index");
    lua_pop(L, 2);
    lua_setfield(L, base, "string");
    lua_newtable(L); keep(L, tab, TABLE_KEEP, sizeof(TABLE_KEEP) / sizeof(TABLE_KEEP[0])); readonly(L);
    lua_setfield(L, base, "table");
    lua_newtable(L);
    lua_pushnil(L);
    while (lua_next(L, mth)) {
        bool drop = false;
        for (size_t k = 0; k < sizeof(MATH_DROP) / sizeof(MATH_DROP[0]); k++)
            if (lua_type(L, -2) == LUA_TSTRING && !strcmp(lua_tostring(L, -2), MATH_DROP[k])) drop = true;
        if (drop) { lua_pop(L, 1); continue; }
        lua_pushvalue(L, -2); lua_insert(L, -2); lua_settable(L, -4);
    }
    readonly(L);
    lua_setfield(L, base, "math");
    /* The engine: world.*, game.*, log, megamod.api -- from VERBS. */
    lua_newtable(L); int world = lua_gettop(L);
    lua_newtable(L); int game = lua_gettop(L);
    for (size_t i = 0; i < sizeof(VERBS) / sizeof(VERBS[0]); i++) {
        const char *dot = strchr(VERBS[i].name, '.');
        int into = !dot ? base : !strncmp(VERBS[i].name, "world.", 6) ? world : game;
        lua_pushcfunction(L, VERBS[i].fn);
        lua_setfield(L, into, dot ? dot + 1 : VERBS[i].name);
    }
    lua_pushvalue(L, game); readonly(L); lua_setfield(L, base, "game");
    lua_pushvalue(L, world); readonly(L); lua_setfield(L, base, "world");
    lua_pop(L, 2);
    lua_newtable(L); lua_pushliteral(L, HTA_SCRIPT_API); lua_setfield(L, -2, "api"); readonly(L);
    lua_setfield(L, base, "megamod");
    lua_setfield(L, LUA_REGISTRYINDEX, "megamod.base");
    lua_settop(L, 0);
    /* Handles: opaque userdata; Lua cannot make or change one. */
    luaL_newmetatable(L, HANDLE_MT);
    lua_pushcfunction(L, handle_tostring); lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, handle_eq); lua_setfield(L, -2, "__eq");
    lua_pushboolean(L, 0); lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
}

/* ---- running ------------------------------------------------------------ */

/* Call the function on top with `nargs` below it, bounded, as script `i`'s
 * `cb`. False when it failed (logged); the host goes on either way. */
static bool run(hta_script_host *h, int32_t i, const char *cb, int nargs, const char *what)
{
    lua_State *L = h->L;
    h->running = i; h->callback = cb; h->spent = 0; h->requests = 0; h->logs = 0;
    lua_sethook(L, hook, LUA_MASKCOUNT, HTA_SCRIPT_HOOK_EVERY);
    int rc = lua_pcall(L, nargs, 0, 0);
    lua_sethook(L, NULL, 0, 0);
    bool ok = rc == LUA_OK;
    if (!ok) {
        const char *msg = lua_tostring(L, -1);
        snprintf(h->last_err, sizeof(h->last_err), "%s", rc == LUA_ERRMEM ? "out of memory (4 MB cap)" : msg ? msg : "error");
        bool budget = msg && strstr(msg, "exceeded instruction budget");
        h->st.errors++;
        if (budget) h->st.budget_aborts++;
        say(h, "[script] %s %s%s: %s%s", sid(h, i), cb, what, rc == LUA_ERRMEM ? "out of memory (4 MB cap)" : msg ? msg : "error",
            h->requests ? " (requests made before it stay made)" : "");
        lua_pop(L, 1);
        if (i >= 0 && ++h->errors[i] >= HTA_SCRIPT_MAX_ERRORS && !h->disabled[i]) {
            h->disabled[i] = true;
            say(h, "[script] %s disabled for the rest of the round after %u errors", sid(h, i), h->errors[i]);
        }
        lua_gc(L, LUA_GCCOLLECT);
    }
    h->running = -1; h->callback = "none";
    return ok;
}

/* Script i's chunk in a fresh environment; its declared callbacks must be
 * functions afterwards. */
static bool load_one(hta_script_host *h, uint32_t i, char *err, size_t n)
{
    lua_State *L = h->L;
    const hta_wscript_def *sc = &h->defs->script[i];
    char name[HTA_WDEF_ID_MAX + 2];
    snprintf(name, sizeof(name), "=%s", sc->id);
    /* Text only: bytecode is never loaded (it can crash the VM). */
    if (luaL_loadbufferx(L, h->defs->pool + sc->at, sc->len, name, "t") != LUA_OK) {
        if (err && n) snprintf(err, n, "%s: syntax error: %s", sc->id, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (h->env[i] != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, h->env[i]);
    lua_newtable(L);                                       /* env */
    lua_createtable(L, 0, 2);
    lua_getfield(L, LUA_REGISTRYINDEX, "megamod.base"); lua_setfield(L, -2, "__index");
    lua_pushboolean(L, 0); lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_pushvalue(L, -1);
    h->env[i] = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_setupvalue(L, -2, 1);                              /* the chunk's _ENV */
    if (!run(h, (int32_t)i, "load", 0, "")) {
        if (err && n) snprintf(err, n, "%s: failed while loading: %s", sc->id, h->last_err);
        return false;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, h->env[i]);
    for (size_t k = 0; k < sizeof(CALLBACKS) / sizeof(CALLBACKS[0]); k++) {
        if (!(sc->callbacks & CALLBACKS[k].bit)) continue;
        lua_getfield(L, -1, CALLBACKS[k].name);
        bool fn = lua_isfunction(L, -1);
        lua_pop(L, 1);
        if (!fn) {
            if (err && n) snprintf(err, n, "%s: declares %s but defines no function %s", sc->id, CALLBACKS[k].name, CALLBACKS[k].name);
            lua_pop(L, 1);
            return false;
        }
    }
    lua_pop(L, 1);
    return true;
}

hta_script_host *hta_script_create(const hta_world_defs *defs, hta_world_entities *w, hta_game *g,
                                   hta_script_log_fn log, void *log_ctx, char *err, size_t errlen)
{
    if (err && errlen) err[0] = 0;
    if (!defs || !defs->script_count || !w || !g) return NULL;
    hta_script_host *h = calloc(1, sizeof(*h));
    if (!h) { if (err && errlen) snprintf(err, errlen, "out of memory"); return NULL; }
    h->defs = defs; h->w = w; h->g = g; h->log = log; h->log_ctx = log_ctx;
    h->mem_limit = HTA_SCRIPT_MEMORY;
    h->running = -1; h->callback = "none";
    for (uint32_t i = 0; i < HTA_WDEF_MAX_SCRIPTS; i++) h->env[i] = LUA_NOREF;
    h->L = lua_newstate(alloc, h);
    if (!h->L) { free(h); if (err && errlen) snprintf(err, errlen, "cannot create the Lua state"); return NULL; }
    *(hta_script_host **)lua_getextraspace(h->L) = h;
    build_base(h->L);
    if (!hta_script_reset(h)) {
        if (err && errlen) snprintf(err, errlen, "%s", h->first_err);
        hta_script_destroy(h);
        return NULL;
    }
    return h;
}

bool hta_script_reset(hta_script_host *h)
{
    if (!h) return false;
    h->ability_count = 0;
    memset(h->ability_cool, 0, sizeof(h->ability_cool));
    memset(h->errors, 0, sizeof(h->errors));
    memset(h->disabled, 0, sizeof(h->disabled));
    bool ok = true;
    h->first_err[0] = 0;
    for (uint32_t i = 0; i < h->defs->script_count; i++) {
        char e[256];
        if (!load_one(h, i, e, sizeof(e))) {
            say(h, "[script] %s", e);
            if (ok) snprintf(h->first_err, sizeof(h->first_err), "%s", e);
            h->disabled[i] = true;
            ok = false;
        }
    }
    lua_gc(h->L, LUA_GCCOLLECT);
    return ok;
}

size_t hta_script_destroy(hta_script_host *h)
{
    if (!h) return 0;
    if (h->L) lua_close(h->L);
    size_t left = h->st.memory;
    free(h);
    return left;
}

bool hta_script_ability(hta_script_host *h, int32_t unit)
{
    if (!h || !h->defs->ability_script || unit < 0 || unit >= (int32_t)HTA_GAME_MAX_UNITS) return false;
    if (h->ability_cool[unit] > 0.0f) return true;          /* pressed too soon: taken, nothing happens */
    if (h->ability_count >= MAX_QUEUE) return true;
    h->ability_cool[unit] = HTA_SCRIPT_ABILITY_COOLDOWN;
    h->ability[h->ability_count++] = unit;
    return true;
}

/* Push the callback function of script i by name; false when missing. */
static bool push_callback(hta_script_host *h, uint32_t i, const char *name)
{
    lua_rawgeti(h->L, LUA_REGISTRYINDEX, h->env[i]);
    lua_getfield(h->L, -1, name);
    lua_remove(h->L, -2);
    if (lua_isfunction(h->L, -1)) return true;
    lua_pop(h->L, 1);
    return false;
}

static void observed(hta_script_host *h, uint32_t i, const char *cb, const char *args, bool ok)
{
    char ins[32];
    if (h->spent) snprintf(ins, sizeof(ins), "%llu", (unsigned long long)h->spent);
    else snprintf(ins, sizeof(ins), "under %d", HTA_SCRIPT_HOOK_EVERY);
    say(h, "[script] %s %s(%s) phase %s tick %llu: %s, %u requests, %s instructions", h->defs->script[i].id, cb, args,
        HTA_SCRIPT_PHASE, (unsigned long long)h->st.ticks, ok ? "ok" : "aborted", h->requests, ins);
}

void hta_script_phase(hta_script_host *h, float dt)
{
    if (!h) return;
    h->st.ticks++;
    for (uint32_t u = 0; u < HTA_GAME_MAX_UNITS; u++) if (h->ability_cool[u] > 0.0f) h->ability_cool[u] -= dt;
    /* Scripted uses, in the order the host took them. */
    for (uint32_t k = 0; k < h->w->call_count; k++) {
        hta_went_call c = h->w->calls[k];
        if (c.entity >= h->defs->count || !h->defs->entity[c.entity].script) continue;
        uint32_t i = h->defs->entity[c.entity].script - 1u;
        if (h->disabled[i] || !push_callback(h, i, "on_used")) continue;
        push_entity(h->L, c.entity);
        int nargs = 1;
        char args[160];
        if (c.actor < h->g->unit_count && h->g->units[c.actor].kind != HTA_UNIT_NONE) { push_player(h->L, c.actor); nargs++; }
        else lua_pushnil(h->L), nargs++;
        snprintf(args, sizeof(args), "%s, unit %u", h->defs->entity[c.entity].id, c.actor);
        h->st.callbacks++;
        char what[180]; snprintf(what, sizeof(what), "(%s)", args);
        bool ok = run(h, (int32_t)i, "on_used", nargs, what);
        observed(h, i, "on_used", args, ok);
    }
    h->w->call_count = 0;
    /* Ability presses. */
    for (uint32_t k = 0; k < h->ability_count; k++) {
        int32_t unit = h->ability[k];
        uint32_t i = h->defs->ability_script - 1u;
        if (h->disabled[i] || unit >= (int32_t)h->g->unit_count || h->g->units[unit].kind == HTA_UNIT_NONE ||
            !h->g->units[unit].alive || h->g->over) continue;
        if (!push_callback(h, i, "on_ability")) continue;
        push_player(h->L, unit);
        char args[32]; snprintf(args, sizeof(args), "unit %d", (int)unit);
        char what[48]; snprintf(what, sizeof(what), "(%s)", args);
        h->st.callbacks++;
        bool ok = run(h, (int32_t)i, "on_ability", 1, what);
        observed(h, i, "on_ability", args, ok);
    }
    h->ability_count = 0;
}

const hta_script_stats *hta_script_get_stats(const hta_script_host *h) { return h ? &h->st : NULL; }

/* ---- the API, as data ---------------------------------------------------- */

typedef struct { char *p; size_t cap, n; } out;
static void put(out *o, const char *fmt, ...)
{
    va_list a; va_start(a, fmt);
    int k = vsnprintf(o->p && o->n < o->cap ? o->p + o->n : NULL, o->p && o->n < o->cap ? o->cap - o->n : 0, fmt, a);
    va_end(a);
    if (k > 0) o->n += (size_t)k;
}
static void list(out *o, const char *const *names, size_t n)
{
    for (size_t i = 0; i < n; i++) put(o, "%s\"%s\"", i ? ", " : "", names[i]);
}

size_t hta_script_api_json(char *buf, size_t cap)
{
    out o = { buf, cap, 0 };
    if (buf && cap) buf[0] = 0;
    put(&o, "{\n  \"api\": \"%s\",\n  \"runtime\": \"%s\",\n  \"authority\": \"host only; joiners never run scripts\",\n",
        HTA_SCRIPT_API, LUA_RELEASE);
    put(&o, "  \"phase\": {\"name\": \"%s\", \"after\": [\"host input (use presses)\", \"game.update\", \"trigger sensing\"], "
            "\"before\": [\"world-event dispatch\", \"movers\", \"teleports\", \"joiner input\", \"snapshots\"]},\n", HTA_SCRIPT_PHASE);
    put(&o, "  \"callbacks\": [\n");
    for (size_t i = 0; i < sizeof(CALLBACKS) / sizeof(CALLBACKS[0]); i++)
        put(&o, "    {\"name\": \"%s\", \"args\": \"%s\", \"when\": \"%s\"}%s\n", CALLBACKS[i].name, CALLBACKS[i].args,
            CALLBACKS[i].when, i + 1 < sizeof(CALLBACKS) / sizeof(CALLBACKS[0]) ? "," : "");
    put(&o, "  ],\n  \"functions\": [\n");
    for (size_t i = 0; i < sizeof(VERBS) / sizeof(VERBS[0]); i++)
        put(&o, "    {\"name\": \"%s\", \"args\": \"%s\", \"returns\": \"%s\", \"when\": \"%s\", \"does\": \"%s\"}%s\n",
            VERBS[i].name, VERBS[i].args, VERBS[i].returns, VERBS[i].when, VERBS[i].doc,
            i + 1 < sizeof(VERBS) / sizeof(VERBS[0]) ? "," : "");
    put(&o, "  ],\n  \"libraries\": {\"base\": [");
    list(&o, BASE_KEEP, sizeof(BASE_KEEP) / sizeof(BASE_KEEP[0]));
    put(&o, "], \"string\": [");
    list(&o, STRING_KEEP, sizeof(STRING_KEEP) / sizeof(STRING_KEEP[0]));
    put(&o, "], \"table\": [");
    list(&o, TABLE_KEEP, sizeof(TABLE_KEEP) / sizeof(TABLE_KEEP[0]));
    put(&o, "], \"math\": \"all but\", \"math_removed\": [");
    list(&o, MATH_DROP, sizeof(MATH_DROP) / sizeof(MATH_DROP[0]));
    put(&o, "], \"not_built\": [\"io\", \"os\", \"package\", \"debug\", \"coroutine\", \"utf8\"]},\n");
    put(&o, "  \"handles\": \"opaque userdata: slot + generation, re-checked on every call; stale after a round restart\",\n");
    put(&o, "  \"source\": \"Lua text only (bytecode refused), ASCII, 32 KB per script, 64 KB per world, 16 scripts\",\n");
    put(&o, "  \"limits\": {\"instructions_per_callback\": %u, \"memory_bytes\": %u, \"requests_per_callback\": %u, "
            "\"logs_per_callback\": %u, \"errors_per_round_before_disable\": %u, \"ability_cooldown_seconds\": %.1f, "
            "\"max_damage\": %.0f, \"max_radius\": %.0f}\n}\n",
        HTA_SCRIPT_INSTRUCTIONS, HTA_SCRIPT_MEMORY, HTA_SCRIPT_REQUESTS, HTA_SCRIPT_LOGS, HTA_SCRIPT_MAX_ERRORS,
        (double)HTA_SCRIPT_ABILITY_COOLDOWN, (double)HTA_SCRIPT_MAX_DAMAGE, (double)HTA_SCRIPT_MAX_RADIUS);
    return o.n;
}
