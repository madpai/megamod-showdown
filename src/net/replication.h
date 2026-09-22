#ifndef HTA_NET_REPLICATION_H
#define HTA_NET_REPLICATION_H
#include "protocol.h"

/* Render between consecutive authoritative snapshots. Position and facing
 * interpolate; discrete action/weapon flags use the newer snapshot. */
bool hta_net_interpolate(const hta_net_player *from, const hta_net_player *to,
                         float fraction, hta_net_player *out);
#endif
