#include "session.h"
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
    return true;
}

bool hta_net_server_open(hta_net_server *s, uint16_t port)
{
    if (!s) return false;
    memset(s,0,sizeof(*s)); s->udp.fd=-1;
    return hta_udp_open(&s->udp,port);
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
    if (p->type==HTA_NET_HELLO) {
        if (p->length!=4) { s->stats.invalid++; return; }
        uint32_t nonce=hta_net_u32_read(p->payload);
        if (!nonce) { s->stats.invalid++; return; }
        if (!peer) {
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
        if (!peer) return; /* full; never allocate from packets */
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
    case HTA_NET_EVENT: {
        hta_net_event e;
        if (p->length!=11 || !hta_net_event_unpack(p->payload+4,7,&e) ||
            e.actor!=peer->player.id) { s->stats.invalid++; break; }
        s->stats.events_in++;
        uint8_t payload[7]; hta_net_event_pack(payload,sizeof(payload),&e);
        for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++)
            if (s->peers[i].active && &s->peers[i]!=peer)
                send_packet(&s->udp,&s->peers[i].addr,&s->stats,HTA_NET_EVENT,
                            &s->sequence,s->tick,payload,7);
        break;
    }
    default: s->stats.invalid++; break;
    }
}

void hta_net_server_pump(hta_net_server *s, double now)
{
    if (!s || s->udp.fd<0) return;
    uint8_t wire[HTA_NET_MAX_PACKET+1]; hta_udp_addr from;
    for (unsigned i=0;i<64;i++) {
        int n=hta_udp_recv(&s->udp,wire,sizeof(wire),&from);
        if (n<0) break;
        s->stats.packets_in++; s->stats.bytes_in+=(unsigned)n;
        hta_net_packet p;
        if (!hta_net_unpack(wire,(size_t)n,&p)) { s->stats.invalid++; continue; }
        server_packet(s,&from,&p,now);
    }
    for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++)
        if (s->peers[i].active && now-s->peers[i].last_seen>10.0)
            s->peers[i].active=false;
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
    if (p->type==HTA_NET_WELCOME && p->length==9 &&
        hta_net_u32_read(p->payload+5)==c->nonce) {
        uint8_t id=p->payload[0];
        if (id>=1 && id<=HTA_NET_MAX_PLAYERS) {
            c->id=id; c->token=hta_net_u32_read(p->payload+1); c->connected=true;
        } else c->stats.invalid++;
        return;
    }
    if (!c->connected) { c->stats.invalid++; return; }
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
    case HTA_NET_EVENT: {
        hta_net_event e;
        if (!hta_net_event_unpack(p->payload,p->length,&e)) { c->stats.invalid++; break; }
        if (c->event_count<32) c->events[c->event_count++]=e;
        else c->stats.dropped++;
        c->stats.events_in++; break;
    }
    default: c->stats.invalid++; break;
    }
}

void hta_net_client_pump(hta_net_client *c, double now)
{
    if (!c || c->udp.fd<0) return;
    if (!c->connected && (c->last_hello==0 || now-c->last_hello>=0.25)) {
        uint8_t payload[4]; hta_net_u32_write(payload,c->nonce);
        send_packet(&c->udp,&c->server,&c->stats,HTA_NET_HELLO,&c->sequence,0,payload,4);
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
