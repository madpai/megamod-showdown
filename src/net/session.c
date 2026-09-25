#include "session.h"
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

static uint32_t random_word(void)
{
    uint32_t x=0;
    FILE *f=fopen("/dev/urandom","rb");
    if (f) { (void)fread(&x,1,sizeof(x),f); fclose(f); }
    return x;
}

static bool send_packet(hta_udp *u, const hta_udp_addr *to, hta_net_stats *stats,
                        uint8_t type, uint32_t *seq, uint32_t tick,
                        const uint8_t *payload, uint16_t len)
{
    uint8_t wire[HTA_NET_MAX_PACKET]; size_t n=0;
    if (!hta_net_pack(wire,sizeof(wire),type,++*seq,tick,payload,len,&n) ||
        !hta_udp_send(u,to,wire,n)) return false;
    stats->packets_out++; stats->bytes_out+=n;
    if (type==HTA_NET_SNAPSHOT) stats->snapshots_out++;
    if (type==HTA_NET_EVENT) stats->events_out++;
    if (type==HTA_NET_WORLD) stats->worlds_out++;
    return true;
}

bool hta_net_server_open(hta_net_server *s, uint16_t port)
{
    return hta_net_server_open_bind(s,NULL,port);
}
bool hta_net_server_open_bind(hta_net_server *s, const char *ip, uint16_t port)
{
    if (!s) return false;
    memset(s,0,sizeof(*s)); s->udp.fd=-1;
    s->info.max_players=HTA_NET_MAX_PLAYERS;
    s->rate_per_s=HTA_NET_RATE_PER_S; s->rate_burst=HTA_NET_RATE_BURST;
    return hta_udp_open_bind(&s->udp,ip,port);
}
void hta_net_server_close(hta_net_server *s)
{ if (s) hta_udp_close(&s->udp); }
unsigned hta_net_server_count(const hta_net_server *s)
{
    unsigned n=0; if (!s) return 0;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) n+=s->peers[i].active;
    return n;
}

static hta_net_peer *find_peer(hta_net_server *s, const hta_udp_addr *from)
{
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++)
        if (s->peers[i].active && hta_udp_addr_equal(from,&s->peers[i].addr))
            return &s->peers[i];
    return NULL;
}

