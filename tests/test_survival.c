#define _POSIX_C_SOURCE 200809L
#include "app/survival.h"
#include "app/host_net.h"
#include "app/progression_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
static hta_session *fixture(const char *dir) {
    hta_session *s=calloc(1,sizeof(*s));assert(s);s->me=-1;s->game.local=-1;s->game.loaded=true;
    s->game.vitals_template=(hta_vitals){.max_health=1,.loaded=true};s->game.start_weapon[0]=s->game.start_weapon[1]=-1;
    hta_game_set_mode(&s->game,HTA_MODE_SURVIVAL);s->world_ext.world_defs.has_survival=true;
    mm_survival_def *d=&s->world_ext.world_defs.survival;d->rest_seconds=5;d->spawn_interval=.1f;d->base_enemies=1;d->wave_enemies=1;d->gate_count=1;d->gates[0][0]=2;d->enemy_count=1;
    d->enemy[0]=(mm_enemy_def){.name="Original foe",.health=1,.speed=1,.damage=1,.gold=25};d->item_count=3;
    d->item[0]=(mm_shop_item){.id="test:spell/ember",.name="Ember",.kind=MM_ITEM_SPELL,.power=.3f,.cost=20,.range=10};
    d->item[1]=(mm_shop_item){.id="test:upgrade/health",.name="Health",.kind=MM_ITEM_UPGRADE,.price=150};
    d->item[2]=(mm_shop_item){.id="test:spell/mend",.name="Mend",.kind=MM_ITEM_SPELL,.effect=MM_EFFECT_HEAL,.power=.5f,.cost=20};
    assert(mm_survival_valid(d));snprintf(s->survival.directory,sizeof(s->survival.directory),"%s",dir);assert(hta_survival_begin(s));return s;
}
static void network_profiles(const char *dir) {
    hta_session *s=fixture(dir);s->net_enabled=s->net_hosting=s->game_on=true;
    for(unsigned i=0;i<8;i++)s->peer_unit[i]=-1;
    assert(hta_net_server_open(&s->host_server,0));uint16_t port=hta_udp_port(&s->host_server.udp);
    hta_net_client *c=calloc(1,sizeof(*c)),*other=calloc(1,sizeof(*other));assert(c&&other);
    assert(hta_net_client_open(c,"127.0.0.1",port));memset(c->identity,42,16);
    double now=1;for(unsigned i=0;i<8&&!c->connected;i++,now+=.05){hta_net_client_pump(c,now);hta_net_server_pump(&s->host_server,now);hta_net_client_pump(c,now);}
    assert(c->connected);hta_host_peers(s,now,NULL);int unit=s->peer_unit[c->id-1];assert(unit>=0);assert(s->survival.profile[unit].gold==100);
    mm_award_gold(&s->survival.profile[unit],300);
    hta_net_control request={.id=c->id,.loadout={255,255},.rpg_serial=1,.rpg_action=MM_REQUEST_BUY,.rpg_item=1};
    for(unsigned i=0;i<3;i++){assert(hta_net_client_control(c,&request));hta_net_server_pump(&s->host_server,now);hta_host_peers(s,now,NULL);now+=.05;}
    assert(s->survival.profile[unit].gold==250 && s->survival.profile[unit].upgrade[0]==1);
    s->host_server.tick=1;hta_host_world(s);hta_net_client_pump(c,now);assert(c->have_rpg && c->rpg.gold==250 && c->rpg.serial==1);
    assert(hta_net_client_open(other,"127.0.0.1",port));memset(other->identity,42,16);
    for(unsigned i=0;i<8&&!other->reject_reason;i++,now+=.05){hta_net_client_pump(other,now);hta_net_server_pump(&s->host_server,now);hta_net_client_pump(other,now);}
    assert(other->reject_reason==HTA_NET_REJECT_IDENTITY);hta_net_client_close(other);
    hta_net_client_close(c);hta_net_server_pump(&s->host_server,now);hta_host_peers(s,now,NULL);
    assert(!s->survival.bound[unit]);
    assert(hta_net_client_open(c,"127.0.0.1",port));memset(c->identity,42,16);
    for(unsigned i=0;i<8&&!c->connected;i++,now+=.05){hta_net_client_pump(c,now);hta_net_server_pump(&s->host_server,now);hta_net_client_pump(c,now);}
    assert(c->connected);hta_host_peers(s,now,NULL);unit=s->peer_unit[c->id-1];assert(unit>=0);
    assert(s->survival.profile[unit].gold==250 && s->survival.profile[unit].upgrade[0]==1);
    hta_net_client_close(c);hta_net_server_close(&s->host_server);hta_survival_flush(s);free(c);free(other);free(s);
    char file[1024],key[33];uint8_t id[16];memset(id,42,16);mm_identity_hex(id,key);snprintf(file,sizeof(file),"%s/%s.mrp",dir,key);unlink(file);
}
static void loadout_and_escape(const char *dir) {
    hta_session *s=fixture(dir);s->game_on=true;
    s->game.weapon_count=2;s->game.start_weapon[0]=0;s->game.start_weapon[1]=1;
    strcpy(s->game.weapons[0].display,"Authored sword");strcpy(s->game.weapons[1].display,"Unwanted rocket");
    strcpy(s->world_ext.world_defs.survival.enemy[0].weapon,"Authored sword");
    int player=hta_game_add(&s->game,HTA_UNIT_LOCAL,"Player",HTA_TEAM_RED);hta_game_spawn(&s->game,player);
    assert(hta_survival_join(s,player,NULL,true));s->survival.timer=0;hta_survival_tick(s,.1f);
    hta_unit *u=&s->game.units[player],*enemy=&s->game.units[1];
    assert(enemy->carry[0].weapon==0 && enemy->carry[1].weapon==-1 && enemy->grenades==0);
    mm_progression before=*u->progression;
    s->mesh.vertex_count=1;for(unsigned c=0;c<3;c++){s->mesh.bounds_min[c]=-12;s->mesh.bounds_max[c]=12;}
    s->mesh.bounds_min[2]=-.4f;
    /* A jump above the wall is legal; an endless fall is not. Protection
     * and armor must not defeat the environmental guard. */
    u->body.pos[2]=4.75f;hta_survival_guard(s);assert(u->alive && u->vitals.health>0);
    u->protect=10;u->armor=.85f;u->ward=10;u->body.pos[2]=-57.03f;
    hta_survival_guard(s);assert(u->vitals.died && u->vitals.health==0);
    hta_game_update(&s->game,.01f);assert(!u->alive);
    hta_game_event event;while(hta_game_pop(&s->game,&event))hta_survival_event(s,&event);
    hta_survival_tick(s,.1f);assert(s->survival.phase==MM_WIPE);
    assert(!memcmp(&before,u->progression,sizeof(before)));
    s->survival.timer=0;hta_survival_tick(s,.1f);assert(u->alive && u->body.pos[2]>-1 && s->survival.phase==MM_REST);
    /* Escaped NPCs cannot stall a wave or mint a kill reward. */
    s->survival.timer=0;hta_survival_tick(s,.1f);enemy=&s->game.units[1];uint64_t gold=u->progression->gold,kills=u->progression->kills;
    enemy->body.pos[0]=15;hta_survival_guard(s);hta_game_update(&s->game,.01f);
    while(hta_game_pop(&s->game,&event))hta_survival_event(s,&event);
    assert(enemy->kind==HTA_UNIT_NONE && u->progression->gold==gold && u->progression->kills==kills);
    /* A peer never decides environmental deaths itself. */
    s->net_enabled=true;s->net_hosting=false;u->body.pos[2]=-100;hta_survival_guard(s);assert(u->alive && u->vitals.health>0);
    s->net_enabled=false;hta_survival_flush(s);free(s);
}
int main(void) {
    char dir[]="/tmp/megamod-survival-XXXXXX",err[160],file[1024];assert(mkdtemp(dir));hta_session *s=fixture(dir);
    int human=hta_game_add(&s->game,HTA_UNIT_REMOTE,"Player",0);assert(human==0);hta_game_spawn(&s->game,human);assert(hta_survival_join(s,human,NULL,true));
    mm_progression *p=s->game.units[0].progression;mm_award_gold(p,300);
    assert(hta_survival_request(s,0,MM_REQUEST_BUY,1,1)==MM_RESULT_OK && p->gold==250);
    assert(hta_survival_request(s,0,MM_REQUEST_BUY,1,1)==MM_RESULT_OK && p->gold==250 && p->upgrade[0]==1);
    assert(hta_survival_request(s,0,MM_REQUEST_BUY,1,2)==MM_RESULT_GOLD && p->upgrade[0]==1);
    assert(hta_survival_request(s,0,MM_REQUEST_BUY,1,1)==MM_RESULT_GOLD && p->gold==250);
    s->survival.timer=0;hta_survival_tick(s,.1f);assert(s->survival.phase==MM_WAVE && s->game.unit_count==2);
    hta_unit *u=&s->game.units[0],*v=&s->game.units[1];u->eye.pos[0]=0;u->eye.pos[2]=v->body.pos[2]+v->body.eye_height*.5f;u->eye.yaw=u->eye.pitch=0;
    v->vitals.health=.5f;assert(hta_survival_cast(s,0));assert(u->mana==80 && p->skill[MM_DESTRUCTION]>0);
    uint64_t xp=p->skill[MM_DESTRUCTION];assert(!hta_survival_cast(s,0));assert(p->skill[MM_DESTRUCTION]==xp);
    /* A missed offensive cast is visible and consumes mana, without practice. */
    u->rpg_cast_wait=0;u->eye.yaw=3.14159265f;float before_miss=u->mana;
    assert(hta_survival_cast(s,0));assert(u->mana==before_miss-20 && p->skill[MM_DESTRUCTION]==xp);
    u->eye.yaw=0;
    float health=v->vitals.health;hta_game_hurt(&s->game,1,0,100,NULL);assert(p->skill[MM_MARKSMANSHIP]==(uint64_t)ceilf(health/v->vitals.max_health*100));
    /* Friendly fire never yields practice or damage. */
    int friend=hta_game_add(&s->game,HTA_UNIT_REMOTE,"Friend",0);hta_game_spawn(&s->game,friend);float hp=s->game.units[friend].vitals.health;
    hta_game_hurt(&s->game,friend,0,100,NULL);assert(s->game.units[friend].vitals.health==hp);
    hta_game_event death={.kind=HTA_EV_KILL,.a=1,.b=0};hta_survival_event(s,&death);assert(p->gold==275 && p->kills==1);
    hta_survival_tick(s,.1f);assert(s->survival.phase==MM_REST && p->waves==1 && u->alive);
    assert(hta_survival_request(s,0,MM_REQUEST_EQUIP,2,3)==MM_RESULT_OK);u->rpg_cast_wait=0;u->vitals.health=u->vitals.max_health*.5f;
    assert(hta_survival_cast(s,0));assert(p->skill[MM_RESTORATION]>0);u->rpg_cast_wait=0;xp=p->skill[MM_RESTORATION];float mana=u->mana;
    assert(!hta_survival_cast(s,0)&&p->skill[MM_RESTORATION]==xp&&u->mana==mana);
    hta_survival_flush(s);mm_progression saved=*p;hta_survival_leave(s,0);assert(hta_survival_join(s,0,NULL,true));assert(!memcmp(&saved,s->game.units[0].progression,sizeof(saved)));
    hta_net_rpg r;hta_survival_snapshot(s,0,&r);uint8_t packet[HTA_NET_RPG_BYTES];assert(hta_net_rpg_pack(packet,sizeof(packet),&r));hta_net_rpg copy;assert(hta_net_rpg_unpack(packet,sizeof(packet),&copy));assert(copy.gold==saved.gold && copy.skill[MM_RESTORATION]==saved.skill[MM_RESTORATION]);packet[8]=255;assert(!hta_net_rpg_unpack(packet,sizeof(packet),&copy));
    s->survival.phase=MM_WAVE;u->alive=false;s->game.units[friend].alive=false;hta_survival_tick(s,.1f);assert(s->survival.phase==MM_WIPE && s->survival.wave==0 && p->gold==saved.gold);
    hta_survival_leave(s,0);free(s);snprintf(file,sizeof(file),"%s/solo.mrp",dir);unlink(file);(void)err;
    network_profiles(dir);loadout_and_escape(dir);snprintf(file,sizeof(file),"%s/solo.mrp",dir);unlink(file);
    s=fixture(dir);snprintf(s->world_ext.world_defs.survival.enemy[0].character,sizeof(s->world_ext.world_defs.survival.enemy[0].character),"Missing donor");
    assert(!hta_survival_begin(s));assert(strstr(s->survival.error,"Missing enemy content"));free(s);
    rmdir(dir);
    puts("survival: shop replay, gold rejection, wave spawning/recovery/wipe, spells, actual practice and reconnect passed");
}
