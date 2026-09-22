#include "net/protocol.h"
#include "net/session.h"
#include "net/replication.h"
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
    wire[4]=3; assert(!hta_net_unpack(wire,n,&p)); wire[4]=HTA_NET_VERSION;
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

static void world_codec(void)
{
    hta_net_fx fx={.kind=HTA_NET_FX_DETONATE,.entity=255,.weapon=2,
        .material=4,.pos={1,2,3},.dir={0,0,1}}, fx2;
    uint8_t fx_wire[HTA_NET_FX_BYTES];
    assert(hta_net_fx_pack(fx_wire,sizeof(fx_wire),&fx));
    assert(hta_net_fx_unpack(fx_wire,sizeof(fx_wire),&fx2));
    assert(fx2.entity==255 && fx2.pos[2]==3);
    hta_net_projectiles projs={0},projs2;
    projs.count=1; projs.live[0].pool=2; projs.live[0].slot=7;
    projs.live[0].pos[0]=12; projs.live[0].dir[1]=1;
    uint8_t proj_wire[1+HTA_NET_MAX_PROJECTILES*HTA_NET_PROJECTILE_BYTES];
    size_t proj_n=0;
    assert(hta_net_projectiles_pack(proj_wire,sizeof(proj_wire),&projs,&proj_n));
    assert(hta_net_projectiles_unpack(proj_wire,proj_n,&projs2));
    assert(projs2.count==1 && projs2.live[0].pos[0]==12);
    hta_net_kill kill={.id=7,.victim=1,.killer=0}, kill2;
    snprintf(kill.text,sizeof(kill.text),"Bot was killed by Host");
    uint8_t kill_wire[HTA_NET_KILL_BYTES];
    assert(hta_net_kill_pack(kill_wire,sizeof(kill_wire),&kill));
    assert(hta_net_kill_unpack(kill_wire,sizeof(kill_wire),&kill2));
    assert(kill2.id==7 && !strcmp(kill2.text,kill.text));
    kill_wire[6]=1;
    assert(!hta_net_kill_unpack(kill_wire,sizeof(kill_wire),&kill2));
    hta_net_control ctl={.id=2,.flags=HTA_NET_TRIGGER|HTA_NET_DUCK,
        .weapon_slot=1,.forward=0.5f,.right=-1.0f,.yaw=1.5f,.pitch=-0.3f,
        .melee_count=3,.grenade_count=2,.reload_count=1}, ctl2;
    uint8_t control_wire[HTA_NET_CONTROL_BYTES];
    assert(hta_net_control_pack(control_wire,sizeof(control_wire),&ctl));
    assert(hta_net_control_unpack(control_wire,sizeof(control_wire),&ctl2));
    assert(ctl2.id==2 && ctl2.forward==0.5f && ctl2.melee_count==3);
    control_wire[1]=0x80;
    assert(!hta_net_control_unpack(control_wire,sizeof(control_wire),&ctl2));
    hta_net_world a={0},b;
    a.time=12.5f; a.round=3; a.count=2; a.bot_count=1;
    a.winner=255; a.score_limit=25; a.time_limit=10; a.respawn_time=5;
    a.entities[0]=(hta_net_entity){.id=0,.kind=HTA_NET_ENTITY_PLAYER,
        .flags=HTA_NET_ENTITY_ALIVE|HTA_NET_ENTITY_GROUNDED,.weapon=1,
        .pos={1,2,3},.velocity={4,5},.yaw=0.4f,.pitch=-0.1f,
        .health=75,.shield=25,.score=3,.kills=4,.deaths=1};
    snprintf(a.entities[0].name,sizeof(a.entities[0].name),"Host");
    a.entities[1]=(hta_net_entity){.id=1,.kind=HTA_NET_ENTITY_BOT,
        .flags=HTA_NET_ENTITY_ALIVE,.weapon=0,.health=60,.shield=75};
    snprintf(a.entities[1].name,sizeof(a.entities[1].name),"Bot");
    uint8_t payload[HTA_NET_MAX_PACKET]; size_t n=0;
    assert(hta_net_world_pack(payload,sizeof(payload),&a,&n));
    assert(n==HTA_NET_WORLD_HEADER+2*HTA_NET_ENTITY_BYTES);
    assert(hta_net_world_unpack(payload,n,&b));
    assert(b.round==3 && b.entities[0].score==3 && b.entities[0].health==75 &&
           !strcmp(b.entities[1].name,"Bot"));
    assert(!hta_net_world_unpack(payload,n-1,&b));
    payload[HTA_NET_WORLD_HEADER+1]=9;
    assert(!hta_net_world_unpack(payload,n,&b));
    payload[HTA_NET_WORLD_HEADER+1]=HTA_NET_ENTITY_PLAYER;
    payload[HTA_NET_WORLD_HEADER+47]=0x01;
    assert(!hta_net_world_unpack(payload,n,&b));
    memset(&a,0,sizeof(a)); a.count=HTA_NET_MAX_ENTITIES; a.winner=255;
    a.item_count=64;
    for (uint8_t i=0;i<a.count;i++) {
        a.entities[i].id=i; a.entities[i].kind=HTA_NET_ENTITY_BOT;
        a.entities[i].weapon=255; a.entities[i].carry[0]=255;
        a.entities[i].carry[1]=255;
        snprintf(a.entities[i].name,sizeof(a.entities[i].name),"Bot %u",i);
    }
    assert(hta_net_world_pack(payload,sizeof(payload),&a,&n));
    assert(n+HTA_NET_HEADER<=HTA_NET_MAX_PACKET);
    assert(hta_net_world_unpack(payload,n,&b) && b.count==HTA_NET_MAX_ENTITIES);
}