static void server_packet(hta_net_server *s, const hta_udp_addr *from,
                          const hta_net_packet *p, double now)
{
    hta_net_peer *peer=find_peer(s,from);
    if (p->type==HTA_NET_DISCOVER) {
        if (p->length!=4) { s->stats.invalid++; return; }
        hta_net_info info=s->info;
        if (!info.max_players || info.max_players>HTA_NET_MAX_PLAYERS)
            info.max_players=HTA_NET_MAX_PLAYERS;
        info.nonce=hta_net_u32_read(p->payload);
        info.players=(uint8_t)hta_net_server_count(s);
        if (info.players>info.max_players) info.players=info.max_players;
        uint8_t payload[HTA_NET_INFO_BYTES];
        if (hta_net_info_pack(payload,sizeof(payload),&info))
            send_packet(&s->udp,from,&s->stats,HTA_NET_INFO,&s->sequence,s->tick,
                        payload,HTA_NET_INFO_BYTES);
        return;
    }
    if (p->type==HTA_NET_HELLO) {
        if (p->length!=8) { s->stats.invalid++; return; }
        uint32_t nonce=hta_net_u32_read(p->payload);
        uint32_t map_crc=hta_net_u32_read(p->payload+4);
        if (!nonce) { s->stats.invalid++; return; }
        if (s->map_crc && map_crc && s->map_crc!=map_crc) {
            uint8_t reject[5]; hta_net_u32_write(reject,nonce);
            reject[4]=HTA_NET_REJECT_MAP;
            send_packet(&s->udp,from,&s->stats,HTA_NET_REJECT,
                        &s->sequence,s->tick,reject,sizeof(reject));
            return;
        }
        unsigned cap=s->info.max_players && s->info.max_players<HTA_NET_MAX_PLAYERS
                   ? s->info.max_players : HTA_NET_MAX_PLAYERS;
        if (!peer && hta_net_server_count(s)<cap) {
            for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) {
                if (!s->peers[i].active) {
                    peer=&s->peers[i]; memset(peer,0,sizeof(*peer));
                    peer->active=true; peer->addr=*from; peer->player.id=(uint8_t)(i+1);
                    peer->token=random_word(); peer->nonce=nonce;
                    if (!peer->token) { peer->active=false; return; }
                    break;
                }
            }
        }
        if (!peer) {
            uint8_t reject[5]; hta_net_u32_write(reject,nonce);
            reject[4]=HTA_NET_REJECT_FULL;
            send_packet(&s->udp,from,&s->stats,HTA_NET_REJECT,
                        &s->sequence,s->tick,reject,sizeof(reject));
            return;
        }
        if (peer->nonce!=nonce) { s->stats.invalid++; return; }
        peer->last_seen=now;
        uint8_t payload[9]={peer->player.id};
        hta_net_u32_write(payload+1,peer->token);
        hta_net_u32_write(payload+5,peer->nonce);
        send_packet(&s->udp,from,&s->stats,HTA_NET_WELCOME,&s->sequence,s->tick,payload,9);
        return;
    }
    if (!peer || p->length<4 || hta_net_u32_read(p->payload)!=peer->token) {
        s->stats.invalid++; return;
    }
    if (p->type!=HTA_NET_DISCONNECT && p->sequence<=peer->last_sequence) {
        s->stats.invalid++; return;
    }
    peer->last_sequence=p->sequence; peer->last_seen=now;
    switch (p->type) {
    case HTA_NET_DISCONNECT:
        if (p->length==4) peer->active=false; else s->stats.invalid++;
        break;
    case HTA_NET_PING:
        if (p->length==8)
            send_packet(&s->udp,from,&s->stats,HTA_NET_PONG,&s->sequence,s->tick,p->payload,8);
        else s->stats.invalid++;
        break;
    case HTA_NET_INPUT: {
        hta_net_player player;
        if (p->length!=4+HTA_NET_PLAYER_BYTES ||
            !hta_net_player_unpack(p->payload+4,HTA_NET_PLAYER_BYTES,&player) ||
            player.id!=peer->player.id) { s->stats.invalid++; break; }
        /* Provisional transform input. Server-owned collision/movement is the
         * next authority step; never accept health, damage or entity IDs. */
        peer->player=player;
        peer->has_state=true;
        break;
    }
    case HTA_NET_CONTROL: {
        hta_net_control control;
        if (p->length!=4+HTA_NET_CONTROL_BYTES ||
            !hta_net_control_unpack(p->payload+4,HTA_NET_CONTROL_BYTES,&control) ||
            control.id!=peer->player.id) { s->stats.invalid++; break; }
        peer->control=control; peer->has_control=true; peer->last_control_at=now;
        break;
    }
    case HTA_NET_EVENT: {
        hta_net_event e;
        if (p->length!=11 || !hta_net_event_unpack(p->payload+4,7,&e) ||
            e.actor!=peer->player.id) { s->stats.invalid++; break; }
        s->stats.events_in++;
        if (s->action_count<32) s->actions[s->action_count++]=e;
        else s->stats.dropped++;
        uint8_t payload[7]; hta_net_event_pack(payload,sizeof(payload),&e);
        for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++)
            if (s->peers[i].active && &s->peers[i]!=peer)
                send_packet(&s->udp,&s->peers[i].addr,&s->stats,HTA_NET_EVENT,
                            &s->sequence,s->tick,payload,7);
        break;
    }
    case HTA_NET_ACK: {
        if (p->length!=8) { s->stats.invalid++; break; }
        uint32_t id=hta_net_u32_read(p->payload+4);
        for (unsigned i=0;i<16;i++)
            if (peer->pending_kills[i].active &&
                peer->pending_kills[i].kill.id==id)
                peer->pending_kills[i].active=false;
        break;
    }
    default: s->stats.invalid++; break;
    }
}

