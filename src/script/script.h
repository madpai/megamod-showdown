/* Host-side Lua gameplay scripting (X3, docs/SCRIPTING.md): a narrow layer
 * that lets authored scripts DECIDE and REQUEST, never own engine state.
 *
 *   "Lua asks MegaMod Engine to do things. MegaMod Engine decides how
 *    those things actually happen."
 *
 * One Lua 5.4 state per match, on the HOST only (a joiner never creates
 * one). Each script (asset/world_def.h hta_wscript_def: namespace:script/
 * name, source text, declared callbacks) runs in its own environment
 * table; the engine API and the few safe libraries are shared read-only.
 * Scripts see entities and players only as opaque checked handles -- the
 * engine's own slot + generation handles (hta_went_handle for world
 * entities, hta_unit.generation for players) -- re-checked on every call.
 *
 * Every verb is a request into an existing engine path: world.send ->
 * hta_went_send (the bounded host event queue, as a link would), and
 * game.damage -> hta_game_hurt (the native damage, death and score
 * pipeline). Nothing here writes a mover, a transform, health, a packet or
 * a definition.
 *
 * When: hta_script_phase, once per host tick inside the session's world
 * step -- after hta_game_update and trigger sensing, before hta_went_step
 * dispatches the world-event queue (so a script's requests take effect in
 * the same tick) and before snapshots go out. HTA_SCRIPT_PHASE names it;
 * every callback's log line carries it and the host tick number.
 *
 * Bounds: an instruction budget per callback (count hook), a memory cap on
 * the whole state (its allocator), a cap on engine requests per callback,
 * no pcall (a script cannot swallow its own budget error). An error or an
 * exhausted budget aborts that callback only; a script that fails
 * HTA_SCRIPT_MAX_ERRORS times in a round is disabled until the next.
 *
 * The API is versioned (HTA_SCRIPT_API); a package declares the version it
 * was written for and anything else is refused at load. The verb and
 * callback tables in script.c are the single source for both the Lua
 * bindings and hta_script_api_json (megamod-script-api --json). */
#ifndef HTA_SCRIPT_H
#define HTA_SCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../asset/world_def.h"
#include "../engine/world_entities.h"
#include "../game/game.h"

#define HTA_SCRIPT_API HTA_WDEF_SCRIPT_API
#define HTA_SCRIPT_INSTRUCTIONS 200000u     /* per callback, and per script load (ours) */
#define HTA_SCRIPT_HOOK_EVERY 1000          /* instructions between budget checks */
#define HTA_SCRIPT_MEMORY (4u * 1024u * 1024u) /* the whole Lua state, bytes (ours) */
#define HTA_SCRIPT_REQUESTS 16u             /* engine requests per callback (ours) */
#define HTA_SCRIPT_LOGS 4u                  /* log lines per callback (ours) */
#define HTA_SCRIPT_MAX_ERRORS 8u            /* per script per round, then disabled (ours) */
#define HTA_SCRIPT_ABILITY_COOLDOWN 1.0f    /* seconds between one player's scripted abilities (ours) */
#define HTA_SCRIPT_MAX_DAMAGE 500.0f        /* one game.damage request, at most */
#define HTA_SCRIPT_MAX_RADIUS 16.0f         /* game.near, wu */
#define HTA_SCRIPT_PHASE "host.world.script"

typedef struct hta_script_host hta_script_host;

typedef struct {
    uint64_t callbacks, errors, budget_aborts, requests, refused_requests, ticks;
    size_t   memory, memory_peak;
} hta_script_stats;

/* One line for the host's log ("[script] ..."). */
typedef void (*hta_script_log_fn)(void *ctx, const char *line);

/* The match's scripts (defs->script_count > 0), loaded and each run once in
 * its environment. `defs`, `w` and `g` are borrowed and must outlive the
 * host. NULL with `err` naming the script and the reason (a syntax error,
 * a declared callback it does not define, its budget...). */
hta_script_host *hta_script_create(const hta_world_defs *defs, hta_world_entities *w, hta_game *g,
                                   hta_script_log_fn log, void *log_ctx, char *err, size_t errlen);
/* Closes the state; returns the bytes its allocator still counted after
 * lua_close (0: everything the VM took came back). */
size_t hta_script_destroy(hta_script_host *h);
/* A new round: fresh environments (each script's chunk runs again), queues
 * emptied, disabled scripts back. Handles from the last round are stale by
 * then (the world's and the game's own resets moved every generation). */
bool hta_script_reset(hta_script_host *h);
/* A player without a native ability pressed ability: queued for the world's
 * on_ability. False when the world has no ability script. */
bool hta_script_ability(hta_script_host *h, int32_t unit);
/* The script phase: the world's scripted uses (w->calls), then queued
 * ability presses, each callback bounded. Host only. */
void hta_script_phase(hta_script_host *h, float dt);
const hta_script_stats *hta_script_get_stats(const hta_script_host *h);

/* The registered API, limits and phase as JSON (NUL-terminated; returns
 * the length it needed). Generated from the same tables as the bindings. */
size_t hta_script_api_json(char *out, size_t cap);

#endif