static void interpolation(void)
{
    hta_net_player a={.id=1,.weapon=0,.flags=HTA_NET_GROUNDED,
                      .pos={0,0,0},.yaw=3.0f};
    hta_net_player b={.id=1,.weapon=1,.flags=HTA_NET_CROUCH,
                      .pos={10,2,4},.yaw=-3.0f};
    hta_net_player out;
    assert(hta_net_interpolate(&a,&b,0.5f,&out));
    assert(out.pos[0]==5 && out.pos[1]==1 && out.pos[2]==2);
    assert(fabsf(fabsf(out.yaw)-3.14159265f)<0.001f);
    assert(out.weapon==1 && out.flags==HTA_NET_CROUCH);
    assert(hta_net_interpolate(&a,&b,-1,&out) && out.pos[0]==0);
    assert(hta_net_interpolate(&a,&b,2,&out) && out.pos[0]==10);
    b.id=2; assert(!hta_net_interpolate(&a,&b,0.5f,&out));
    assert(!hta_net_interpolate(&a,&a,NAN,&out));
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
    /* A stale WELCOME with another nonce cannot replace this session. */
    uint8_t stale_payload[9]={a.id}, stale_wire[HTA_NET_MAX_PACKET];
    size_t stale_n=0;
    hta_net_u32_write(stale_payload+1,0x12345678u);
    hta_net_u32_write(stale_payload+5,a.nonce^1u);
    assert(hta_net_pack(stale_wire,sizeof(stale_wire),HTA_NET_WELCOME,999,0,
                        stale_payload,sizeof(stale_payload),&stale_n));
    assert(hta_udp_send(&s.udp,&s.peers[a.id-1].addr,stale_wire,stale_n));
    hta_net_client_pump(&a,1.3);
    assert(a.token==s.peers[a.id-1].token);
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
    bad_wire[4]=HTA_NET_VERSION+1;
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
    hta_net_control ctl={.id=b.id,.flags=HTA_NET_TRIGGER,.weapon_slot=1,
                         .forward=1,.yaw=0.5f,.melee_count=1};
    assert(hta_net_client_control(&b,&ctl));
    hta_net_server_pump(&s,2.25);
    assert(s.peers[b.id-1].has_control && s.peers[b.id-1].control.melee_count==1);
    hta_net_world world={0}; world.count=3; world.winner=255;
    world.entities[0].id=0; world.entities[0].kind=HTA_NET_ENTITY_PLAYER;
    world.entities[0].weapon=0; world.entities[0].peer_id=a.id;
    snprintf(world.entities[0].name,sizeof(world.entities[0].name),"Host");
    world.entities[1].id=1; world.entities[1].kind=HTA_NET_ENTITY_BOT;
    world.entities[1].weapon=0; world.entities[1].shield=25;
    snprintf(world.entities[1].name,sizeof(world.entities[1].name),"Bot");
    world.entities[2].id=2; world.entities[2].kind=HTA_NET_ENTITY_PLAYER;
    world.entities[2].weapon=1; world.entities[2].peer_id=b.id;
    world.entities[2].score=3;
    snprintf(world.entities[2].name,sizeof(world.entities[2].name),"Guest");
    assert(hta_net_server_world(&s,&world));
    hta_net_client_pump(&a,2.3); hta_net_client_pump(&b,2.3);
    assert(a.have_world && b.have_world && b.world.count==3 &&
           a.world.entities[1].shield==25 && b.world.entities[2].score==3 &&
           b.world.entities[2].peer_id==b.id);
    assert(!hta_net_server_world(&s,&world)); /* once per server tick */
    hta_net_projectiles projs={0}; projs.count=1;
    projs.live[0].pool=4; projs.live[0].slot=1; projs.live[0].pos[2]=3;
    assert(hta_net_server_projectiles(&s,&projs));
    hta_net_client_pump(&b,2.305);
    assert(b.have_projectiles && b.projectiles.live[0].pool==4 &&
           b.projectiles.live[0].pos[2]==3);
    hta_net_fx fx={.kind=HTA_NET_FX_FIRE,.entity=0,.weapon=1};
    assert(hta_net_server_fx(&s,&fx));
    hta_net_client_pump(&b,2.306);
    hta_net_fx fx_recv;
    assert(hta_net_client_pop_fx(&b,&fx_recv) && fx_recv.weapon==1);
    hta_net_kill kill={.victim=1,.killer=0};
    snprintf(kill.text,sizeof(kill.text),"Bot was killed by Host");
    assert(hta_net_server_kill(&s,&kill,2.3));
    hta_net_client_pump(&b,2.31);
    hta_net_kill received_kill;
    assert(hta_net_client_pop_kill(&b,&received_kill));
    assert(!strcmp(received_kill.text,kill.text));
    hta_net_server_pump(&s,2.32);
    assert(!s.peers[b.id-1].pending_kills[0].active);
    hta_net_event fire={a.id,HTA_NET_EVENT_FIRE,1,42}, received;
    assert(hta_net_client_event(&a,&fire));
    pump(&s,&a,&b,3);
    assert(hta_net_server_pop_action(&s,&received));
    assert(received.actor==a.id && received.event_id==42);
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

    /* An unexpectedly lost server clears stale remote state and retries. */
    assert(hta_net_server_open(&s,0));
    port=hta_udp_port(&s.udp);
    assert(hta_net_client_open(&a,"127.0.0.1",port));
    pump(&s,&a,NULL,20.0);
    assert(a.connected);
    uint32_t old_nonce=a.nonce;
    hta_net_server_close(&s);
    hta_net_client_pump(&a,31.0);
    assert(!a.connected && a.id==0 && a.nonce!=old_nonce && !a.present[0]);
    hta_net_client_close(&a);
}