/* One token from this packet's source, or false: over its rate. A new
 * source takes the slot of the one heard from longest ago (spoofed
 * addresses can churn the table; that only ever resets someone to a full
 * bucket, never locks them out). */
static bool rate_allow(hta_net_server *s, const hta_udp_addr *from, double now)
{
    if (s->rate_per_s<=0.0f) return true;
    uint32_t ip=0;
    if (from->addr.ss_family==AF_INET)
        ip=((const struct sockaddr_in *)&from->addr)->sin_addr.s_addr;
    hta_net_rate_source *r=NULL, *oldest=&s->rate[0];
    for (unsigned i=0;i<HTA_NET_RATE_SOURCES;i++) {
        hta_net_rate_source *c=&s->rate[i];
        if (c->used && c->ip==ip) { r=c; break; }
        if (!c->used || (oldest->used && c->last<oldest->last)) oldest=c;
    }
    if (!r) {
        r=oldest; r->used=true; r->ip=ip; r->tokens=s->rate_burst; r->last=now;
    }
    double dt=now-r->last;
    if (dt>0) {
        double t=r->tokens+dt*s->rate_per_s;
        r->tokens=(float)(t>s->rate_burst ? s->rate_burst : t);
        r->last=now;
    }
    if (r->tokens<1.0f) return false;
    r->tokens-=1.0f;
    return true;
}

void hta_net_server_pump(hta_net_server *s, double now)
{
    if (!s || s->udp.fd<0) return;
    uint8_t wire[HTA_NET_MAX_PACKET+1]; hta_udp_addr from;
    /* Up to 1024 a call: dropping a flood is cheap, so drain it rather
     * than let it sit in front of the players' packets. */
    for (unsigned i=0;i<1024;i++) {
        int n=hta_udp_recv(&s->udp,wire,sizeof(wire),&from);
        if (n<0) break;
        s->stats.packets_in++; s->stats.bytes_in+=(unsigned)n;
        if (!rate_allow(s,&from,now)) { s->stats.limited++; continue; }
        hta_net_packet p;
        if (!hta_net_unpack(wire,(size_t)n,&p)) { s->stats.invalid++; continue; }
        server_packet(s,&from,&p,now);
    }
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++)
        if (s->peers[i].active && now-s->peers[i].last_seen>10.0)
            s->peers[i].active=false;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) if (s->peers[i].active)
        for (unsigned k=0;k<16;k++) {
            hta_net_pending_kill *pending=&s->peers[i].pending_kills[k];
            if (!pending->active || now-pending->last_sent<0.15) continue;
            uint8_t payload[HTA_NET_KILL_BYTES];
            if (hta_net_kill_pack(payload,sizeof(payload),&pending->kill))
                send_packet(&s->udp,&s->peers[i].addr,&s->stats,HTA_NET_KILL,
                            &s->sequence,s->tick,payload,sizeof(payload));
            pending->last_sent=now;
        }
    if (now-s->last_snapshot<0.05) return;
    s->last_snapshot=now; s->tick++;
    uint8_t payload[1+HTA_NET_MAX_PLAYERS*HTA_NET_PLAYER_BYTES];
    unsigned count=0;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++)
        if (s->peers[i].active && s->peers[i].has_state &&
            hta_net_player_pack(payload+1+count*HTA_NET_PLAYER_BYTES,
              sizeof(payload)-1-count*HTA_NET_PLAYER_BYTES,&s->peers[i].player)) count++;
    payload[0]=(uint8_t)count;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) if (s->peers[i].active)
        send_packet(&s->udp,&s->peers[i].addr,&s->stats,HTA_NET_SNAPSHOT,
                    &s->sequence,s->tick,payload,(uint16_t)(1+count*HTA_NET_PLAYER_BYTES));
}

