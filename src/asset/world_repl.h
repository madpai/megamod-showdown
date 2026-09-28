/* X8: which world objects carry replicated state, and where on the wire
 * (docs/WORLD_STATE.md).
 *
 * Three identities, kept apart:
 *
 *   runtime object index  every placed or prefab-expanded entity, 0 ..
 *                         count-1 in the compiled order (world_def.h), below
 *                         HTA_WDEF_MAX_ENTITIES. The host simulates by it;
 *                         links, bindings, Lua handles and the trace name it.
 *                         It is the same on every peer because the world key
 *                         covers the bytes it is compiled from.
 *   spatial index         movers only, 0 .. spatial_count-1, in runtime
 *                         order: where a mover's phase and progress sit in
 *                         WORLD_STATE.
 *   flag index            relays only, 0 .. flag_count-1, in runtime order:
 *                         a relay's active bit in WORLD_STATE (logical state).
 *
 * Every other kind is HOST-ONLY: its runtime state (a trigger's occupants,
 * an interactable's cooldown) never leaves the host, and its static facts
 * (where it is, what it looks like) are in the package both sides load.
 * Such an object costs no replicated slot and no byte. Nothing a client
 * sends names an object: a press is the player's controls, and the host
 * finds the button by its own reach test.
 *
 * The mapping is a pure function of the compiled definitions: no pointer,
 * hash order or platform enters it. Portable C11, no allocation. */
#ifndef HTA_WORLD_REPL_H
#define HTA_WORLD_REPL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "world_def.h"

#define HTA_WREP_MAX_SPATIAL 256u     /* movers in one world: every one's state fits one WORLD_STATE */
#define HTA_WREP_MAX_FLAGS 1024u      /* relays in one world (logical flags) */
#define HTA_WREP_NONE 0xFFFFu

/* A kind's replication channel. */
typedef enum { HTA_WREP_HOST_ONLY = 0, HTA_WREP_SPATIAL, HTA_WREP_FLAG, HTA_WREP_CHANNEL_COUNT } hta_wrep_channel;

/* One row per kind: the classification X8 is built on, printed by
 * megamod-resources --json (so Open Asset Lab reads the same table). */
typedef struct {
    uint8_t     channel;        /* hta_wrep_channel */
    const char *host_state;     /* what the host keeps while playing ("" none) */
    const char *replicated;     /* what reaches clients ("" nothing) */
    bool        static_known;   /* its placement is in the package: clients know it without the wire */
    bool        late_join;      /* a late joiner needs its current state */
    bool        interaction;    /* a player's press can reach it (found by the host's reach test) */
    bool        positioned;     /* a sound or damage can happen at it (the FX world sound names it) */
} hta_wrep_kind_info;

const hta_wrep_kind_info *hta_wrep_kind(uint8_t kind);   /* NULL for none / out of range */
const char *hta_wrep_channel_name(uint8_t channel);      /* "host-only", "spatial", "logical" */

typedef struct {
    uint16_t runtime_count;
    uint16_t spatial_count, flag_count;
    uint16_t index[HTA_WDEF_MAX_ENTITIES];   /* runtime -> its channel's index, HTA_WREP_NONE */
    uint16_t spatial[HTA_WREP_MAX_SPATIAL];  /* spatial index -> runtime */
    uint16_t flag[HTA_WREP_MAX_FLAGS];       /* flag index -> runtime */
} hta_wrep_map;

/* Build the mapping for `d`. False, with a message naming the limit, when
 * the world has more movers or relays than the wire can carry. */
bool hta_wrep_build(hta_wrep_map *m, const hta_world_defs *d, char *err, size_t errlen);
/* How many objects a world holds on each channel (no mapping kept). */
void hta_wrep_count(const hta_world_defs *d, uint32_t *spatial, uint32_t *flags, uint32_t *host_only);

#endif