/* A lobby finds a server without an address typed in, and a full server
 * turns a third player away. */
static void discovery(void)
{
    uint8_t wire[HTA_NET_INFO_BYTES];
    hta_net_info in={.nonce=7,.players=1,.max_players=4,.score_limit=25,.time_limit=10}, out;
    snprintf(in.name,sizeof(in.name),"Blood Gulch LAN");
    assert(hta_net_info_pack(wire,sizeof(wire),&in));
    assert(hta_net_info_unpack(wire,sizeof(wire),&out));
    assert(out.nonce==7 && out.players==1 && out.max_players==4 && out.score_limit==25 &&
           out.time_limit==10 && !strcmp(out.name,"Blood Gulch LAN"));
    wire[8]=7; assert(!hta_net_info_unpack(wire,sizeof(wire),&out)); wire[8]='B';
    wire[4]=5; assert(!hta_net_info_unpack(wire,sizeof(wire),&out)); wire[4]=1;
    assert(!hta_net_info_unpack(wire,sizeof(wire)-1,&out));

    hta_net_server s; hta_net_client a,b; hta_net_scan scan;
    assert(hta_net_server_open(&s,0));
    assert(s.info.max_players==HTA_NET_MAX_PLAYERS);
    s.info.max_players=1; s.info.score_limit=15;
    snprintf(s.info.name,sizeof(s.info.name),"Phone");
    uint16_t port=hta_udp_port(&s.udp);
    assert(hta_net_scan_open(&scan));
    assert(hta_net_scan_send(&scan,"127.0.0.1",port));
    hta_net_server_pump(&s,1.0);
    char ip[16]; uint16_t from_port=0;
    assert(hta_net_scan_recv(&scan,&out,ip,&from_port));
    assert(!strcmp(ip,"127.0.0.1") && from_port==port);
    assert(out.players==0 && out.max_players==1 && out.score_limit==15 && !strcmp(out.name,"Phone"));
    assert(hta_net_server_count(&s)==0);   /* asking is not joining */

    assert(hta_net_client_open(&a,"127.0.0.1",port));
    assert(hta_net_client_open(&b,"127.0.0.1",port));
    for (int i=0;i<5;i++) pump(&s,&a,&b,2+i*0.3);
    assert(a.connected && !b.connected && b.reject_reason==HTA_NET_REJECT_FULL &&
           hta_net_server_count(&s)==1);
    assert(hta_net_scan_send(&scan,"127.0.0.1",port));
    hta_net_server_pump(&s,4.0);
    assert(hta_net_scan_recv(&scan,&out,ip,&from_port) && out.players==1);
    hta_net_client_close(&a); hta_net_client_close(&b);
    hta_net_scan_close(&scan); hta_net_server_close(&s);

    assert(hta_net_server_open(&s,0));
    s.map_crc=0xAABBCCDDu;
    port=hta_udp_port(&s.udp);
    assert(hta_net_client_open(&a,"127.0.0.1",port));
    a.map_crc=0x12345678u;
    for (int i=0;i<4;i++) pump(&s,&a,NULL,10+i*0.3);
    assert(!a.connected && a.reject_reason==HTA_NET_REJECT_MAP &&
           hta_net_server_count(&s)==0);
    hta_net_client_close(&a); hta_net_server_close(&s);
}

int main(void) { codec(); world_codec(); interpolation(); sessions(); discovery(); puts("net: codec, world, interpolation, malformed input, two UDP clients, snapshots, events, ping, disconnect, LAN discovery, player cap OK"); }