bool hta_net_server_world(hta_net_server *s, const hta_net_world *world)
{
    if (!s || !world || s->udp.fd<0 || s->last_world_tick==s->tick) return false;
    uint8_t payload[HTA_NET_MAX_PACKET-HTA_NET_HEADER]; size_t len=0;
    if (!hta_net_world_pack(payload,sizeof(payload),world,&len)) return false;
    s->last_world_tick=s->tick;
    bool sent=false;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) if (s->peers[i].active) {
        if (send_packet(&s->udp,&s->peers[i].addr,&s->stats,HTA_NET_WORLD,
                        &s->sequence,s->tick,payload,(uint16_t)len)) sent=true;
    }
    return sent;
}

bool hta_net_server_pop_action(hta_net_server *s, hta_net_event *action)
{
    if (!s || !action || !s->action_count) return false;
    *action=s->actions[0]; s->action_count--;
    memmove(s->actions,s->actions+1,s->action_count*sizeof(s->actions[0]));
    return true;
}

bool hta_net_server_kill(hta_net_server *s, const hta_net_kill *kill, double now)
{
    if (!s || !kill || s->udp.fd<0) return false;
    hta_net_kill event=*kill;
    event.id=++s->next_kill_id;
    if (!event.id) event.id=++s->next_kill_id;
    uint8_t payload[HTA_NET_KILL_BYTES];
    if (!hta_net_kill_pack(payload,sizeof(payload),&event)) return false;
    bool sent=false;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) if (s->peers[i].active) {
        hta_net_peer *p=&s->peers[i];
        unsigned slot=16;
        for (unsigned k=0;k<16;k++) if (!p->pending_kills[k].active) { slot=k; break; }
        if (slot==16) { slot=0; s->stats.dropped++; }
        p->pending_kills[slot]=(hta_net_pending_kill){true,event,now};
        if (send_packet(&s->udp,&p->addr,&s->stats,HTA_NET_KILL,
                        &s->sequence,s->tick,payload,sizeof(payload))) sent=true;
    }
    return sent;
}

bool hta_net_server_fx(hta_net_server *s, const hta_net_fx *fx)
{
    if (!s || !fx || s->udp.fd<0) return false;
    uint8_t payload[HTA_NET_FX_BYTES];
    if (!hta_net_fx_pack(payload,sizeof(payload),fx)) return false;
    bool sent=false;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) if (s->peers[i].active)
        if (send_packet(&s->udp,&s->peers[i].addr,&s->stats,HTA_NET_FX,
                        &s->sequence,s->tick,payload,sizeof(payload))) sent=true;
    return sent;
}

bool hta_net_server_projectiles(hta_net_server *s, const hta_net_projectiles *projectiles)
{
    if (!s || !projectiles || s->udp.fd<0 || s->last_projectile_tick==s->tick)
        return false;
    uint8_t payload[1+HTA_NET_MAX_PROJECTILES*HTA_NET_PROJECTILE_BYTES];
    size_t len=0;
    if (!hta_net_projectiles_pack(payload,sizeof(payload),projectiles,&len)) return false;
    s->last_projectile_tick=s->tick;
    bool sent=false;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) if (s->peers[i].active)
        if (send_packet(&s->udp,&s->peers[i].addr,&s->stats,HTA_NET_PROJECTILES,
                        &s->sequence,s->tick,payload,(uint16_t)len)) sent=true;
    return sent;
}

