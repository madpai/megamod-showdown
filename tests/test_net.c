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
    wire[4]=HTA_NET_VERSION+1; assert(!hta_net_unpack(wire,n,&p)); wire[4]=HTA_NET_VERSION;
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
    /* X7: a binding's world sound -- a world entity and a sound index
     * (low byte in `weapon`, high in `material`), bounded both ways. */
    {
        /* v11 (X8): the entity is a 16-bit runtime object index: past the
         * old 6-bit range, up to the last legal one, and not one more. */
        hta_net_fx ws={.kind=HTA_NET_FX_WORLD_SOUND,.entity=1023,.weapon=0xFF,.material=1,.pos={-3,2.5f,0.9f}}, ws2;
        uint8_t w[HTA_NET_FX_BYTES];
        assert(hta_net_fx_pack(w,sizeof(w),&ws) && hta_net_fx_unpack(w,sizeof(w),&ws2));
        assert(ws2.kind==HTA_NET_FX_WORLD_SOUND && ws2.entity==1023 && (ws2.weapon|(ws2.material<<8))==511 && ws2.pos[1]==2.5f);
        assert(w[1]==0xFF && w[2]==0x03);                        /* little-endian on the wire */
        ws.entity=72; assert(hta_net_fx_pack(w,sizeof(w),&ws) && hta_net_fx_unpack(w,sizeof(w),&ws2) && ws2.entity==72);
        ws.entity=HTA_NET_FX_MAX_WORLD_ENTITIES; assert(!hta_net_fx_pack(w,sizeof(w),&ws));
        ws.entity=0; ws.material=8; assert(!hta_net_fx_pack(w,sizeof(w),&ws));   /* sound 2303: over 2048 */
        ws.material=7; assert(hta_net_fx_pack(w,sizeof(w),&ws));                  /* sound 2047 */
        ws.material=1; assert(hta_net_fx_pack(w,sizeof(w),&ws));
        w[1]=0x00; w[2]=0x04; assert(!hta_net_fx_unpack(w,sizeof(w),&ws2));   /* 1024 on the wire */
        w[2]=0; w[0]=6; assert(!hta_net_fx_unpack(w,sizeof(w),&ws2));
        /* a unit FX keeps its unit (or 255) in the same 16 bits */
        hta_net_fx u={.kind=HTA_NET_FX_FIRE,.entity=256,.weapon=1};
        assert(!hta_net_fx_pack(w,sizeof(w),&u));
    }
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
    /* v9: a gibbing kill carries how hard and from where. */
    kill.flags=HTA_NET_KILL_GIBBED; kill.amount=1.5f;
    kill.pos[0]=3.25f; kill.pos[2]=0.4f; kill.from[0]=2.0f; kill.from[1]=-1.0f;
    assert(hta_net_kill_pack(kill_wire,sizeof(kill_wire),&kill));
    assert(hta_net_kill_unpack(kill_wire,sizeof(kill_wire),&kill2));
    assert(kill2.flags==HTA_NET_KILL_GIBBED && fabsf(kill2.amount-1.5f)<0.01f &&
           kill2.pos[0]==3.25f && kill2.from[1]==-1.0f);
    { hta_net_kill bad=kill; bad.amount=9.0f; assert(!hta_net_kill_pack(kill_wire,sizeof(kill_wire),&bad));
      bad=kill; bad.flags=2; assert(!hta_net_kill_pack(kill_wire,sizeof(kill_wire),&bad));
      bad=kill; bad.pos[1]=NAN; assert(!hta_net_kill_pack(kill_wire,sizeof(kill_wire),&bad));
      assert(hta_net_kill_pack(kill_wire,sizeof(kill_wire),&kill)); kill_wire[102]=4;
      assert(!hta_net_kill_unpack(kill_wire,sizeof(kill_wire),&kill2)); }
    kill.flags=0; kill.amount=0; memset(kill.pos,0,sizeof(kill.pos)); memset(kill.from,0,sizeof(kill.from));
    hta_net_control ctl={.id=2,.flags=HTA_NET_TRIGGER|HTA_NET_DUCK,
        .weapon_slot=1,.forward=0.5f,.right=-1.0f,.yaw=1.5f,.pitch=-0.3f,
        .melee_count=3,.grenade_count=2,.reload_count=1}, ctl2;
    uint8_t control_wire[HTA_NET_CONTROL_BYTES];
    assert(hta_net_control_pack(control_wire,sizeof(control_wire),&ctl));
    assert(hta_net_control_unpack(control_wire,sizeof(control_wire),&ctl2));
    assert(ctl2.id==2 && ctl2.forward==0.5f && ctl2.melee_count==3);
    control_wire[1]=0x80;
    assert(!hta_net_control_unpack(control_wire,sizeof(control_wire),&ctl2));
    ctl.flags|=HTA_NET_ALT; ctl.action_count=41;
    assert(hta_net_control_pack(control_wire,sizeof(control_wire),&ctl));
    assert(hta_net_control_unpack(control_wire,sizeof(control_wire),&ctl2));
    assert((ctl2.flags&HTA_NET_ALT) && ctl2.action_count==41);
    /* v6: a class and a body travel with the controls, and READY. */
    ctl.flags|=HTA_NET_READY; ctl.loadout[0]=10; ctl.loadout[1]=255; ctl.character=3;
    assert(hta_net_control_pack(control_wire,sizeof(control_wire),&ctl));
    assert(hta_net_control_unpack(control_wire,sizeof(control_wire),&ctl2));
    assert((ctl2.flags&HTA_NET_READY) && ctl2.loadout[0]==10 && ctl2.loadout[1]==255 && ctl2.character==3);
    ctl.loadout[0]=HTA_NET_MAX_WEAPONS; assert(!hta_net_control_pack(control_wire,sizeof(control_wire),&ctl));
    ctl.loadout[0]=10; ctl.character=64; assert(!hta_net_control_pack(control_wire,sizeof(control_wire),&ctl));
    ctl.character=3;
    ctl.team=2; ctl.ability_count=65534; ctl.flags|=HTA_NET_FLY;
    assert(hta_net_control_pack(control_wire,sizeof(control_wire),&ctl));
    assert(hta_net_control_unpack(control_wire,sizeof(control_wire),&ctl2));
    assert(ctl2.team==2 && ctl2.ability_count==65534 && (ctl2.flags&HTA_NET_FLY));
    ctl.team=3; assert(!hta_net_control_pack(control_wire,sizeof(control_wire),&ctl));
    ctl.team=2;
    /* Vehicles: 32 of them, full, under the packet cap; values survive
     * their fixed point; bad values and trailing bytes are refused. */
    static hta_net_vehicles veh, veh2;
    memset(&veh,0,sizeof(veh));
    veh.count=HTA_NET_MAX_VEHICLES;
    for (uint8_t i=0;i<veh.count;i++) {
        hta_net_vehicle *c=&veh.cars[i];
        c->index=i; c->flags=HTA_NET_VEHICLE_ACTIVE|HTA_NET_VEHICLE_DRIVEN;
        c->pos[0]=100.25f; c->pos[1]=-144.63f; c->pos[2]=0.58f+i;
        c->yaw=3.1f; c->pitch=-0.12f; c->roll=0.05f; c->aim_yaw=-2.9f; c->aim_pitch=0.4f;
        c->steering=0.5f; c->wheel_spin=7.0f; c->barrel_spin=-1.0f; c->speed=-3.6f;
        for (int k=0;k<HTA_NET_VEHICLE_SEATS;k++) c->occupant[k]=k==2 ? 5 : 255;
        c->travel[0]=0.1f; c->travel[3]=-0.2f;
    }
    uint8_t veh_wire[1+HTA_NET_MAX_VEHICLES*HTA_NET_VEHICLE_BYTES];
    size_t veh_n=0;
    assert(hta_net_vehicles_pack(veh_wire,sizeof(veh_wire),&veh,&veh_n));
    assert(veh_n+HTA_NET_HEADER<=HTA_NET_MAX_PACKET);
    assert(hta_net_vehicles_unpack(veh_wire,veh_n,&veh2));
    assert(veh2.count==32 && fabsf(veh2.cars[5].pos[0]-100.25f)<0.006f &&
           fabsf(veh2.cars[5].pos[2]-5.58f)<0.006f && veh2.cars[5].occupant[2]==5 &&
           veh2.cars[5].occupant[0]==255 && fabsf(veh2.cars[5].speed+3.6f)<0.006f &&
           fabsf(veh2.cars[5].yaw-3.1f)<0.0002f && fabsf(veh2.cars[5].travel[3]+0.2f)<0.003f);
    /* wheel spin wraps into -pi..pi rather than failing */
    assert(fabsf(veh2.cars[0].wheel_spin-(7.0f-6.2831853f))<0.001f);
    assert(!hta_net_vehicles_unpack(veh_wire,veh_n-1,&veh2));
    veh_wire[1+26]=200;   /* an occupant no match could have */
    assert(!hta_net_vehicles_unpack(veh_wire,veh_n,&veh2));
    veh.cars[0].pos[0]=500.0f;   /* past the fixed-point range */
    assert(!hta_net_vehicles_pack(veh_wire,sizeof(veh_wire),&veh,&veh_n));
    /* An angle of exactly +-pi (a car parked facing 180 degrees) must
     * survive the receiver's re-pack check: it once rejected every VEHICLES
     * packet on Blood Gulch, and DROPS and GAME share the quantiser. */
    {
        const float edges[4]={3.14159265f,-3.14159265f,3.14158f,-3.14158f};
        for (unsigned e=0;e<4;e++) {
            hta_net_vehicles one; memset(&one,0,sizeof(one)); one.count=1;
            for (int k=0;k<HTA_NET_VEHICLE_SEATS;k++) one.cars[0].occupant[k]=255;
            one.cars[0].yaw=one.cars[0].pitch=one.cars[0].roll=one.cars[0].aim_yaw=
                one.cars[0].aim_pitch=one.cars[0].steering=one.cars[0].wheel_spin=
                one.cars[0].barrel_spin=edges[e];
            assert(hta_net_vehicles_pack(veh_wire,sizeof(veh_wire),&one,&veh_n));
            assert(hta_net_vehicles_unpack(veh_wire,veh_n,&veh2));
            assert(fabsf(fabsf(veh2.cars[0].yaw)-3.1415f)<0.0002f);
            hta_net_drops dr,dr2; memset(&dr,0,sizeof(dr)); dr.count=1; dr.drop[0].yaw=edges[e];
            uint8_t dw[1+HTA_NET_DROP_BYTES]; size_t dn=0;
            assert(hta_net_drops_pack(dw,sizeof(dw),&dr,&dn) && hta_net_drops_unpack(dw,dn,&dr2));
            hta_net_game g,g2; memset(&g,0,sizeof(g)); g.winner_team=255;
            g.flag[0].present=1; g.flag[0].carrier=255; g.flag[0].yaw=edges[e];
            uint8_t gw[HTA_NET_GAME_BYTES];
            assert(hta_net_game_pack(gw,sizeof(gw),&g) && hta_net_game_unpack(gw,sizeof(gw),&g2));
        }
    }
    hta_net_world a={0},b;
    a.time=12.5f; a.round=3; a.count=2; a.bot_count=1;
    a.winner=255; a.score_limit=25; a.time_limit=10; a.respawn_time=5;
    a.entities[0]=(hta_net_entity){.id=0,.kind=HTA_NET_ENTITY_PLAYER,
        .flags=HTA_NET_ENTITY_ALIVE|HTA_NET_ENTITY_GROUNDED|HTA_NET_ENTITY_CLASS_REJECT,.weapon=42,
        .pos={1,2,3},.velocity={4,5},.yaw=0.4f,.pitch=-0.1f,
        .health=75,.shield=25,.score=3,.kills=4,.deaths=1};
    snprintf(a.entities[0].name,sizeof(a.entities[0].name),"Host");
    a.entities[1]=(hta_net_entity){.id=1,.kind=HTA_NET_ENTITY_BOT,
        .flags=HTA_NET_ENTITY_ALIVE|HTA_NET_ENTITY_BLUE,.weapon=0,.health=60,.shield=75};
    snprintf(a.entities[1].name,sizeof(a.entities[1].name),"Bot");
    uint8_t payload[HTA_NET_MAX_PACKET]; size_t n=0;
    assert(hta_net_world_pack(payload,sizeof(payload),&a,&n));
    assert(n==HTA_NET_WORLD_HEADER+2*HTA_NET_ENTITY_BYTES);
    assert(hta_net_world_unpack(payload,n,&b));
    assert(b.round==3 && b.entities[0].score==3 && b.entities[0].health==75 &&
           b.entities[0].weapon==42 && (b.entities[0].flags&HTA_NET_ENTITY_CLASS_REJECT) &&
           !strcmp(b.entities[1].name,"Bot"));
    assert((b.entities[1].flags&HTA_NET_ENTITY_BLUE) && !(b.entities[0].flags&HTA_NET_ENTITY_BLUE));
    assert(!hta_net_world_unpack(payload,n-1,&b));
    payload[HTA_NET_WORLD_HEADER+1]=3;   /* no such kind */
    assert(!hta_net_world_unpack(payload,n,&b));
    payload[HTA_NET_WORLD_HEADER+1]=HTA_NET_ENTITY_PLAYER | (5 << 2);   /* v6: wearing character 4 */
    assert(hta_net_world_unpack(payload,n,&b) && b.entities[0].character==5 && b.entities[0].kind==HTA_NET_ENTITY_PLAYER);
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
    static hta_net_vehicles cars;
    memset(&cars,0,sizeof(cars)); cars.count=2;
    cars.cars[1].index=7; cars.cars[1].flags=HTA_NET_VEHICLE_ACTIVE;
    cars.cars[1].pos[0]=42.5f; cars.cars[1].yaw=1.0f;
    for (int k=0;k<HTA_NET_VEHICLE_SEATS;k++) cars.cars[0].occupant[k]=cars.cars[1].occupant[k]=255;
    cars.cars[1].occupant[0]=2;
    assert(hta_net_server_vehicles(&s,&cars));
    assert(!hta_net_server_vehicles(&s,&cars)); /* once per server tick */
    hta_net_client_pump(&b,2.3055);
    assert(b.have_vehicles && b.vehicles.count==2 && b.vehicles.cars[1].index==7 &&
           fabsf(b.vehicles.cars[1].pos[0]-42.5f)<0.01f && b.vehicles.cars[1].occupant[0]==2);
    static hta_net_drops drops;
    memset(&drops,0,sizeof(drops)); drops.count=HTA_NET_MAX_DROPS;
    for (unsigned i=0;i<drops.count;i++) { drops.drop[i].weapon=(uint8_t)(i%9); drops.drop[i].pos[0]=i; drops.drop[i].yaw=3.0f; }
    assert(hta_net_server_drops(&s,&drops));
    hta_net_client_pump(&b,2.3057);
    assert(b.have_drops && b.drops.count==HTA_NET_MAX_DROPS && b.drops.drop[31].weapon==31%9 &&
           fabsf(b.drops.drop[31].pos[0]-31)<0.01f && fabsf(b.drops.drop[5].yaw-3.0f)<0.001f);
    drops.drop[0].weapon=HTA_NET_MAX_WEAPONS;
    assert(!hta_net_server_drops(&s,&drops));
    /* v4: the rules ride beside WORLD -- CTF with blue's flag in unit 3's
     * hands, red's lying in the field, and a wrecked car. */
    static hta_net_game gm;
    memset(&gm,0,sizeof(gm));
    gm.mode=2; gm.score_limit=3; gm.team_score[0]=2; gm.team_score[1]=-1; gm.winner_team=255;
    gm.flag[0].present=1; gm.flag[0].state=HTA_NET_FLAG_DROPPED; gm.flag[0].carrier=255;
    gm.flag[0].pos[0]=-12.5f; gm.flag[0].pos[2]=0.75f; gm.flag[0].yaw=-2.0f;
    gm.flag[1].present=1; gm.flag[1].state=HTA_NET_FLAG_CARRIED; gm.flag[1].carrier=3;
    for (unsigned i=0;i<HTA_NET_MAX_VEHICLES;i++) gm.hull[i]=255;
    gm.hull[4]=0; gm.hull[5]=100;
    /* v9: three hundred props, the first and the last broken. */
    gm.prop_count=300; gm.prop_broken[0]=1; gm.prop_broken[299/8]=(uint8_t)(1u<<(299%8));
    assert(hta_net_server_game(&s,&gm));
    assert(!hta_net_server_game(&s,&gm)); /* once per server tick */
    hta_net_client_pump(&b,2.3058);
    assert(b.have_game && b.game.mode==2 && b.game.team_score[0]==2 && b.game.team_score[1]==-1 &&
           b.game.flag[0].state==HTA_NET_FLAG_DROPPED && fabsf(b.game.flag[0].pos[0]+12.5f)<0.01f &&
           fabsf(b.game.flag[0].yaw+2.0f)<0.001f && b.game.flag[1].carrier==3 &&
           b.game.hull[4]==0 && b.game.hull[5]==100 && b.game.hull[6]==255);
    assert(b.game.prop_count==300 && b.game.prop_broken[0]==1 &&
           b.game.prop_broken[37]==(uint8_t)(1u<<3) && b.game.prop_broken[38]==0);
    { uint8_t buf[HTA_NET_GAME_BYTES]; hta_net_game bad=gm; bad.flag[1].carrier=HTA_NET_MAX_ENTITIES;
      assert(!hta_net_game_pack(buf,sizeof(buf),&bad));
      bad=gm; bad.mode=7; assert(!hta_net_game_pack(buf,sizeof(buf),&bad));
      bad=gm; bad.options=4; assert(!hta_net_game_pack(buf,sizeof(buf),&bad));
      bad=gm; bad.options=HTA_NET_GAME_CLASSES | HTA_NET_GAME_DUPLICATES; hta_net_game ok;
      assert(hta_net_game_pack(buf,sizeof(buf),&bad) && hta_net_game_unpack(buf,sizeof(buf),&ok) &&
             ok.options==(HTA_NET_GAME_CLASSES | HTA_NET_GAME_DUPLICATES));
      bad=gm; bad.prop_count=HTA_NET_MAX_PROPS+1; assert(!hta_net_game_pack(buf,sizeof(buf),&bad));
      /* A bit past the count is never on the wire, and one there is refused. */
      bad=gm; bad.prop_broken[40]=0xFF; assert(hta_net_game_pack(buf,sizeof(buf),&bad) &&
          hta_net_game_unpack(buf,sizeof(buf),&ok) && ok.prop_broken[40]==0);
      buf[32+HTA_NET_MAX_VEHICLES+40]=1; assert(!hta_net_game_unpack(buf,sizeof(buf),&ok));
      assert(hta_net_game_pack(buf,sizeof(buf),&gm)); buf[8]=0xFF;
      hta_net_game back; assert(!hta_net_game_unpack(buf,sizeof(buf),&back)); }
    /* WORLD_STATE (v11): movers and relays by replication index, over the
     * same loopback; sent when it changes, not every tick. */
    { static hta_net_world_state ws; memset(&ws,0,sizeof(ws));
      ws.spatial_total=100; ws.flag_total=20;
      for (unsigned i=0;i<100;i++) hta_net_bit_set(ws.spatial_has,i,true);
      for (unsigned i=0;i<20;i++) hta_net_bit_set(ws.flag_has,i,true);
      ws.phase[72]=1; ws.t[72]=12345; ws.phase[99]=2; ws.t[99]=65535;
      hta_net_bit_set(ws.flag,4,true); hta_net_bit_set(ws.flag,19,true);
      assert(hta_net_server_world_state(&s,&ws));
      assert(!hta_net_server_world_state(&s,&ws)); /* once per server tick */
      hta_net_client_pump(&b,2.30585);
      assert(b.have_world_state && b.world_state.spatial_total==100 && b.world_state.flag_total==20 &&
             b.world_state.phase[72]==1 && b.world_state.t[72]==12345 && b.world_state.phase[99]==2 &&
             b.world_state.t[99]==65535 && hta_net_bit(b.world_state.flag,4) && hta_net_bit(b.world_state.flag,19) &&
             !hta_net_bit(b.world_state.flag,5) && b.stats.world_states==1);
      assert(s.stats.world_states==1 && s.stats.world_state_bytes==5+4+100+2+5+3); }
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
    /* v5: the host's world travels with the answer. */
    assert(!out.map[0]);
    snprintf(in.map,sizeof(in.map),"de_dust2");
    assert(hta_net_info_pack(wire,sizeof(wire),&in));
    assert(hta_net_info_unpack(wire,sizeof(wire),&out) && !strcmp(out.map,"de_dust2"));
    wire[8+HTA_NET_NAME]='/'; assert(!hta_net_info_unpack(wire,sizeof(wire),&out));
    snprintf(in.map,sizeof(in.map),"../x"); assert(!hta_net_info_pack(wire,sizeof(wire),&in));
    in.map[0]=0; assert(hta_net_info_pack(wire,sizeof(wire),&in));

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

