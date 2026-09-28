/* X8: replication classification and mapping (world_repl.h). */
#include "world_repl.h"
#include <stdio.h>
#include <string.h>

static const hta_wrep_kind_info KIND[HTA_WDEF_KIND_COUNT] = {
    [HTA_WDEF_INTERACTABLE] = { HTA_WREP_HOST_ONLY, "cooldown", "", true, false, true, true },
    [HTA_WDEF_RELAY]        = { HTA_WREP_FLAG, "active", "active (one bit)", true, true, false, false },
    [HTA_WDEF_MOVER]        = { HTA_WREP_SPATIAL, "phase, progress", "phase, progress (1 byte at rest, 3 moving)", true, true, false, true },
    [HTA_WDEF_TRIGGER]      = { HTA_WREP_HOST_ONLY, "occupants", "", true, false, false, true },
    [HTA_WDEF_TELEPORT]     = { HTA_WREP_HOST_ONLY, "", "", true, false, false, true },
    [HTA_WDEF_PROP]         = { HTA_WREP_HOST_ONLY, "", "", true, false, false, true },
};

const hta_wrep_kind_info *hta_wrep_kind(uint8_t kind)
{
    return kind > HTA_WDEF_NONE && kind < HTA_WDEF_KIND_COUNT ? &KIND[kind] : NULL;
}

const char *hta_wrep_channel_name(uint8_t c)
{
    return c == HTA_WREP_SPATIAL ? "spatial" : c == HTA_WREP_FLAG ? "logical" : c == HTA_WREP_HOST_ONLY ? "host-only" : "?";
}

void hta_wrep_count(const hta_world_defs *d, uint32_t *spatial, uint32_t *flags, uint32_t *host_only)
{
    uint32_t n[HTA_WREP_CHANNEL_COUNT] = { 0 };
    for (uint32_t i = 0; d && i < d->count && i < HTA_WDEF_MAX_ENTITIES; i++) {
        const hta_wrep_kind_info *k = hta_wrep_kind(d->entity[i].kind);
        n[k ? k->channel : HTA_WREP_HOST_ONLY]++;
    }
    if (spatial) *spatial = n[HTA_WREP_SPATIAL];
    if (flags) *flags = n[HTA_WREP_FLAG];
    if (host_only) *host_only = n[HTA_WREP_HOST_ONLY];
}

bool hta_wrep_build(hta_wrep_map *m, const hta_world_defs *d, char *err, size_t errlen)
{
    if (!m) return false;
    memset(m, 0, sizeof(*m));
    memset(m->index, 0xFF, sizeof(m->index));
    if (!d) return true;
    if (d->count > HTA_WDEF_MAX_ENTITIES) {
        if (err && errlen) snprintf(err, errlen, "world has %u runtime objects, exceeds limit %u", d->count, HTA_WDEF_MAX_ENTITIES);
        return false;
    }
    uint32_t s, f;
    hta_wrep_count(d, &s, &f, NULL);
    if (s > HTA_WREP_MAX_SPATIAL) {
        if (err && errlen) snprintf(err, errlen, "world has %u movers, exceeds the spatial replication limit %u", s, HTA_WREP_MAX_SPATIAL);
        return false;
    }
    if (f > HTA_WREP_MAX_FLAGS) {
        if (err && errlen) snprintf(err, errlen, "world has %u relays, exceeds the logical state limit %u", f, HTA_WREP_MAX_FLAGS);
        return false;
    }
    m->runtime_count = (uint16_t)d->count;
    for (uint32_t i = 0; i < d->count; i++) {
        const hta_wrep_kind_info *k = hta_wrep_kind(d->entity[i].kind);
        if (!k || k->channel == HTA_WREP_HOST_ONLY) continue;
        if (k->channel == HTA_WREP_SPATIAL) { m->index[i] = m->spatial_count; m->spatial[m->spatial_count++] = (uint16_t)i; }
        else { m->index[i] = m->flag_count; m->flag[m->flag_count++] = (uint16_t)i; }
    }
    return true;
}