bool hta_net_server_vehicles(hta_net_server *s, const hta_net_vehicles *vehicles)
{
    if (!s || !vehicles || s->udp.fd<0 || s->last_vehicle_tick==s->tick) return false;
    uint8_t payload[1+HTA_NET_MAX_VEHICLES*HTA_NET_VEHICLE_BYTES];
    size_t len=0;
    if (!hta_net_vehicles_pack(payload,sizeof(payload),vehicles,&len)) return false;
    s->last_vehicle_tick=s->tick;
    bool sent=false;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) if (s->peers[i].active)
        if (send_packet(&s->udp,&s->peers[i].addr,&s->stats,HTA_NET_VEHICLES,
                        &s->sequence,s->tick,payload,(uint16_t)len)) sent=true;
    return sent;
}

bool hta_net_server_drops(hta_net_server *s, const hta_net_drops *drops)
{
    if (!s || !drops || s->udp.fd<0 || s->last_drop_tick==s->tick) return false;
    uint8_t payload[1+HTA_NET_MAX_DROPS*HTA_NET_DROP_BYTES];
    size_t len=0;
    if (!hta_net_drops_pack(payload,sizeof(payload),drops,&len)) return false;
    s->last_drop_tick=s->tick;
    bool sent=false;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) if (s->peers[i].active)
        if (send_packet(&s->udp,&s->peers[i].addr,&s->stats,HTA_NET_DROPS,
                        &s->sequence,s->tick,payload,(uint16_t)len)) sent=true;
    return sent;
}

bool hta_net_server_game(hta_net_server *s, const hta_net_game *game)
{
    if (!s || !game || s->udp.fd<0 || s->last_game_tick==s->tick) return false;
    uint8_t payload[HTA_NET_GAME_BYTES];
    if (!hta_net_game_pack(payload,sizeof(payload),game)) return false;
    s->last_game_tick=s->tick;
    bool sent=false;
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) if (s->peers[i].active)
        if (send_packet(&s->udp,&s->peers[i].addr,&s->stats,HTA_NET_GAME,
                        &s->sequence,s->tick,payload,(uint16_t)sizeof(payload))) sent=true;
    return sent;
}

bool hta_net_scan_open(hta_net_scan *s)
{
    if (!s) return false;
    memset(s,0,sizeof(*s)); s->udp.fd=-1;
    if (!hta_udp_open(&s->udp,0)) return false;
    hta_udp_broadcast(&s->udp);   /* unicast still works if this fails */
    s->nonce=random_word();
    if (!s->nonce) s->nonce=1;
    return true;
}
void hta_net_scan_close(hta_net_scan *s) { if (s) hta_udp_close(&s->udp); }
bool hta_net_scan_send(hta_net_scan *s, const char *ip, uint16_t port)
{
    hta_udp_addr to; hta_net_stats stats={0}; uint32_t seq=0;
    uint8_t payload[4];
    if (!s || !hta_udp_resolve(&to,ip,port)) return false;
    hta_net_u32_write(payload,s->nonce);
    return send_packet(&s->udp,&to,&stats,HTA_NET_DISCOVER,&seq,0,payload,4);
}
bool hta_net_scan_recv(hta_net_scan *s, hta_net_info *info, char ip[16], uint16_t *port)
{
    if (!s || !info) return false;
    uint8_t wire[HTA_NET_MAX_PACKET+1]; hta_udp_addr from;
    for (unsigned i=0;i<64;i++) {
        int n=hta_udp_recv(&s->udp,wire,sizeof(wire),&from);
        if (n<0) return false;
        hta_net_packet p;
        if (!hta_net_unpack(wire,(size_t)n,&p) || p.type!=HTA_NET_INFO ||
            !hta_net_info_unpack(p.payload,p.length,info) || info->nonce!=s->nonce) continue;
        if (ip && !hta_udp_addr_ip(&from,ip,port)) continue;
        return true;
    }
    return false;
}