/* ---- X8: v11 WORLD_STATE -------------------------------------------------- */

static uint32_t rng_state = 12345u;
static uint32_t rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }

/* Two decoded states say the same (a resting mover's progress is implied). */
static bool ws_equal(const hta_net_world_state *a, const hta_net_world_state *b)
{
    if (a->spatial_total != b->spatial_total || a->flag_total != b->flag_total) return false;
    for (unsigned i = 0; i < a->spatial_total; i++) {
        if (hta_net_bit(a->spatial_has, i) != hta_net_bit(b->spatial_has, i)) return false;
        if (!hta_net_bit(a->spatial_has, i)) continue;
        if (a->phase[i] != b->phase[i]) return false;
        if ((a->phase[i] == 1 || a->phase[i] == 3) && a->t[i] != b->t[i]) return false;
    }
    for (unsigned i = 0; i < a->flag_total; i++) {
        if (hta_net_bit(a->flag_has, i) != hta_net_bit(b->flag_has, i)) return false;
        if (hta_net_bit(a->flag_has, i) && hta_net_bit(a->flag, i) != hta_net_bit(b->flag, i)) return false;
    }
    return true;
}

static void ws_random(hta_net_world_state *w, bool full)
{
    memset(w, 0, sizeof(*w));
    w->spatial_total = (uint16_t)(rnd() % (HTA_NET_WSTATE_MAX_SPATIAL + 1));
    w->flag_total = (uint16_t)(rnd() % (HTA_NET_WSTATE_MAX_FLAGS + 1));
    /* partial: blocks of carried and skipped entries, as a sender would pick them */
    bool take = true;
    for (unsigned i = 0, left = 0; i < w->spatial_total; i++) {
        if (!left) { left = 1 + rnd() % 40; take = full || rnd() % 3; }
        left--;
        if (!take) continue;
        hta_net_bit_set(w->spatial_has, i, true);
        w->phase[i] = (uint8_t)(rnd() & 3u);
        w->t[i] = (uint16_t)rnd();
    }
    for (unsigned i = 0, left = 0; i < w->flag_total; i++) {
        if (!left) { left = 1 + rnd() % 80; take = full || rnd() % 3; }
        left--;
        if (!take) continue;
        hta_net_bit_set(w->flag_has, i, true);
        hta_net_bit_set(w->flag, i, rnd() & 1u);
    }
}

