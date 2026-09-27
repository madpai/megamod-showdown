/* World entities at RUNTIME: the mutable instances of a world's compiled
 * definitions (asset/world_def.h), the host's bounded event queue, movers
 * with collision, triggers and teleports. X1: docs/WORLD_ENTITIES.md.
 *
 * Definition vs instance: `defs` is the package's immutable description
 * (borrowed, const: nothing here writes it); `st` is what changes during a
 * match -- a mover's progress, a trigger's occupants, an interactable's
 * cooldown -- and a generation per slot. A mover definition shared by
 * several placed movers (X2) is built into ONE collision grid, which each
 * of those movers places as its own collision instance at its own offset:
 * opening one moves only its instance. Links are resolved at load into handles (slot + generation); every
 * dispatched event re-checks its handle, so an event queued before a round
 * restart cannot act on the new round's entity.
 *
 * Authority: the HOST calls hta_went_interact / hta_went_sense and runs
 * the queue in hta_went_step. A LAN client (`remote`) never dispatches: it
 * applies the host's mover states (hta_went_apply) and only animates them.
 * What crosses the network is resulting state, never events.
 *
 * Bounds: the queue holds HTA_WENT_QUEUE events; at most HTA_WENT_BUDGET
 * are dispatched per step (the rest wait for the next step, in order); an
 * event more than HTA_WDEF_MAX_CHAIN links from its root is dropped (only a
 * corrupt, cyclic graph gets there -- the loader rejects cycles). Overflow
 * and drops are counted and described in `diag`, never silent.
 *
 * Portable C11, no allocation, no platform. */
#ifndef HTA_WORLD_ENTITIES_H
#define HTA_WORLD_ENTITIES_H

#include <stdbool.h>
#include <stdint.h>
#include "../asset/world_def.h"
#include "player.h"

#define HTA_WENT_QUEUE 512u
#define HTA_WENT_BUDGET 256u
#define HTA_WENT_MAX_ACTORS 64u         /* trigger occupancy is a 64-bit mask */
#define HTA_WENT_MAX_TELEPORTS 16u
#define HTA_WENT_COOLDOWN 0.5f          /* an interactable, between uses (ours) */
#define HTA_WENT_NO_ACTOR 0xFFu
#define HTA_WENT_MAX_CALLS 16u          /* scripted uses waiting for the script phase, per step */
#define HTA_WENT_MAX_CUES 16u           /* X5: sounds to start, per step */

/* A checked reference: slot in the low 16 bits, generation in the high.
 * Generation 0 never occurs, so 0 is "no handle". */
typedef uint32_t hta_went_handle;

enum { HTA_MOVER_CLOSED = 0, HTA_MOVER_OPENING, HTA_MOVER_OPEN, HTA_MOVER_CLOSING };

typedef struct {
    uint16_t generation;
    /* mover */
    uint8_t  phase;             /* HTA_MOVER_* */
    float    t;                 /* 0 closed .. 1 open */
    /* trigger */
    uint64_t inside;            /* bit per actor */
    /* interactable */
    float    cooldown;
    /* mover (X5): the phase its sound last answered, so a start sounds once */
    uint8_t  heard;
} hta_went_state;

typedef struct {
    hta_went_handle target;
    uint16_t source;            /* the entity that emitted it */
    uint8_t  input;             /* hta_wdef_input */
    uint8_t  actor;             /* who began the chain, HTA_WENT_NO_ACTOR none */
    uint8_t  depth;             /* links from the root */
    uint32_t seq;
} hta_went_event;

typedef struct { uint8_t actor; float pos[3], yaw; } hta_went_teleport;

/* A scripted interactable was used (X3): the host's script phase
 * (script/script.h) calls its script's on_used, then forgets it. This
 * file never runs a script; it only says one is due. */
typedef struct { uint8_t entity, actor; } hta_went_call;

/* X5: a mover whose definition names a sound started to open or close
 * this step -- on the host and, from the host's replicated state, on every
 * joiner alike. The caller plays `sound` (asset table index) at `pos`. A
 * join's first state (a snap) and a round reset are silent. */
typedef struct { uint8_t entity; uint16_t sound; float pos[3]; } hta_went_cue;

typedef struct {
    uint64_t dispatched, deferred, dropped_full, dropped_depth, dropped_stale, dropped_input;
    uint32_t max_queue;
} hta_went_stats;