bool hta_net_client_open(hta_net_client *c, const char *ip, uint16_t port)
{
    if (!c) return false;
    memset(c,0,sizeof(*c)); c->udp.fd=-1;
    if (!hta_udp_resolve(&c->server,ip,port) || !hta_udp_open(&c->udp,0)) return false;
    c->nonce=random_word();
    if (!c->nonce) { hta_udp_close(&c->udp); return false; }
    return true;
}
void hta_net_client_close(hta_net_client *c)
{
    if (!c) return;
    if (c->connected) {
        uint8_t payload[4]; hta_net_u32_write(payload,c->token);
        send_packet(&c->udp,&c->server,&c->stats,HTA_NET_DISCONNECT,&c->sequence,0,payload,4);
    }
    hta_udp_close(&c->udp); c->connected=false;
}

static void client_packet(hta_net_client *c, const hta_net_packet *p, double now)
{
    if (p->type==HTA_NET_REJECT && p->length==5 && !c->connected &&
        hta_net_u32_read(p->payload)==c->nonce &&
        (p->payload[4]==HTA_NET_REJECT_FULL || p->payload[4]==HTA_NET_REJECT_MAP)) {
        c->reject_reason=p->payload[4];
        c->last_receive=now;
        return;
    }
    if (p->type==HTA_NET_WELCOME && p->length==9 &&
        hta_net_u32_read(p->payload+5)==c->nonce) {
        uint8_t id=p->payload[0];
        if (id>=1 && id<=HTA_NET_MAX_PLAYERS) {
            c->id=id; c->token=hta_net_u32_read(p->payload+1); c->connected=true;
            c->reject_reason=0;
            c->last_receive=now;
        } else c->stats.invalid++;
        return;
    }
    if (!c->connected) { c->stats.invalid++; return; }
    c->last_receive=now;
    switch (p->type) {
    case HTA_NET_PONG:
        if (p->length==8 && hta_net_u32_read(p->payload)==c->token &&
            hta_net_u32_read(p->payload+4)==c->ping_nonce)
            c->stats.ping_ms=(now-c->ping_sent)*1000.0;
        else c->stats.invalid++;
        break;
    case HTA_NET_SNAPSHOT: {
        if (p->tick<=c->last_snapshot_tick) { c->stats.dropped++; break; }
        if (p->length<1 || p->payload[0]>HTA_NET_MAX_PLAYERS ||
            p->length!=1+p->payload[0]*HTA_NET_PLAYER_BYTES) {
            c->stats.invalid++; break;
        }
        hta_net_player players[HTA_NET_MAX_PLAYERS]; bool seen[HTA_NET_MAX_PLAYERS]={0};
        unsigned count=p->payload[0]; bool valid=true;
        for (unsigned i=0;i<count;i++) {
            if (!hta_net_player_unpack(p->payload+1+i*HTA_NET_PLAYER_BYTES,
                                       HTA_NET_PLAYER_BYTES,&players[i]) ||
                seen[players[i].id-1]) { valid=false; break; }
            seen[players[i].id-1]=true;
        }
        if (!valid) { c->stats.invalid++; break; }
        memset(c->present,0,sizeof(c->present));
        for (unsigned i=0;i<count;i++) {
            c->players[players[i].id-1]=players[i]; c->present[players[i].id-1]=true;
        }
        c->last_snapshot_tick=p->tick;
        c->stats.snapshots_in++; break;
    }
    case HTA_NET_WORLD: {
        if (p->tick<=c->last_world_tick) { c->stats.dropped++; break; }
        hta_net_world world;
        if (!hta_net_world_unpack(p->payload,p->length,&world)) {
            c->stats.invalid++; break;
        }
        c->world=world; c->have_world=true; c->last_world_tick=p->tick;
        c->stats.worlds_in++; break;
    }
    case HTA_NET_EVENT: {
        hta_net_event e;
        if (!hta_net_event_unpack(p->payload,p->length,&e)) { c->stats.invalid++; break; }
        if (c->event_count<32) c->events[c->event_count++]=e;
        else c->stats.dropped++;
        c->stats.events_in++; break;
    }
    case HTA_NET_KILL: {
        hta_net_kill kill;
        if (!hta_net_kill_unpack(p->payload,p->length,&kill)) {
            c->stats.invalid++; break;
        }
        bool seen=false;
        for (unsigned i=0;i<64;i++) if (c->seen_kills[i]==kill.id) { seen=true; break; }
        if (!seen && c->kill_count>=32) { c->stats.dropped++; break; }
        if (!seen) {
            c->kills[c->kill_count++]=kill;
            c->seen_kills[c->seen_kill_cursor++%64]=kill.id;
        }
        uint8_t ack[8]; hta_net_u32_write(ack,c->token);
        hta_net_u32_write(ack+4,kill.id);
        send_packet(&c->udp,&c->server,&c->stats,HTA_NET_ACK,
                    &c->sequence,0,ack,sizeof(ack));
        break;
    }
    case HTA_NET_FX: {
        hta_net_fx fx;
        if (!hta_net_fx_unpack(p->payload,p->length,&fx)) {
            c->stats.invalid++; break;
        }
        if (c->fx_count<64) c->fx[c->fx_count++]=fx;
        else c->stats.dropped++;
        break;
    }
    case HTA_NET_PROJECTILES: {
        if (p->tick<=c->last_projectile_tick) { c->stats.dropped++; break; }
        hta_net_projectiles proj;
        if (!hta_net_projectiles_unpack(p->payload,p->length,&proj)) {
            c->stats.invalid++; break;
        }
        c->projectiles=proj; c->have_projectiles=true;
        c->last_projectile_tick=p->tick;
        break;
    }
    case HTA_NET_VEHICLES: {
        if (p->tick<=c->last_vehicle_tick) { c->stats.dropped++; break; }
        hta_net_vehicles veh;
        if (!hta_net_vehicles_unpack(p->payload,p->length,&veh)) {
            c->stats.invalid++; break;
        }
        c->vehicles=veh; c->have_vehicles=true;
        c->last_vehicle_tick=p->tick;
        break;
    }
    case HTA_NET_DROPS: {
        if (p->tick<=c->last_drop_tick) { c->stats.dropped++; break; }
        hta_net_drops d;
        if (!hta_net_drops_unpack(p->payload,p->length,&d)) { c->stats.invalid++; break; }
        c->drops=d; c->have_drops=true; c->last_drop_tick=p->tick;
        break;
    }
    case HTA_NET_GAME: {
        if (p->tick<=c->last_game_tick) { c->stats.dropped++; break; }
        hta_net_game gm;
        if (!hta_net_game_unpack(p->payload,p->length,&gm)) { c->stats.invalid++; break; }
        c->game=gm; c->have_game=true; c->last_game_tick=p->tick;
        break;
    }
    default: c->stats.invalid++; break;
    }
}

