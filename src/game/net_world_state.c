/* X8: applying the host's WORLD_STATE (net_world_state.h). */
#include "net_world_state.h"
#include <stdio.h>
#include <string.h>

void hta_net_wstate_reset(hta_net_wstate_sync *y)
{
    if (!y) return;
    uint32_t refused = y->refused;
    memset(y, 0, sizeof(*y));
    y->refused = refused;
}

uint32_t hta_net_wstate_apply(hta_net_wstate_sync *y, const hta_net_client *net, hta_world_entities *went)
{
    if (!y || !net || !went || !went->loaded) return 0;
    if (!net->connected) { if (y->tick || y->synced) hta_net_wstate_reset(y); return 0; }
    if (!net->have_world_state || net->last_world_state_tick == y->tick) return 0;
    y->tick = net->last_world_state_tick;
    const hta_net_world_state *ws = &net->world_state;
    const hta_wrep_map *rep = &went->rep;
    if (ws->spatial_total != rep->spatial_count || ws->flag_total != rep->flag_count) {
        y->refused++;
        snprintf(y->error, sizeof(y->error), "WORLD_STATE describes %u movers and %u logical flags; this world has %u and %u",
                 ws->spatial_total, ws->flag_total, rep->spatial_count, rep->flag_count);
        return 0;
    }
    uint32_t n = 0;
    for (uint32_t k = 0; k < ws->spatial_total; k++) {
        if (!hta_net_bit(ws->spatial_has, k)) continue;
        hta_went_mover_state m = { rep->spatial[k], ws->phase[k], ws->t[k] };
        n += hta_went_apply(went, &m, !hta_net_bit(y->seen_spatial, k));
        hta_net_bit_set(y->seen_spatial, k, true);
    }
    for (uint32_t k = 0; k < ws->flag_total; k++) {
        if (!hta_net_bit(ws->flag_has, k)) continue;
        n += hta_went_apply_flag(went, k, hta_net_bit(ws->flag, k));
        hta_net_bit_set(y->seen_flag, k, true);
    }
    bool all = true;
    for (uint32_t k = 0; all && k < rep->spatial_count; k++) all = hta_net_bit(y->seen_spatial, k);
    for (uint32_t k = 0; all && k < rep->flag_count; k++) all = hta_net_bit(y->seen_flag, k);
    y->synced = all;
    y->applied++;
    return n;
}
