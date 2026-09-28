/* X8: a LAN client applying the host's replicated world state
 * (WORLD_STATE, docs/WORLD_STATE.md) to its own copy of the world. Shared by
 * the phone (platform_android.c), the desktop joiner (net_view.h) and tests.
 * Portable C11, no allocation, no platform. */
#ifndef HTA_NET_WORLD_STATE_H
#define HTA_NET_WORLD_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "../net/session.h"
#include "../engine/world_entities.h"

/* X8: a client's hold on the host's replicated world state (WORLD_STATE,
 * docs/WORLD_STATE.md). The desktop joiner and the phone share it. */
typedef struct {
    uint32_t tick;                /* the last WORLD_STATE applied */
    bool     synced;              /* every replicated entry has arrived at least once */
    /* which entries have arrived since we joined: each one's first value
     * snaps (we were not there to see it change), later ones ease */
    uint8_t  seen_spatial[HTA_NET_WSTATE_MAX_SPATIAL / 8u];
    uint8_t  seen_flag[HTA_NET_WSTATE_MAX_FLAGS / 8u];
    uint32_t applied;             /* messages applied */
    uint32_t refused;             /* messages that do not describe this world */
    char     error[160];          /* why the last was refused */
} hta_net_wstate_sync;
void hta_net_wstate_reset(hta_net_wstate_sync *y);
/* The host's latest WORLD_STATE into `went`, which must be `remote`:
 * movers by spatial index, relays by flag index (w->rep). An entry's first
 * value snaps (we were not there), later ones are eased toward. A message
 * whose table sizes are not this world's is refused whole (`error` says
 * why). Returns how many entries it applied (0 when nothing new). */
uint32_t hta_net_wstate_apply(hta_net_wstate_sync *y, const hta_net_client *net, hta_world_entities *went);

#endif