void hta_net_client_pump(hta_net_client *c, double now)
{
    if (!c || c->udp.fd<0) return;
    if (c->connected && now-c->last_receive>10.0) {
        c->connected=false; c->id=0; c->token=0;
        c->last_snapshot_tick=0; c->event_count=0;
        c->last_world_tick=0; c->have_world=false;
        c->kill_count=0; c->seen_kill_cursor=0;
        c->fx_count=0; c->have_projectiles=false; c->last_projectile_tick=0;
        c->have_vehicles=false; c->last_vehicle_tick=0;
        c->have_drops=false; c->last_drop_tick=0;
        c->have_game=false; c->last_game_tick=0;
        memset(c->seen_kills,0,sizeof(c->seen_kills));
        memset(c->present,0,sizeof(c->present));
        uint32_t old_nonce=c->nonce;
        c->nonce=random_word();
        if (!c->nonce || c->nonce==old_nonce) c->nonce=old_nonce+1u;
        if (!c->nonce) c->nonce=1;
        c->last_hello=0; c->last_ping=0;
        c->stats.dropped++;
    }
    if (!c->connected && !c->reject_reason &&
        (c->last_hello==0 || now-c->last_hello>=0.25)) {
        uint8_t payload[8]; hta_net_u32_write(payload,c->nonce);
        hta_net_u32_write(payload+4,c->map_crc);
        send_packet(&c->udp,&c->server,&c->stats,HTA_NET_HELLO,&c->sequence,0,payload,8);
        c->last_hello=now;
    }
    if (c->connected && (c->last_ping==0 || now-c->last_ping>=1.0)) {
        uint8_t payload[8]; hta_net_u32_write(payload,c->token);
        c->ping_nonce++; hta_net_u32_write(payload+4,c->ping_nonce);
        send_packet(&c->udp,&c->server,&c->stats,HTA_NET_PING,&c->sequence,0,payload,8);
        c->ping_sent=now; c->last_ping=now;
    }
    uint8_t wire[HTA_NET_MAX_PACKET+1]; hta_udp_addr from;
    for (unsigned i=0;i<64;i++) {
        int n=hta_udp_recv(&c->udp,wire,sizeof(wire),&from);
        if (n<0) break;
        c->stats.packets_in++; c->stats.bytes_in+=(unsigned)n;
        hta_net_packet p;
        if (!hta_udp_addr_equal(&from,&c->server) ||
            !hta_net_unpack(wire,(size_t)n,&p)) { c->stats.invalid++; continue; }
        client_packet(c,&p,now);
    }
}