static void world_state_codec(void)
{
    static hta_net_world_state w, back;
    uint8_t buf[HTA_NET_MAX_PACKET];
    size_t n = 0;
    char err[160];
    /* An empty world, and one with only relays: nothing a mover needs. */
    memset(&w, 0, sizeof(w));
    assert(hta_net_world_state_pack(buf, sizeof(buf), &w, &n) && n == HTA_NET_WSTATE_HEADER);
    assert(hta_net_world_state_unpack(buf, n, &back, err, sizeof(err)) && back.spatial_total == 0);
    w.flag_total = 9;
    for (unsigned i = 0; i < 9; i++) hta_net_bit_set(w.flag_has, i, true);
    hta_net_bit_set(w.flag, 0, true); hta_net_bit_set(w.flag, 8, true);
    assert(hta_net_world_state_pack(buf, sizeof(buf), &w, &n) && n == 5 + 5 + 2);
    assert(buf[0] == 1 && buf[3] == 9 && buf[4] == 0 && buf[5] == HTA_NET_WSTATE_FLAGS && buf[10] == 0x01 && buf[11] == 0x01);
    assert(hta_net_world_state_unpack(buf, n, &back, err, sizeof(err)) && ws_equal(&w, &back));
    /* The largest complete snapshot: every mover moving, every flag. It
     * fits one packet (which is why the host never splits one). */
    memset(&w, 0, sizeof(w));
    w.spatial_total = HTA_NET_WSTATE_MAX_SPATIAL; w.flag_total = HTA_NET_WSTATE_MAX_FLAGS;
    for (unsigned i = 0; i < w.spatial_total; i++) { hta_net_bit_set(w.spatial_has, i, true); w.phase[i] = 1; w.t[i] = (uint16_t)(i * 7); }
    for (unsigned i = 0; i < w.flag_total; i++) { hta_net_bit_set(w.flag_has, i, true); hta_net_bit_set(w.flag, i, i % 3 == 0); }
    assert(hta_net_world_state_pack(buf, sizeof(buf), &w, &n) && n == HTA_NET_WSTATE_FULL_MAX);
    assert(HTA_NET_WSTATE_FULL_MAX + HTA_NET_HEADER <= HTA_NET_MAX_PACKET);
    assert(hta_net_world_state_unpack(buf, n, &back, err, sizeof(err)) && ws_equal(&w, &back));
    assert(back.phase[255] == 1 && back.t[255] == (uint16_t)(255 * 7) && hta_net_bit(back.flag, 1023) == (1023 % 3 == 0));
    /* At rest a mover is one byte: progress implied, exactly 0 or 1. */
    for (unsigned i = 0; i < w.spatial_total; i++) w.phase[i] = (uint8_t)(i & 1 ? 2 : 0);
    assert(hta_net_world_state_pack(buf, sizeof(buf), &w, &n) && n == 5 + 2 * 4 + 256 + 5 + 128);
    assert(hta_net_world_state_unpack(buf, n, &back, err, sizeof(err)) && back.t[1] == 65535 && back.t[2] == 0);
    /* Over the limits: refused both ways, in words. */
    w.spatial_total = HTA_NET_WSTATE_MAX_SPATIAL + 1; assert(!hta_net_world_state_pack(buf, sizeof(buf), &w, &n));
    w.spatial_total = HTA_NET_WSTATE_MAX_SPATIAL; w.flag_total = HTA_NET_WSTATE_MAX_FLAGS + 1;
    assert(!hta_net_world_state_pack(buf, sizeof(buf), &w, &n));
    w.flag_total = HTA_NET_WSTATE_MAX_FLAGS; w.phase[3] = 4; assert(!hta_net_world_state_pack(buf, sizeof(buf), &w, &n));
    w.phase[3] = 0;
    assert(!hta_net_world_state_pack(buf, 100, &w, &n));                       /* no room: refused, not cut */
    uint8_t m[64];
    /* a hand-made message: 300 movers declared */
    m[0] = 1; m[1] = 0x2C; m[2] = 0x01; m[3] = 0; m[4] = 0;
    assert(!hta_net_world_state_unpack(m, 5, &back, err, sizeof(err)) &&
           !strcmp(err, "WORLD_STATE describes 300 movers, exceeding the spatial limit 256"));
    /* spatial 241 declared, a record for 317 */
    m[1] = 241; m[2] = 0; m[5] = HTA_NET_WSTATE_SPATIAL; m[6] = 0x3D; m[7] = 0x01; m[8] = 1; m[9] = 0;
    assert(!hta_net_world_state_unpack(m, 10, &back, err, sizeof(err)) &&
           !strcmp(err, "WORLD_STATE record references spatial object 317, but the snapshot defines 241"));
    /* the maximum index is legal, one past it is not */
    m[1] = 0; m[2] = 1; m[6] = 0xFF; m[7] = 0; m[8] = 1; m[9] = 2;           /* 256 movers, record 255 open */
    assert(hta_net_world_state_unpack(m, 10, &back, err, sizeof(err)) && back.phase[255] == 2 && hta_net_bit(back.spatial_has, 255));
    m[6] = 0; m[7] = 1;                                                        /* record 256 */
    assert(!hta_net_world_state_unpack(m, 10, &back, err, sizeof(err)) && strstr(err, "spatial object 256, but the snapshot defines 256"));
    /* duplicates / out of order */
    m[6] = 5; m[7] = 0; m[8] = 1; m[9] = 0; m[10] = HTA_NET_WSTATE_SPATIAL; m[11] = 5; m[12] = 0; m[13] = 1; m[14] = 2;
    assert(!hta_net_world_state_unpack(m, 15, &back, err, sizeof(err)) &&
           !strcmp(err, "WORLD_STATE spatial run at 5 repeats or precedes entries up to 5 (runs must ascend)"));
    m[11] = 6; assert(!hta_net_world_state_unpack(m, 15, &back, err, sizeof(err)) &&
                      !strcmp(err, "WORLD_STATE spatial run at 6 continues the previous one (runs are maximal)"));
    m[11] = 7; assert(hta_net_world_state_unpack(m, 15, &back, err, sizeof(err)) && back.phase[7] == 2 && !hta_net_bit(back.spatial_has, 6));
    /* unknown phase, empty run, truncation, unknown kind, format */
    m[9] = 7; assert(!hta_net_world_state_unpack(m, 15, &back, err, sizeof(err)) && strstr(err, "unknown mover phase 7"));
    m[9] = 1; assert(!hta_net_world_state_unpack(m, 15, &back, err, sizeof(err)));      /* moving: needs 2 more bytes */
    m[9] = 0; m[8] = 0; assert(!hta_net_world_state_unpack(m, 10, &back, err, sizeof(err)) && strstr(err, "is empty"));
    m[8] = 1; assert(!hta_net_world_state_unpack(m, 9, &back, err, sizeof(err)) && strstr(err, "truncated"));
    m[10] = 9; assert(!hta_net_world_state_unpack(m, 15, &back, err, sizeof(err)) &&
                      !strcmp(err, "WORLD_STATE section kind 9 unsupported (at byte 10)"));
    m[0] = 2; assert(!hta_net_world_state_unpack(m, 10, &back, err, sizeof(err)) &&
                     !strcmp(err, "WORLD_STATE format 2 unsupported (this build reads format 1)"));
    assert(!hta_net_world_state_unpack(m, 4, &back, err, sizeof(err)) && strstr(err, "truncated"));
    /* flags: padding bits, past the total, over the limit */
    m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 10; m[4] = 0;
    m[5] = HTA_NET_WSTATE_FLAGS; m[6] = 0; m[7] = 0; m[8] = 10; m[9] = 0; m[10] = 0xFF; m[11] = 0x03;
    assert(hta_net_world_state_unpack(m, 12, &back, err, sizeof(err)) && hta_net_bit(back.flag, 9) && !hta_net_bit(back.flag, 10));
    m[11] = 0x07; assert(!hta_net_world_state_unpack(m, 12, &back, err, sizeof(err)) && strstr(err, "sets bits past its 10 flags"));
    m[11] = 0x03; m[8] = 11; assert(!hta_net_world_state_unpack(m, 12, &back, err, sizeof(err)) &&
                                    !strcmp(err, "WORLD_STATE record references logical flag 10, but the snapshot defines 10"));
    m[3] = 0x01; m[4] = 0x04; assert(!hta_net_world_state_unpack(m, 5, &back, err, sizeof(err)) && strstr(err, "1025 logical flags"));
    /* A refused message leaves the destination untouched. */
    back.spatial_total = 77; m[0] = 5; assert(!hta_net_world_state_unpack(m, 12, &back, err, sizeof(err)) && back.spatial_total == 77);
    /* Deterministic little-endian bytes: this exact message, on any host. */
    memset(&w, 0, sizeof(w));
    w.spatial_total = 3; w.flag_total = 2;
    for (unsigned i = 0; i < 3; i++) hta_net_bit_set(w.spatial_has, i, true);
    w.phase[0] = 0; w.phase[1] = 3; w.t[1] = 0x1234; w.phase[2] = 2;
    hta_net_bit_set(w.flag_has, 0, true); hta_net_bit_set(w.flag_has, 1, true); hta_net_bit_set(w.flag, 1, true);
    static const uint8_t want[] = { 1, 3, 0, 2, 0, 1, 0, 0, 3, 0, 3, 0x34, 0x12, 2, 2, 0, 0, 2, 0, 0x02 };
    assert(hta_net_world_state_pack(buf, sizeof(buf), &w, &n) && n == sizeof(want) && !memcmp(buf, want, n));
    /* Random states round trip, full and partial; a complete one's size is
     * what hta_net_world_state_bytes (the contract's formula) says. */
    for (int it = 0; it < 2000; it++) {
        ws_random(&w, it & 1);
        if (!hta_net_world_state_pack(buf, sizeof(buf), &w, &n)) { assert(!(it & 1)); continue; }
        if (it & 1) {
            uint32_t moving = 0;
            for (unsigned i = 0; i < w.spatial_total; i++) moving += w.phase[i] == 1 || w.phase[i] == 3;
            assert(n == hta_net_world_state_bytes(w.spatial_total, moving, w.flag_total));
        }
        assert(hta_net_world_state_unpack(buf, n, &back, err, sizeof(err)) && ws_equal(&w, &back));
    }
}

