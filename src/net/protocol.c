#include "protocol.h"

#include <math.h>
#include <string.h>

static void u16w(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static uint16_t u16r(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
void hta_net_u32_write(uint8_t *p, uint32_t v)
{ for (unsigned i=0; i<4; i++) p[i]=(uint8_t)(v>>(i*8)); }
uint32_t hta_net_u32_read(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static bool known(uint8_t t) { return t >= HTA_NET_HELLO && t <= HTA_NET_EVENT; }

bool hta_net_pack(uint8_t *dst, size_t cap, uint8_t type, uint32_t seq,
                  uint32_t tick, const uint8_t *payload, uint16_t len,
                  size_t *written)
{
    if (!dst || !known(type) || (len && !payload) ||
        (size_t)len + HTA_NET_HEADER > HTA_NET_MAX_PACKET ||
        cap < (size_t)len + HTA_NET_HEADER) return false;
    hta_net_u32_write(dst, HTA_NET_MAGIC);
    u16w(dst+4, HTA_NET_VERSION);
    dst[6]=type; dst[7]=0;
    hta_net_u32_write(dst+8, seq);
    hta_net_u32_write(dst+12, tick);
    u16w(dst+16, len); u16w(dst+18, 0);
    if (len) memcpy(dst+HTA_NET_HEADER, payload, len);
    if (written) *written=HTA_NET_HEADER+len;
    return true;
}

bool hta_net_unpack(const uint8_t *src, size_t len, hta_net_packet *out)
{
    if (!src || !out || len < HTA_NET_HEADER || len > HTA_NET_MAX_PACKET ||
        hta_net_u32_read(src) != HTA_NET_MAGIC ||
        u16r(src+4) != HTA_NET_VERSION || !known(src[6]) || src[7] ||
        u16r(src+18) || (size_t)u16r(src+16)+HTA_NET_HEADER != len) return false;
    out->type=src[6]; out->sequence=hta_net_u32_read(src+8);
    out->tick=hta_net_u32_read(src+12); out->length=u16r(src+16);
    out->payload=src+HTA_NET_HEADER;
    return true;
}

static void fw(uint8_t *p, float f) { uint32_t bits; memcpy(&bits,&f,4); hta_net_u32_write(p,bits); }
static float fr(const uint8_t *p) { uint32_t bits=hta_net_u32_read(p); float f; memcpy(&f,&bits,4); return f; }

bool hta_net_player_pack(uint8_t *dst, size_t cap, const hta_net_player *p)
{
    if (!dst || !p || cap < HTA_NET_PLAYER_BYTES || p->id == 0 ||
        p->id > HTA_NET_MAX_PLAYERS || p->weapon > 23 || p->flags & ~31u) return false;
    const float values[8]={p->pos[0],p->pos[1],p->pos[2],p->velocity[0],
                            p->velocity[1],p->velocity[2],p->yaw,p->pitch};
    for (unsigned i=0; i<8; i++) if (!isfinite(values[i]) || fabsf(values[i]) > 100000.0f) return false;
    dst[0]=p->id; dst[1]=p->weapon; dst[2]=p->flags;
    for (unsigned i=0; i<8; i++) fw(dst+3+i*4,values[i]);
    return true;
}

bool hta_net_player_unpack(const uint8_t *src, size_t len, hta_net_player *p)
{
    if (!src || !p || len != HTA_NET_PLAYER_BYTES) return false;
    hta_net_player tmp={0}; tmp.id=src[0]; tmp.weapon=src[1]; tmp.flags=src[2];
    for (unsigned i=0; i<3; i++) { tmp.pos[i]=fr(src+3+i*4); tmp.velocity[i]=fr(src+15+i*4); }
    tmp.yaw=fr(src+27); tmp.pitch=fr(src+31);
    uint8_t check[HTA_NET_PLAYER_BYTES];
    if (!hta_net_player_pack(check,sizeof(check),&tmp)) return false;
    *p=tmp; return true;
}

bool hta_net_event_pack(uint8_t *dst, size_t cap, const hta_net_event *e)
{
    if (!dst || !e || cap < 7 || e->actor < 1 || e->actor > HTA_NET_MAX_PLAYERS ||
        e->kind < HTA_NET_EVENT_FIRE || e->kind > HTA_NET_EVENT_WEAPON ||
        e->weapon > 23) return false;
    dst[0]=e->actor; dst[1]=e->kind; dst[2]=e->weapon;
    hta_net_u32_write(dst+3,e->event_id); return true;
}
bool hta_net_event_unpack(const uint8_t *src, size_t len, hta_net_event *e)
{
    if (!src || !e || len != 7) return false;
    hta_net_event tmp={src[0],src[1],src[2],hta_net_u32_read(src+3)};
    uint8_t check[7]; if (!hta_net_event_pack(check,sizeof(check),&tmp)) return false;
    *e=tmp; return true;
}
