#include "protocol.h"

#include <math.h>
#include <string.h>

static void u16w(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static uint16_t u16r(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
void hta_net_u32_write(uint8_t *p, uint32_t v)
{ for (unsigned i=0; i<4; i++) p[i]=(uint8_t)(v>>(i*8)); }
uint32_t hta_net_u32_read(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static bool known(uint8_t t) { return t >= HTA_NET_HELLO && t <= HTA_NET_DROPS; }

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

bool hta_net_info_pack(uint8_t *dst, size_t cap, const hta_net_info *i)
{
    if (!dst || !i || cap < HTA_NET_INFO_BYTES || !i->max_players ||
        i->max_players > HTA_NET_MAX_PLAYERS || i->players > i->max_players) return false;
    hta_net_u32_write(dst,i->nonce);
    dst[4]=i->players; dst[5]=i->max_players; dst[6]=i->score_limit; dst[7]=i->time_limit;
    memset(dst+8,0,HTA_NET_NAME);
    for (unsigned k=0; k<HTA_NET_NAME-1 && i->name[k]; k++) {
        char ch=i->name[k];
        dst[8+k]=(uint8_t)(ch>=32 && ch<127 ? ch : '?');
    }
    return true;
}
bool hta_net_info_unpack(const uint8_t *src, size_t len, hta_net_info *i)
{
    if (!src || !i || len != HTA_NET_INFO_BYTES || src[8+HTA_NET_NAME-1]) return false;
    hta_net_info tmp={0};
    tmp.nonce=hta_net_u32_read(src);
    tmp.players=src[4]; tmp.max_players=src[5]; tmp.score_limit=src[6]; tmp.time_limit=src[7];
    for (unsigned k=0; k<HTA_NET_NAME; k++) {
        uint8_t ch=src[8+k];
        if (!ch) break;
        if (ch<32 || ch>126) return false;
        tmp.name[k]=(char)ch;
    }
    uint8_t check[HTA_NET_INFO_BYTES];
    if (!hta_net_info_pack(check,sizeof(check),&tmp)) return false;
    *i=tmp; return true;
}

bool hta_net_world_pack(uint8_t *dst, size_t cap, const hta_net_world *w, size_t *written)
{
    if (!dst || !w || w->count>HTA_NET_MAX_ENTITIES || w->bot_count>7 ||
        w->item_count>64 ||
        w->over>1 || (w->winner!=255 && w->winner>=HTA_NET_MAX_ENTITIES) ||
        !isfinite(w->time) || w->time<0.0f || w->time>100000.0f) return false;
    size_t n=HTA_NET_WORLD_HEADER+(size_t)w->count*HTA_NET_ENTITY_BYTES;
    if (cap<n || n+HTA_NET_HEADER>HTA_NET_MAX_PACKET) return false;
    fw(dst,w->time); u16w(dst+4,w->round);
    dst[6]=w->count; dst[7]=w->bot_count; dst[8]=w->over;
    dst[9]=w->winner; dst[10]=w->score_limit;
    dst[11]=w->time_limit; dst[12]=w->respawn_time;
    dst[13]=w->item_count;
    memcpy(dst+14,w->item_present,8);
    memcpy(dst+22,w->item_choice,64);
    bool seen[HTA_NET_MAX_ENTITIES]={0};
    for (uint8_t i=0;i<w->count;i++) {
        const hta_net_entity *e=&w->entities[i];
        if (e->id>=HTA_NET_MAX_ENTITIES || seen[e->id] ||
            e->kind<HTA_NET_ENTITY_PLAYER || e->kind>HTA_NET_ENTITY_BOT ||
            e->flags & ~63u || (e->weapon!=255 && e->weapon>23) ||
            e->peer_id>HTA_NET_MAX_PLAYERS || e->slot>1 ||
            (e->carry[0]!=255 && e->carry[0]>23) ||
            (e->carry[1]!=255 && e->carry[1]>23) ||
            (e->kind==HTA_NET_ENTITY_BOT && e->peer_id)) return false;
        seen[e->id]=true;
        const float values[9]={e->pos[0],e->pos[1],e->pos[2],
            e->velocity[0],e->velocity[1],e->yaw,e->pitch,e->health,e->shield};
        for (unsigned k=0;k<9;k++)
            if (!isfinite(values[k]) || fabsf(values[k])>100000.0f) return false;
        size_t namelen=0;
        while (namelen<HTA_NET_ENTITY_NAME && e->name[namelen]) {
            if ((unsigned char)e->name[namelen]<32 || (unsigned char)e->name[namelen]>126)
                return false;
            namelen++;
        }
        if (!namelen || namelen==HTA_NET_ENTITY_NAME) return false;
        uint8_t *p=dst+HTA_NET_WORLD_HEADER+(size_t)i*HTA_NET_ENTITY_BYTES;
        p[0]=e->id; p[1]=e->kind; p[2]=e->flags; p[3]=e->weapon; p[4]=e->peer_id;
        for (unsigned k=0;k<9;k++) fw(p+5+k*4,values[k]);
        u16w(p+41,(uint16_t)e->score);
        u16w(p+43,(uint16_t)e->kills);
        u16w(p+45,(uint16_t)e->deaths);
        memset(p+47,0,HTA_NET_ENTITY_NAME);
        memcpy(p+47,e->name,namelen);
        p[59]=e->carry[0]; p[60]=e->carry[1];
        p[61]=e->slot; p[62]=e->grenades; p[63]=e->powerup;
        u16w(p+64,e->ammo_loaded); u16w(p+66,e->ammo_reserve);
    }
    if (written) *written=n;
    return true;
}

bool hta_net_world_unpack(const uint8_t *src, size_t len, hta_net_world *w)
{
    if (!src || !w || len<HTA_NET_WORLD_HEADER || src[6]>HTA_NET_MAX_ENTITIES ||
        len!=HTA_NET_WORLD_HEADER+(size_t)src[6]*HTA_NET_ENTITY_BYTES) return false;
    hta_net_world tmp={0};
    tmp.time=fr(src); tmp.round=u16r(src+4); tmp.count=src[6];
    tmp.bot_count=src[7]; tmp.over=src[8]; tmp.winner=src[9];
    tmp.score_limit=src[10]; tmp.time_limit=src[11]; tmp.respawn_time=src[12];
    tmp.item_count=src[13];
    memcpy(tmp.item_present,src+14,8);
    memcpy(tmp.item_choice,src+22,64);
    for (uint8_t i=0;i<tmp.count;i++) {
        const uint8_t *p=src+HTA_NET_WORLD_HEADER+(size_t)i*HTA_NET_ENTITY_BYTES;
        hta_net_entity *e=&tmp.entities[i];
        e->id=p[0]; e->kind=p[1]; e->flags=p[2]; e->weapon=p[3]; e->peer_id=p[4];
        for (unsigned k=0;k<3;k++) e->pos[k]=fr(p+5+k*4);
        for (unsigned k=0;k<2;k++) e->velocity[k]=fr(p+17+k*4);
        e->yaw=fr(p+25); e->pitch=fr(p+29);
        e->health=fr(p+33); e->shield=fr(p+37);
        e->score=(int16_t)u16r(p+41);
        e->kills=(int16_t)u16r(p+43);
        e->deaths=(int16_t)u16r(p+45);
        memcpy(e->name,p+47,HTA_NET_ENTITY_NAME);
        e->carry[0]=p[59]; e->carry[1]=p[60];
        e->slot=p[61]; e->grenades=p[62]; e->powerup=p[63];
        e->ammo_loaded=u16r(p+64); e->ammo_reserve=u16r(p+66);
    }
    uint8_t check[HTA_NET_MAX_PACKET]; size_t written=0;
    if (!hta_net_world_pack(check,sizeof(check),&tmp,&written) ||
        written!=len || memcmp(check,src,len)) return false;
    *w=tmp; return true;
}

bool hta_net_control_pack(uint8_t *dst, size_t cap, const hta_net_control *c)
{
    if (!dst || !c || cap<HTA_NET_CONTROL_BYTES || !c->id ||
        c->id>HTA_NET_MAX_PLAYERS || c->flags & ~15u || c->weapon_slot>1 ||
        !isfinite(c->forward) || fabsf(c->forward)>1.0f ||
        !isfinite(c->right) || fabsf(c->right)>1.0f ||
        !isfinite(c->yaw) || fabsf(c->yaw)>1000.0f ||
        !isfinite(c->pitch) || fabsf(c->pitch)>3.2f) return false;
    dst[0]=c->id; dst[1]=c->flags; dst[2]=c->weapon_slot;
    fw(dst+3,c->forward); fw(dst+7,c->right);
    fw(dst+11,c->yaw); fw(dst+15,c->pitch);
    u16w(dst+19,c->melee_count); u16w(dst+21,c->grenade_count);
    u16w(dst+23,c->reload_count); u16w(dst+25,c->pickup_count);
    u16w(dst+27,c->action_count);
    return true;
}

bool hta_net_control_unpack(const uint8_t *src, size_t len, hta_net_control *c)
{
    if (!src || !c || len!=HTA_NET_CONTROL_BYTES) return false;
    hta_net_control tmp={src[0],src[1],src[2],fr(src+3),fr(src+7),
        fr(src+11),fr(src+15),u16r(src+19),u16r(src+21),u16r(src+23),u16r(src+25),
        u16r(src+27)};
    uint8_t check[HTA_NET_CONTROL_BYTES];
    if (!hta_net_control_pack(check,sizeof(check),&tmp)) return false;
    *c=tmp; return true;
}

bool hta_net_kill_pack(uint8_t *dst, size_t cap, const hta_net_kill *k)
{
    if (!dst || !k || cap<HTA_NET_KILL_BYTES || !k->id ||
        k->victim>=HTA_NET_MAX_ENTITIES ||
        (k->killer!=255 && k->killer>=HTA_NET_MAX_ENTITIES)) return false;
    size_t n=0;
    while (n<sizeof(k->text) && k->text[n]) {
        if ((unsigned char)k->text[n]<32 || (unsigned char)k->text[n]>126) return false;
        n++;
    }
    if (!n || n==sizeof(k->text)) return false;
    hta_net_u32_write(dst,k->id);
    dst[4]=k->victim; dst[5]=k->killer;
    memset(dst+6,0,96); memcpy(dst+6,k->text,n);
    return true;
}
bool hta_net_kill_unpack(const uint8_t *src, size_t len, hta_net_kill *k)
{
    if (!src || !k || len!=HTA_NET_KILL_BYTES) return false;
    hta_net_kill tmp={0};
    tmp.id=hta_net_u32_read(src); tmp.victim=src[4]; tmp.killer=src[5];
    memcpy(tmp.text,src+6,96);
    uint8_t check[HTA_NET_KILL_BYTES];
    if (!hta_net_kill_pack(check,sizeof(check),&tmp) || memcmp(check,src,len)) return false;
    *k=tmp; return true;
}

bool hta_net_fx_pack(uint8_t *dst, size_t cap, const hta_net_fx *fx)
{
    if (!dst || !fx || cap<HTA_NET_FX_BYTES ||
        fx->kind<HTA_NET_FX_FIRE || fx->kind>HTA_NET_FX_DETONATE ||
        (fx->entity!=255 && fx->entity>=HTA_NET_MAX_ENTITIES) ||
        fx->weapon>=HTA_NET_MAX_WEAPONS) return false;
    dst[0]=fx->kind; dst[1]=fx->entity;
    dst[2]=fx->weapon; dst[3]=fx->material;
    for (unsigned i=0;i<3;i++) {
        if (!isfinite(fx->pos[i]) || fabsf(fx->pos[i])>100000.0f ||
            !isfinite(fx->dir[i]) || fabsf(fx->dir[i])>100000.0f) return false;
        fw(dst+4+i*4,fx->pos[i]); fw(dst+16+i*4,fx->dir[i]);
    }
    return true;
}
bool hta_net_fx_unpack(const uint8_t *src, size_t len, hta_net_fx *fx)
{
    if (!src || !fx || len!=HTA_NET_FX_BYTES) return false;
    hta_net_fx tmp={0};
    tmp.kind=src[0]; tmp.entity=src[1]; tmp.weapon=src[2]; tmp.material=src[3];
    for (unsigned i=0;i<3;i++) {
        tmp.pos[i]=fr(src+4+i*4); tmp.dir[i]=fr(src+16+i*4);
    }
    uint8_t check[HTA_NET_FX_BYTES];
    if (!hta_net_fx_pack(check,sizeof(check),&tmp)) return false;
    *fx=tmp; return true;
}

bool hta_net_projectiles_pack(uint8_t *dst, size_t cap, const hta_net_projectiles *p,
                              size_t *written)
{
    if (!dst || !p || p->count>HTA_NET_MAX_PROJECTILES ||
        cap<1u+(size_t)p->count*HTA_NET_PROJECTILE_BYTES) return false;
    dst[0]=p->count;
    for (uint8_t i=0;i<p->count;i++) {
        const hta_net_projectile *q=&p->live[i];
        /* Match pools first; HTA_NET_POOL_HOST_* carry the host's
         * first-person weapon projectiles and grenades. */
        if (q->pool>=HTA_NET_MAX_POOLS || q->slot>7 || !isfinite(q->speed) ||
            q->speed<0.0f || q->speed>100000.0f) return false;
        uint8_t *out=dst+1u+(size_t)i*HTA_NET_PROJECTILE_BYTES;
        out[0]=q->pool; out[1]=q->slot;
        for (unsigned k=0;k<3;k++) {
            if (!isfinite(q->pos[k]) || fabsf(q->pos[k])>100000.0f ||
                !isfinite(q->dir[k]) || fabsf(q->dir[k])>100000.0f) return false;
            fw(out+2+k*4,q->pos[k]); fw(out+14+k*4,q->dir[k]);
        }
        fw(out+26,q->speed);
    }
    if (written) *written=1u+(size_t)p->count*HTA_NET_PROJECTILE_BYTES;
    return true;
}
bool hta_net_projectiles_unpack(const uint8_t *src, size_t len, hta_net_projectiles *p)
{
    if (!src || !p || len<1 || src[0]>HTA_NET_MAX_PROJECTILES ||
        len!=1u+(size_t)src[0]*HTA_NET_PROJECTILE_BYTES) return false;
    hta_net_projectiles tmp={0}; tmp.count=src[0];
    for (uint8_t i=0;i<tmp.count;i++) {
        const uint8_t *in=src+1u+(size_t)i*HTA_NET_PROJECTILE_BYTES;
        hta_net_projectile *q=&tmp.live[i];
        q->pool=in[0]; q->slot=in[1];
        for (unsigned k=0;k<3;k++) {
            q->pos[k]=fr(in+2+k*4); q->dir[k]=fr(in+14+k*4);
        }
        q->speed=fr(in+26);
    }
    uint8_t check[1+HTA_NET_MAX_PROJECTILES*HTA_NET_PROJECTILE_BYTES];
    size_t written=0;
    if (!hta_net_projectiles_pack(check,sizeof(check),&tmp,&written) ||
        written!=len || memcmp(check,src,len)) return false;
    *p=tmp; return true;
}

/* ---- vehicles -------------------------------------------------------- */

static bool q16(float v, float scale, int16_t *out)
{
    if (!isfinite(v)) return false;
    float q = roundf(v * scale);
    if (q < -32767.0f || q > 32767.0f) return false;
    *out = (int16_t)q;
    return true;
}
static float wrapf(float a)
{
    const float PI = 3.14159265f;
    if (!isfinite(a)) return a;
    a = fmodf(a + PI, 2.0f * PI);
    if (a < 0.0f) a += 2.0f * PI;
    return a - PI;
}
#define VQ_POS 100.0f
#define VQ_ANGLE 10000.0f
#define VQ_SPEED 100.0f
#define VQ_TRAVEL 500.0f

bool hta_net_vehicles_pack(uint8_t *dst, size_t cap, const hta_net_vehicles *v,
                           size_t *written)
{
    if (!dst || !v || v->count>HTA_NET_MAX_VEHICLES ||
        cap<1u+(size_t)v->count*HTA_NET_VEHICLE_BYTES) return false;
    dst[0]=v->count;
    for (uint8_t i=0;i<v->count;i++) {
        const hta_net_vehicle *c=&v->cars[i];
        uint8_t *o=dst+1u+(size_t)i*HTA_NET_VEHICLE_BYTES;
        if (c->index>=HTA_NET_MAX_VEHICLES || c->flags & ~7u) return false;
        o[0]=c->index; o[1]=c->flags;
        int16_t q;
        for (unsigned k=0;k<3;k++) {
            if (!q16(c->pos[k],VQ_POS,&q)) return false;
            u16w(o+2+k*2,(uint16_t)q);
        }
        const float angles[8]={c->yaw,c->pitch,c->roll,c->aim_yaw,c->aim_pitch,
                               c->steering,c->wheel_spin,c->barrel_spin};
        for (unsigned k=0;k<8;k++) {
            if (!q16(wrapf(angles[k]),VQ_ANGLE,&q)) return false;
            u16w(o+8+k*2,(uint16_t)q);
        }
        if (!q16(c->speed,VQ_SPEED,&q)) return false;
        u16w(o+24,(uint16_t)q);
        for (unsigned k=0;k<HTA_NET_VEHICLE_SEATS;k++) {
            if (c->occupant[k]!=255 && c->occupant[k]>=HTA_NET_MAX_ENTITIES) return false;
            o[26+k]=c->occupant[k];
        }
        for (unsigned k=0;k<4;k++) {
            float t=roundf(c->travel[k]*VQ_TRAVEL);
            if (!isfinite(t) || t<-127.0f || t>127.0f) return false;
            o[32+k]=(uint8_t)(int8_t)t;
        }
    }
    if (written) *written=1u+(size_t)v->count*HTA_NET_VEHICLE_BYTES;
    return true;
}

bool hta_net_vehicles_unpack(const uint8_t *src, size_t len, hta_net_vehicles *v)
{
    if (!src || !v || len<1 || src[0]>HTA_NET_MAX_VEHICLES ||
        len!=1u+(size_t)src[0]*HTA_NET_VEHICLE_BYTES) return false;
    hta_net_vehicles tmp;
    memset(&tmp,0,sizeof(tmp));
    tmp.count=src[0];
    for (uint8_t i=0;i<tmp.count;i++) {
        const uint8_t *in=src+1u+(size_t)i*HTA_NET_VEHICLE_BYTES;
        hta_net_vehicle *c=&tmp.cars[i];
        c->index=in[0]; c->flags=in[1];
        for (unsigned k=0;k<3;k++) c->pos[k]=(float)(int16_t)u16r(in+2+k*2)/VQ_POS;
        float angles[8];
        for (unsigned k=0;k<8;k++) angles[k]=(float)(int16_t)u16r(in+8+k*2)/VQ_ANGLE;
        c->yaw=angles[0]; c->pitch=angles[1]; c->roll=angles[2];
        c->aim_yaw=angles[3]; c->aim_pitch=angles[4]; c->steering=angles[5];
        c->wheel_spin=angles[6]; c->barrel_spin=angles[7];
        c->speed=(float)(int16_t)u16r(in+24)/VQ_SPEED;
        for (unsigned k=0;k<HTA_NET_VEHICLE_SEATS;k++) c->occupant[k]=in[26+k];
        for (unsigned k=0;k<4;k++) c->travel[k]=(float)(int8_t)in[32+k]/VQ_TRAVEL;
    }
    uint8_t check[1+HTA_NET_MAX_VEHICLES*HTA_NET_VEHICLE_BYTES];
    size_t written=0;
    if (!hta_net_vehicles_pack(check,sizeof(check),&tmp,&written) ||
        written!=len || memcmp(check,src,len)) return false;
    *v=tmp; return true;
}

/* ---- drops ------------------------------------------------------------ */

bool hta_net_drops_pack(uint8_t *dst, size_t cap, const hta_net_drops *d, size_t *written)
{
    if (!dst || !d || d->count>HTA_NET_MAX_DROPS ||
        cap<1u+(size_t)d->count*HTA_NET_DROP_BYTES) return false;
    dst[0]=d->count;
    for (uint8_t i=0;i<d->count;i++) {
        uint8_t *o=dst+1u+(size_t)i*HTA_NET_DROP_BYTES;
        if (d->drop[i].weapon>=HTA_NET_MAX_WEAPONS) return false;
        o[0]=d->drop[i].weapon;
        int16_t q;
        for (unsigned k=0;k<3;k++) {
            if (!q16(d->drop[i].pos[k],VQ_POS,&q)) return false;
            u16w(o+1+k*2,(uint16_t)q);
        }
        if (!q16(wrapf(d->drop[i].yaw),VQ_ANGLE,&q)) return false;
        u16w(o+7,(uint16_t)q);
    }
    if (written) *written=1u+(size_t)d->count*HTA_NET_DROP_BYTES;
    return true;
}

bool hta_net_drops_unpack(const uint8_t *src, size_t len, hta_net_drops *d)
{
    if (!src || !d || len<1 || src[0]>HTA_NET_MAX_DROPS ||
        len!=1u+(size_t)src[0]*HTA_NET_DROP_BYTES) return false;
    hta_net_drops tmp;
    memset(&tmp,0,sizeof(tmp));
    tmp.count=src[0];
    for (uint8_t i=0;i<tmp.count;i++) {
        const uint8_t *in=src+1u+(size_t)i*HTA_NET_DROP_BYTES;
        tmp.drop[i].weapon=in[0];
        for (unsigned k=0;k<3;k++) tmp.drop[i].pos[k]=(float)(int16_t)u16r(in+1+k*2)/VQ_POS;
        tmp.drop[i].yaw=(float)(int16_t)u16r(in+7)/VQ_ANGLE;
    }
    uint8_t check[1+HTA_NET_MAX_DROPS*HTA_NET_DROP_BYTES];
    size_t written=0;
    if (!hta_net_drops_pack(check,sizeof(check),&tmp,&written) ||
        written!=len || memcmp(check,src,len)) return false;
    *d=tmp; return true;
}