bool hta_net_client_state(hta_net_client *c, const hta_net_player *p)
{
    if (!c || !c->connected || !p || p->id!=c->id) return false;
    uint8_t payload[4+HTA_NET_PLAYER_BYTES]; hta_net_u32_write(payload,c->token);
    if (!hta_net_player_pack(payload+4,HTA_NET_PLAYER_BYTES,p)) return false;
    return send_packet(&c->udp,&c->server,&c->stats,HTA_NET_INPUT,
                       &c->sequence,0,payload,sizeof(payload));
}
bool hta_net_client_control(hta_net_client *c, const hta_net_control *control)
{
    if (!c || !c->connected || !control || control->id!=c->id) return false;
    uint8_t payload[4+HTA_NET_CONTROL_BYTES];
    hta_net_u32_write(payload,c->token);
    if (!hta_net_control_pack(payload+4,HTA_NET_CONTROL_BYTES,control)) return false;
    return send_packet(&c->udp,&c->server,&c->stats,HTA_NET_CONTROL,
                       &c->sequence,0,payload,sizeof(payload));
}
bool hta_net_client_event(hta_net_client *c, const hta_net_event *e)
{
    if (!c || !c->connected || !e || e->actor!=c->id) return false;
    uint8_t payload[11]; hta_net_u32_write(payload,c->token);
    if (!hta_net_event_pack(payload+4,7,e)) return false;
    return send_packet(&c->udp,&c->server,&c->stats,HTA_NET_EVENT,
                       &c->sequence,0,payload,sizeof(payload));
}
bool hta_net_client_pop_event(hta_net_client *c, hta_net_event *e)
{
    if (!c || !e || !c->event_count) return false;
    *e=c->events[0]; c->event_count--;
    memmove(c->events,c->events+1,c->event_count*sizeof(c->events[0])); return true;
}
bool hta_net_client_pop_kill(hta_net_client *c, hta_net_kill *kill)
{
    if (!c || !kill || !c->kill_count) return false;
    *kill=c->kills[0]; c->kill_count--;
    memmove(c->kills,c->kills+1,c->kill_count*sizeof(c->kills[0]));
    return true;
}
bool hta_net_client_pop_fx(hta_net_client *c, hta_net_fx *fx)
{
    if (!c || !fx || !c->fx_count) return false;
    *fx=c->fx[0]; c->fx_count--;
    memmove(c->fx,c->fx+1,c->fx_count*sizeof(c->fx[0]));
    return true;
}
