#include "replication.h"
#include <math.h>

bool hta_net_interpolate(const hta_net_player *from, const hta_net_player *to,
                         float fraction, hta_net_player *out)
{
    if (!from || !to || !out || from->id!=to->id || !isfinite(fraction)) return false;
    if (fraction<0) fraction=0;
    if (fraction>1) fraction=1;
    hta_net_player result=*to;
    for (int k=0;k<3;k++) {
        result.pos[k]=from->pos[k]+(to->pos[k]-from->pos[k])*fraction;
        result.velocity[k]=from->velocity[k]+(to->velocity[k]-from->velocity[k])*fraction;
    }
    result.yaw=from->yaw+remainderf(to->yaw-from->yaw,6.28318530718f)*fraction;
    result.pitch=from->pitch+(to->pitch-from->pitch)*fraction;
    *out=result; return true;
}