typedef struct hta_world_entities {
    const hta_world_defs *defs;          /* borrowed, immutable */
    hta_went_state  st[HTA_WDEF_MAX_ENTITIES];
    hta_went_handle link_target[HTA_WDEF_MAX_LINKS];
    /* mover definitions: a box grid each (shared, built once at load) */
    hta_bsp_mesh    mover_mesh[HTA_WDEF_MAX_MOVER_DEFS];
    hta_collision   mover_coll[HTA_WDEF_MAX_MOVER_DEFS];
    hta_vertex      mover_verts[HTA_WDEF_MAX_MOVER_DEFS][8];
    uint32_t        mover_idx[HTA_WDEF_MAX_MOVER_DEFS][36];
    /* placed movers: their definition's grid placed where they are; props
     * (X5): their own box grid, standing still */
    hta_collision_instance inst[HTA_WDEF_MAX_ENTITIES];
    hta_bsp_mesh    prop_mesh[HTA_WDEF_MAX_ENTITIES];
    hta_collision   prop_coll[HTA_WDEF_MAX_ENTITIES];
    hta_vertex      prop_verts[HTA_WDEF_MAX_ENTITIES][8];
    uint32_t        prop_idx[HTA_WDEF_MAX_ENTITIES][36];
    /* the queue: a ring */
    hta_went_event  queue[HTA_WENT_QUEUE];
    uint32_t        head, count, seq;
    /* what the step decided the caller must do */
    hta_went_teleport teleports[HTA_WENT_MAX_TELEPORTS];
    uint32_t        teleport_count;
    hta_went_call   calls[HTA_WENT_MAX_CALLS];
    uint32_t        call_count;
    hta_went_cue    cues[HTA_WENT_MAX_CUES];
    uint32_t        cue_count;
    bool            remote;              /* a LAN client: the host decides */
    bool            loaded;
    uint32_t        version;             /* bumps when any mover's state changes */
    hta_went_stats  stats;
    char            diag[192];           /* the last problem, for the log */
    uint32_t        diag_count;
} hta_world_entities;

/* Instances for `defs` (borrowed; must outlive w), links resolved to
 * handles, movers' collision built. Checks `defs` again first. */
bool hta_went_load(hta_world_entities *w, const hta_world_defs *defs, char *err, size_t errlen);
void hta_went_free(hta_world_entities *w);
/* A new round: every generation moves on (queued events go stale), the
 * queue empties, movers close, triggers forget who stood in them. */
void hta_went_reset(hta_world_entities *w);

hta_went_handle hta_went_handle_of(const hta_world_entities *w, uint32_t index);
/* The slot a handle names, or -1 when it is stale or invalid. */
int32_t hta_went_resolve(const hta_world_entities *w, hta_went_handle h);

/* Host: `actor` pressed use with its eye at `eye` looking along `fwd`
 * (unit). The nearest interactable within its reach and in front of the
 * eye is used: returns its index (the press is consumed), else -1. */
int32_t hta_went_interact(hta_world_entities *w, uint8_t actor, const float eye[3], const float fwd[3]);
/* The interactable such a press would use, without using it (a prompt). */
int32_t hta_went_can_interact(const hta_world_entities *w, const float eye[3], const float fwd[3]);
/* Host, every step, for every actor: where its body is (feet) and whether
 * it is there at all. Entering a trigger emits `entered` once; staying in
 * it does not; leaving (or dying) re-arms it. */
void hta_went_sense(hta_world_entities *w, uint8_t actor, const float feet[3], bool present);
/* Queue the input on the entity directly (tests; a future scripting API). */
bool hta_went_send(hta_world_entities *w, uint32_t target, uint8_t input, uint8_t actor);

/* One step: the queue (host only), then every mover and cooldown. Fills
 * `teleports` for the caller to apply to its actors. */
void hta_went_step(hta_world_entities *w, float dt);

/* Movers' and props' collision instances, appended to `out` (room for
 * `cap`): the count written. */
uint32_t hta_went_instances(const hta_world_entities *w, hta_collision_instance *out, uint32_t cap);
/* A mover's current offset from its closed place. */
void hta_went_offset(const hta_world_entities *w, uint32_t index, float out[3]);

/* Replication, host -> clients: each mover's phase and progress. `t_q`
 * is t in 1/65535ths. */
typedef struct { uint8_t index, phase; uint16_t t_q; } hta_went_mover_state;
uint32_t hta_went_snapshot(const hta_world_entities *w, hta_went_mover_state *out, uint32_t cap);
/* Client: the host's word for one mover. Ignored unless `index` is a mover
 * here. `snap` jumps to it (a join); else it is eased toward. */
bool hta_went_apply(hta_world_entities *w, const hta_went_mover_state *s, bool snap);

#endif
