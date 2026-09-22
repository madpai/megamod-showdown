#include "net/protocol.h"
#include "net/session.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void codec(void)
{
    uint8_t payload[4]={1,2,3,4}, wire[HTA_NET_MAX_PACKET]; size_t n=0;
    hta_net_packet p;
    assert(hta_net_pack(wire,sizeof(wire),HTA_NET_HELLO,7,9,payload,4,&n));
    assert(n==24 && hta_net_unpack(wire,n,&p));
    assert(p.type==HTA_NET_HELLO && p.sequence==7 && p.tick==9 && p.length==4);
    assert(memcmp(p.payload,payload,4)==0);
    assert(!hta_net_unpack(wire,n-1,&p));
    wire[4]=2; assert(!hta_net_unpack(wire,n,&p)); wire[4]=1;
    wire[6]=250; assert(!hta_net_unpack(wire,n,&p)); wire[6]=HTA_NET_HELLO;
    wire[7]=1; assert(!hta_net_unpack(wire,n,&p)); wire[7]=0;
    wire[16]=255; assert(!hta_net_unpack(wire,n,&p)); wire[16]=4;
    wire[0]=0; assert(!hta_net_unpack(wire,n,&p));
    assert(!hta_net_pack(wire,23,HTA_NET_HELLO,0,0,payload,4,NULL));

    hta_net_player a={.id=2,.weapon=4,.flags=HTA_NET_GROUNDED|HTA_NET_CROUCH,
                      .pos={12.5f,-7.25f,3},.velocity={1,2,3},.yaw=1.5f,.pitch=-0.5f}, b;
    assert(hta_net_player_pack(wire,sizeof(wire),&a));
    assert(hta_net_player_unpack(wire,HTA_NET_PLAYER_BYTES,&b));
    assert(b.id==2 && b.weapon==4 && b.pos[0]==12.5f && b.yaw==1.5f);
    assert(!hta_net_player_unpack(wire,HTA_NET_PLAYER_BYTES-1,&b));
    wire[0]=9; assert(!hta_net_player_unpack(wire,HTA_NET_PLAYER_BYTES,&b)); wire[0]=2;
    wire[3]=0;wire[4]=0;wire[5]=0xc0;wire[6]=0x7f; /* NaN */
    assert(!hta_net_player_unpack(wire,HTA_NET_PLAYER_BYTES,&b));
    hta_net_event e={2,HTA_NET_EVENT_FIRE,4,123}, e2;
    assert(hta_net_event_pack(wire,sizeof(wire),&e));
    assert(hta_net_event_unpack(wire,7,&e2) && e2.event_id==123);
    wire[1]=255; assert(!hta_net_event_unpack(wire,7,&e2));
}

static void pump(hta_net_server *s, hta_net_client *a, hta_net_client *b, double now)
{
    if (a) hta_net_client_pump(a,now);
    if (b) hta_net_client_pump(b,now);
    hta_net_server_pump(s,now);
    if (a) hta_net_client_pump(a,now+0.0001);
    if (b) hta_net_client_pump(b,now+0.0001);
}

static void sessions(void)
{
    hta_net_server s; hta_net_client a,b;
    assert(hta_net_server_open(&s,0));
    uint16_t port=hta_udp_port(&s.udp); assert(port);
    assert(hta_net_client_open(&a,"127.0.0.1",port));
    assert(hta_net_client_open(&b,"127.0.0.1",port));
    for (int i=0;i<5;i++) pump(&s,&a,&b,1+i*0.05);
    assert(a.connected && b.connected && a.id!=b.id);
    assert(hta_net_server_count(&s)==2);
    assert(a.stats.ping_ms>=0 && b.stats.ping_ms>=0);
    /* A valid endpoint without the assigned token cannot submit state. */
    uint8_t bad_payload[4+HTA_NET_PLAYER_BYTES]={0}, bad_wire[HTA_NET_MAX_PACKET];
    size_t bad_n=0;
    hta_net_player bogus={.id=a.id,.weapon=1};
    assert(hta_net_player_pack(bad_payload+4,HTA_NET_PLAYER_BYTES,&bogus));
    assert(hta_net_pack(bad_wire,sizeof(bad_wire),HTA_NET_INPUT,0,0,
                        bad_payload,sizeof(bad_payload),&bad_n));
    assert(hta_udp_send(&a.udp,&a.server,bad_wire,bad_n));
    hta_net_server_pump(&s,1.5);
    assert(s.stats.invalid>=1 && !s.peers[a.id-1].has_state);
    /* A mismatched version fails at the codec boundary before state access. */
    bad_wire[4]=2;
    assert(hta_udp_send(&a.udp,&a.server,bad_wire,bad_n));
    hta_net_server_pump(&s,1.6);
    assert(s.stats.invalid>=2);
    hta_net_player pa={.id=a.id,.weapon=1,.flags=HTA_NET_GROUNDED,
                       .pos={1,2,3},.yaw=0.75f};
    hta_net_player pb={.id=b.id,.weapon=2,.flags=HTA_NET_CROUCH,
                       .pos={4,5,6},.yaw=-0.75f};
    assert(hta_net_client_state(&a,&pa));
    assert(hta_net_client_state(&b,&pb));
    for (int i=0;i<4;i++) pump(&s,&a,&b,2+i*0.06);
    assert(a.present[b.id-1] && b.present[a.id-1]);
    assert(a.players[b.id-1].pos[0]==4 && b.players[a.id-1].pos[0]==1);
    assert(a.players[b.id-1].flags & HTA_NET_CROUCH);
    hta_net_event fire={a.id,HTA_NET_EVENT_FIRE,1,42}, received;
    assert(hta_net_client_event(&a,&fire));
    pump(&s,&a,&b,3);
    assert(hta_net_client_pop_event(&b,&received));
    assert(received.actor==a.id && received.kind==HTA_NET_EVENT_FIRE && received.event_id==42);
    assert(!hta_net_client_pop_event(&b,&received));
    hta_net_client_close(&a);
    hta_net_server_pump(&s,3.1);
    assert(hta_net_server_count(&s)==1);
    hta_net_client_close(&b);
    hta_net_server_pump(&s,3.2);
    assert(hta_net_server_count(&s)==0);
    hta_net_server_close(&s);
}

int main(void) { codec(); sessions(); puts("net: codec, malformed input, two UDP clients, snapshots, events, ping, disconnect OK"); }