/* Mutated messages: never a crash or a read past the end; whatever decodes
 * re-encodes to the same state; a refusal says the same thing each time. */
static void world_state_fuzz(void)
{
    static hta_net_world_state w, back, again;
    uint8_t good[HTA_NET_MAX_PACKET], buf[HTA_NET_MAX_PACKET + 8], re[HTA_NET_MAX_PACKET];
    char err[160], err2[160];
    unsigned accepted = 0, refused = 0;
    for (int it = 0; it < 20000; it++) {
        ws_random(&w, it % 3 != 0);
        size_t n = 0;
        assert(hta_net_world_state_pack(good, sizeof(good), &w, &n));
        memcpy(buf, good, n);
        size_t len = n;
        switch (rnd() % 6) {
        case 0: for (int k = 1 + (int)(rnd() % 4); k > 0; k--) buf[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 1: buf[rnd() % len] = (uint8_t)rnd(); break;
        case 2: len = rnd() % (n + 1); break;                                   /* truncated */
        case 3: { size_t extra = 1 + rnd() % 8; for (size_t k = 0; k < extra; k++) buf[len + k] = (uint8_t)rnd(); len += extra; } break;
        case 4: if (len > 7) { size_t a = 5 + rnd() % (len - 5); buf[a] = (uint8_t)(rnd() % 4); } break;   /* a kind byte */
        default: if (len >= 5) { buf[1] = (uint8_t)rnd(); buf[2] = (uint8_t)(rnd() % 3); } break;            /* totals */
        }
        memset(&back, 0x5A, sizeof(back));
        bool ok = hta_net_world_state_unpack(buf, len, &back, err, sizeof(err));
        if (ok) {
            accepted++;
            assert(back.spatial_total <= HTA_NET_WSTATE_MAX_SPATIAL && back.flag_total <= HTA_NET_WSTATE_MAX_FLAGS);
            for (unsigned i = 0; i < back.spatial_total; i++) assert(!hta_net_bit(back.spatial_has, i) || back.phase[i] <= 3);
            size_t m = 0;
            assert(hta_net_world_state_pack(re, sizeof(re), &back, &m));
            assert(hta_net_world_state_unpack(re, m, &again, err2, sizeof(err2)) && ws_equal(&back, &again));
        } else {
            refused++;
            assert(err[0] && strlen(err) < sizeof(err) - 1);
            assert(!hta_net_world_state_unpack(buf, len, &again, err2, sizeof(err2)) && !strcmp(err, err2));
        }
    }
    /* Pure noise, at every length. */
    for (int it = 0; it < 20000; it++) {
        size_t len = rnd() % (HTA_NET_MAX_PACKET - HTA_NET_HEADER + 4);
        for (size_t k = 0; k < len; k++) buf[k] = (uint8_t)rnd();
        if (len) buf[0] = rnd() % 4 ? 1 : buf[0];
        if (hta_net_world_state_unpack(buf, len, &back, err, sizeof(err))) accepted++; else refused++;
    }
    assert(accepted > 1000 && refused > 1000);
    printf("net: WORLD_STATE fuzz %u accepted, %u refused (no crash)\n", accepted, refused);
}

/* The server's send policy: complete state on change, then twice more,
 * while moving every 4th tick, a keyframe every 20th, and at once for a
 * new peer. */
static void world_state_policy(void)
{
    hta_net_server s; hta_net_client a, b;
    assert(hta_net_server_open(&s, 0));
    uint16_t port = hta_udp_port(&s.udp);
    assert(hta_net_client_open(&a, "127.0.0.1", port));
    double now = 1.0;
    for (int i = 0; i < 5; i++, now += 0.06) pump(&s, &a, NULL, now);
    assert(a.connected);
    static hta_net_world_state w;
    memset(&w, 0, sizeof(w));
    w.spatial_total = 3; w.flag_total = 2;
    for (unsigned i = 0; i < 3; i++) hta_net_bit_set(w.spatial_has, i, true);
    for (unsigned i = 0; i < 2; i++) hta_net_bit_set(w.flag_has, i, true);
    unsigned sent[64] = { 0 };
    /* tick 0: first -> sent; the next two repeat it; then quiet until the keyframe */
    for (int t = 0; t < 30; t++, now += 0.06) {
        pump(&s, &a, NULL, now);
        if (t == 25) hta_net_bit_set(w.flag, 1, true);           /* a relay goes active */
        sent[t] = hta_net_server_world_state(&s, &w);
    }
    for (int t = 0; t < 30; t++) {
        bool want = t <= 2 || t == 22 || (t >= 25 && t <= 27);
        if (sent[t] != want) { printf("tick %d sent %u\n", t, sent[t]); assert(0); }
    }
    pump(&s, &a, NULL, now); now += 0.06;
    assert(a.have_world_state && hta_net_bit(a.world_state.flag, 1));
    /* a mover moving: every 4th tick (plus the phase change's own three) */
    w.phase[2] = 1;
    memset(sent, 0, sizeof(sent));
    for (int t = 0; t < 16; t++, now += 0.06) {
        pump(&s, &a, NULL, now);
        w.t[2] = (uint16_t)(1000 * (t + 1));
        sent[t] = hta_net_server_world_state(&s, &w);
    }
    for (int t = 0; t < 16; t++) {
        bool want = t <= 2 || t == 6 || t == 10 || t == 14;
        if (sent[t] != want) { printf("moving tick %d sent %u\n", t, sent[t]); assert(0); }
    }
    /* a newcomer is sent the world on the next tick, whatever it is */
    w.phase[2] = 2; w.t[2] = 65535;
    for (int t = 0; t < 5; t++, now += 0.06) { pump(&s, &a, NULL, now); hta_net_server_world_state(&s, &w); }
    assert(hta_net_client_open(&b, "127.0.0.1", port));
    bool got = false;
    for (int t = 0; t < 6 && !got; t++, now += 0.06) {
        pump(&s, &a, &b, now);
        hta_net_server_world_state(&s, &w);
        hta_net_client_pump(&b, now + 0.001);
        got = b.have_world_state;
    }
    assert(b.connected && got && b.world_state.phase[2] == 2 && hta_net_bit(b.world_state.flag, 1));
    hta_net_client_close(&a); hta_net_client_close(&b); hta_net_server_close(&s);
}

/* v11: peers that speak another protocol are named, not just ignored. */
static void version_mismatch(void)
{
    uint8_t wire[HTA_NET_MAX_PACKET + 1];
    size_t n = 0;
    uint16_t v; uint8_t t;
    assert(hta_net_pack(wire, sizeof(wire), HTA_NET_PING, 1, 0, NULL, 0, &n));
    assert(hta_net_peek(wire, n, &v, &t) && v == HTA_NET_VERSION && t == HTA_NET_PING);
    wire[0] ^= 1; assert(!hta_net_peek(wire, n, &v, &t));
    assert(hta_net_pack_probe(wire, sizeof(wire), 10, 0xABCD, &n) && n == HTA_NET_HEADER + 4);
    assert(hta_net_peek(wire, n, &v, &t) && v == 10 && t == HTA_NET_DISCOVER);
    hta_net_packet p;
    assert(!hta_net_unpack(wire, n, &p));                                    /* not ours to read */
    assert(!hta_net_pack_probe(wire, sizeof(wire), HTA_NET_VERSION, 1, &n)); /* only older ones */
    assert(!hta_net_pack_probe(wire, sizeof(wire), HTA_NET_PROBE_OLDEST - 1, 1, &n));
    /* An older joiner's HELLO: refused with its version in the host's stats. */
    hta_net_server s;
    assert(hta_net_server_open(&s, 0));
    uint16_t port = hta_udp_port(&s.udp);
    hta_udp old; assert(hta_udp_open(&old, 0));
    hta_udp_addr to; assert(hta_udp_resolve(&to, "127.0.0.1", port));
    uint8_t hello[16] = { 0x44, 0x33, 0x22, 0x11 };
    assert(hta_net_pack(wire, sizeof(wire), HTA_NET_HELLO, 1, 0, hello, 16, &n));
    wire[4] = 10; wire[5] = 0;                                                /* as a v10 build sends it */
    assert(hta_udp_send(&old, &to, wire, n));
    for (int i = 0; i < 20 && !s.stats.refused; i++) hta_net_server_pump(&s, 1.0 + i * 0.001);
    assert(s.stats.refused == 1 && s.last_refusal == HTA_NET_REJECT_VERSION && s.last_refused_version == 10 &&
           hta_net_server_count(&s) == 0);
    /* its answer is ours (v11): the old build cannot read it, a peek can */
    hta_udp_addr from; int got = -1;
    for (int i = 0; i < 200 && got < 0; i++) got = hta_udp_recv(&old, wire, sizeof(wire), &from);
    assert(got > 0 && hta_net_unpack(wire, (size_t)got, &p) && p.type == HTA_NET_REJECT && p.length == 7 &&
           hta_net_u32_read(p.payload) == 0x11223344u && p.payload[4] == HTA_NET_REJECT_VERSION && p.payload[5] == HTA_NET_VERSION);
    hta_net_server_close(&s);
    /* An older host: it ignores our HELLO but answers the v10 probe in v10. */
    hta_net_client c;
    assert(hta_net_client_open(&c, "127.0.0.1", hta_udp_port(&old)));
    hta_net_client_pump(&c, 5.0);
    bool answered = false;
    for (int i = 0; i < 400; i++) {
        got = hta_udp_recv(&old, wire, sizeof(wire), &from);
        if (got < 0) continue;
        if (hta_net_peek(wire, (size_t)got, &v, &t) && v == 10 && t == HTA_NET_DISCOVER) {
            wire[6] = HTA_NET_INFO;                                          /* any v10 answer will do */
            assert(hta_udp_send(&old, &from, wire, (size_t)got));
            answered = true;
            break;
        }
    }
    assert(answered);
    for (int i = 0; i < 200 && !c.reject_reason; i++) hta_net_client_pump(&c, 5.001 + i * 0.001);
    assert(!c.connected && c.reject_reason == HTA_NET_REJECT_VERSION && c.peer_version == 10);
    hta_net_client_close(&c);
    hta_udp_close(&old);
}

int main(void) { codec(); world_codec(); world_state_codec(); world_state_fuzz(); world_state_policy(); version_mismatch(); interpolation(); sessions(); discovery(); puts("net: codec, world, interpolation, malformed input, two UDP clients, snapshots, events, ping, disconnect, LAN discovery, player cap, v11 WORLD_STATE (codec, fuzz, send policy), version refusal OK"); }
