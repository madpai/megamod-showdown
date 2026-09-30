#include "survival.h"
#include "progression_store.h"
#include "../platform/platform.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
static const mm_survival_def *definition(const hta_session *s) { return &s->world_ext.world_defs.survival; }
static bool authority(const hta_session *s) { return !s->net_enabled || s->net_hosting; }
static float distance(const float a[3],const float b[3]) { float v=0;for(int k=0;k<3;k++){float d=a[k]-b[k];v+=d*d;}return sqrtf(v); }
static int find_item(const hta_session *s,const char *id) {
    const mm_survival_def *d=definition(s);for(unsigned i=0;i<d->item_count;i++) if(!strcmp(d->item[i].id,id))return (int)i;
    return -1;
}
static int weapon(const hta_game *g,const char *id) {
    for(unsigned i=0;i<g->weapon_count && i<HTA_GAME_MAX_WEAPONS;i++) if(!strcmp(g->weapons[i].display,id)||!strcmp(g->weapons[i].def.path,id))return (int)i;
    return -1;
}
static float max_mana(const mm_progression *p) { return 100+10*sqrtf((float)p->upgrade[MM_MAGICKA])+50*mm_skill_bonus(p,MM_DESTRUCTION); }
static float max_stamina(const mm_progression *p) { return 100+10*sqrtf((float)p->upgrade[MM_STAMINA])+50*mm_skill_bonus(p,MM_ATHLETICS); }
static void equipment(hta_session *s,int unit,bool refill) {
    hta_unit *u=&s->game.units[unit];mm_progression *p=&s->survival.profile[unit];
    u->progression=p;u->team=HTA_TEAM_RED;u->armor=0;u->grenades=0;
    int a=find_item(s,p->equipment[2]);if(a>=0)u->armor=definition(s)->item[a].power;
    hta_game_apply_body(&s->game,unit);
    for(unsigned k=0;k<2;k++) {
        int i=find_item(s,p->equipment[k]);int w=i>=0?weapon(&s->game,definition(s)->item[i].asset):-1;
        if(w<0) {u->carry[k].weapon=-1;memset(&u->carry[k].ammo,0,sizeof(u->carry[k].ammo));}
        if(w>=0 && (u->carry[k].weapon!=w || refill)) {
            u->carry[k].weapon=w;hta_ammo_init(&u->carry[k].ammo,&s->game.weapons[w].def);
        }
    }
    if(u->carry[u->slot&1].weapon<0)u->slot=u->carry[0].weapon>=0?0:1;
    if(refill) {u->mana=max_mana(p);u->stamina=max_stamina(p);hta_vitals_reset(&u->vitals);}
}
static bool save(hta_session *s,int unit) {
    mm_survival *r=&s->survival;
    if(!r->bound[unit])return true;
    if(!mm_profile_save(r->directory,r->key[unit],&r->profile[unit],r->error,sizeof(r->error))) {r->result[unit]=MM_RESULT_SAVE;hta_log("[survival] %s",r->error);return false;}
    r->dirty[unit]=false;return true;
}
void hta_survival_flush(hta_session *s) {if(!authority(s))return;for(unsigned i=0;i<16;i++)if(s->survival.dirty[i])save(s,(int)i);}
bool hta_survival_join(hta_session *s,int unit,const uint8_t identity[16],bool solo) {
    if(unit<0||unit>=16||!s->survival.active||!authority(s))return false;
    mm_survival *r=&s->survival;char key[33];if(solo)strcpy(key,"solo");else {if(!identity)return false;bool nonzero=false;for(unsigned k=0;k<16;k++)nonzero=nonzero||identity[k]!=0;if(!nonzero)return false;mm_identity_hex(identity,key);}
    for(unsigned i=0;i<16;i++)if(i!=(unsigned)unit&&r->bound[i]&&!strcmp(r->key[i],key))return false;
    mm_progression p;mm_save_result result=mm_profile_load(r->directory,key,&p,r->error,sizeof(r->error));
    if(result==MM_SAVE_MISSING)mm_progression_init(&p);
    else if(result!=MM_SAVE_OK){hta_log("[survival] profile refused: %s",r->error);return false;}
    const mm_survival_def *d=definition(s);
    /* Authored zero-price entries are starter equipment, granted once by ID. */
    for(unsigned i=0;i<d->item_count;i++)if(!d->item[i].price && !mm_item_quantity(&p,d->item[i].id) && d->item[i].kind!=MM_ITEM_UPGRADE) {
        if(!mm_item_add(&p,d->item[i].id,1))return false;
        unsigned slot=d->item[i].kind==MM_ITEM_WEAPON?0:d->item[i].kind==MM_ITEM_ARMOR?2:3;
        if((!p.equipment[slot][0] || find_item(s,p.equipment[slot])<0) && (d->item[i].kind==MM_ITEM_WEAPON||d->item[i].kind==MM_ITEM_ARMOR||d->item[i].kind==MM_ITEM_SPELL))strcpy(p.equipment[slot],d->item[i].id);
    }
    r->profile[unit]=p;strcpy(r->key[unit],key);r->bound[unit]=true;r->dirty[unit]=true;r->request_seen[unit]=0;r->result[unit]=0;
    if(!save(s,unit)){r->bound[unit]=false;return false;}
    equipment(s,unit,true);memcpy(r->pos[unit],s->game.units[unit].body.pos,12);r->ground[unit]=true;r->jumped[unit]=false;r->practice[unit]=0;
    if(r->phase==MM_WAVE){s->game.units[unit].alive=false;s->game.units[unit].respawn=INFINITY;}return true;
}
void hta_survival_leave(hta_session *s,int unit) {if(unit<0||unit>=16)return;save(s,unit);s->survival.bound[unit]=false;s->game.units[unit].progression=NULL;}
bool hta_survival_begin(hta_session *s) {
    mm_survival *r=&s->survival;char dir[512];snprintf(dir,sizeof(dir),"%s",r->directory);memset(r,0,sizeof(*r));strcpy(r->directory,dir);
    r->active=s->game.mode==HTA_MODE_SURVIVAL && s->world_ext.world_defs.has_survival;if(!r->active)return false;
    r->phase=MM_REST;r->timer=definition(s)->rest_seconds;s->game.start_grenades=0;s->game.simulate_drops=false;s->game.spawn_protect=0;s->game.teams=true;
    if(!authority(s))return true;
    const mm_survival_def *d=definition(s);
    for(unsigned i=0;i<d->enemy_count;i++) {
        const mm_enemy_def *e=&d->enemy[i];bool found=!e->character[0];
        for(unsigned k=0;k<s->game.character_count;k++)if(!strcmp(e->character,s->game.characters[k]->display))found=true;
        if(!found || (e->weapon[0] && weapon(&s->game,e->weapon)<0)){snprintf(r->error,sizeof(r->error),"Missing enemy content: %s",e->name);return false;}
    }
    for(unsigned i=0;i<d->item_count;i++)if(d->item[i].kind==MM_ITEM_WEAPON && weapon(&s->game,d->item[i].asset)<0){snprintf(r->error,sizeof(r->error),"Missing shop weapon: %s",d->item[i].name);return false;}
    if(!dir[0]){snprintf(r->error,sizeof(r->error),"Gatebound needs a writable profile directory");return false;}
    if(s->me>=0) {
        uint8_t identity[16];bool solo=!s->net_enabled;
        if(!solo&&!mm_profile_identity(dir,identity,r->error,sizeof(r->error)))return false;
        if(!hta_survival_join(s,s->me,identity,solo))return false;
    }
    return true;
}
uint64_t hta_survival_price(const hta_session *s,int unit,unsigned item) {
    const mm_survival_def *d=definition(s);if(unit<0||unit>=16||item>=d->item_count)return UINT64_MAX;
    const mm_shop_item *i=&d->item[item];if(i->kind!=MM_ITEM_UPGRADE)return i->price;
    uint64_t t=(uint64_t)s->survival.profile[unit].upgrade[i->upgrade]+1;
    if(t>UINT64_MAX/t)return UINT64_MAX;
    t*=t;
    return i->price && t>UINT64_MAX/i->price?UINT64_MAX:t*i->price;
}
bool hta_survival_can_shop(const hta_session *s,int unit) {
    return s->survival.active && unit>=0 && unit<16 && s->game.units[unit].alive && s->survival.phase==MM_REST && distance(s->game.units[unit].body.pos,definition(s)->shop)<=4;
}
uint8_t hta_survival_request(hta_session *s,int unit,uint8_t action,unsigned item,uint16_t serial) {
    mm_survival *r=&s->survival;
    if(!authority(s)||!r->active||unit<0||unit>=16||!r->bound[unit])return MM_RESULT_UNAVAILABLE;
    if(!serial||serial==r->request_seen[unit])return r->result[unit];
    /* Accept a forward half-range counter only; stale replay cannot buy again. */
    if((uint16_t)(serial-r->request_seen[unit])>=32768)return r->result[unit];
    r->request_seen[unit]=serial;uint8_t result=MM_RESULT_UNAVAILABLE;
    hta_unit *u=&s->game.units[unit];float old_health=u->vitals.health,old_mana=u->mana;mm_progression before=r->profile[unit],*p=&r->profile[unit];
    const mm_survival_def *d=definition(s);
    if(!u->alive)return r->result[unit]=result;
    if(action==MM_REQUEST_PRESTIGE) {if(r->phase==MM_REST && distance(u->body.pos,d->shop)<=4 && mm_prestige(p))result=MM_RESULT_OK;}
    else if(item<d->item_count && item<MM_SHOP_ITEMS) {
        const mm_shop_item *i=&d->item[item];
        char asset[64];memcpy(asset,i->asset,sizeof(asset));
        if(action==MM_REQUEST_BUY && hta_survival_can_shop(s,unit)) {
            uint64_t price=hta_survival_price(s,unit,item);
            if(i->kind==MM_ITEM_WEAPON && weapon(&s->game,asset)<0)result=MM_RESULT_UNAVAILABLE;
            else if(i->kind==MM_ITEM_UPGRADE && p->upgrade[i->upgrade]==UINT32_MAX)result=MM_RESULT_UNAVAILABLE;
            else if(i->kind!=MM_ITEM_UPGRADE && i->kind!=MM_ITEM_HEALTH && i->kind!=MM_ITEM_MANA && mm_item_quantity(p,i->id))result=MM_RESULT_OWNED;
            else if(price==UINT64_MAX||price>p->gold)result=MM_RESULT_GOLD;
            else if(i->kind==MM_ITEM_UPGRADE || mm_item_add(p,i->id,1)) {p->gold-=price;if(i->kind==MM_ITEM_UPGRADE)p->upgrade[i->upgrade]++;result=MM_RESULT_OK;}
        } else if(action==MM_REQUEST_EQUIP && mm_item_quantity(p,i->id)) {
            unsigned slot=i->kind==MM_ITEM_WEAPON?(u->slot&1):i->kind==MM_ITEM_ARMOR?2:3;
            if(i->kind==MM_ITEM_WEAPON && weapon(&s->game,asset)<0)result=MM_RESULT_UNAVAILABLE;
            else if(i->kind==MM_ITEM_WEAPON||i->kind==MM_ITEM_ARMOR||i->kind==MM_ITEM_SPELL){strcpy(p->equipment[slot],i->id);result=MM_RESULT_OK;}
        } else if(action==MM_REQUEST_USE && mm_item_quantity(p,i->id)) {
            if(i->kind==MM_ITEM_HEALTH && u->vitals.health<u->vitals.max_health) {u->vitals.health=fminf(u->vitals.max_health,u->vitals.health+i->power*u->vitals.max_health);result=MM_RESULT_OK;}
            if(i->kind==MM_ITEM_MANA && u->mana<max_mana(p)){u->mana=fminf(max_mana(p),u->mana+i->power);result=MM_RESULT_OK;}
            if(result==MM_RESULT_OK)mm_item_take(p,i->id,1);
        }
    }
    if(result==MM_RESULT_OK){r->dirty[unit]=true;if(!save(s,unit)){*p=before;u->vitals.health=old_health;u->mana=old_mana;result=MM_RESULT_SAVE;}else equipment(s,unit,false);}
    return r->result[unit]=result;
}
static bool visible(const hta_game *g,const float from[3],const float to[3]) {
    float dir[3],length=distance(from,to);if(length<.01f)return true;
    for(int k=0;k<3;k++)dir[k]=(to[k]-from[k])/length;
    float t;return !g->col || !hta_collision_ray(g->col,from,dir,fmaxf(0,length-.2f),&t,NULL,NULL);
}
bool hta_survival_cast(hta_session *s,int unit) {
    if(!authority(s)||!s->survival.active||unit<0||unit>=16||!s->survival.bound[unit])return false;
    hta_unit *u=&s->game.units[unit];mm_progression *p=u->progression;int n=find_item(s,p->equipment[3]);
    if(!u->alive||n<0||u->rpg_cast_wait>0)return false;
    const mm_shop_item *i=&definition(s)->item[n];if(i->kind!=MM_ITEM_SPELL||u->mana<i->cost)return false;
    float endpoint[3];memcpy(endpoint,u->eye.pos,12);endpoint[2]-=.3f;
    float power=i->power*s->game.vitals_template.max_health;bool worked=false;
    if(i->effect==MM_EFFECT_HEAL) {float healed=fminf(u->vitals.max_health-u->vitals.health,i->power*u->vitals.max_health*(1+mm_skill_bonus(p,MM_RESTORATION)));if(healed>0){u->vitals.health+=healed;mm_practice(p,MM_RESTORATION,(uint64_t)ceilf(healed/u->vitals.max_health*100));worked=true;}}
    else if(i->effect==MM_EFFECT_WARD){u->ward=i->duration;worked=true;}
    else {
        float fwd[3];hta_camera_forward(&u->eye,fwd);int target=-1;float best=i->range;
        for(unsigned k=0;k<s->game.unit_count;k++) {hta_unit *v=&s->game.units[k];if(!v->alive||v->kind!=HTA_UNIT_BOT||v->team==u->team)continue;
            float pt[3];hta_game_centre(&s->game,(int)k,pt);float dist=distance(u->eye.pos,pt),dot=0;
            for(int c=0;c<3;c++)dot+=(pt[c]-u->eye.pos[c])*fwd[c];
            if(dist<best && dot>dist*.93f && visible(&s->game,u->eye.pos,pt)){best=dist;target=(int)k;}}
        for(int k=0;k<3;k++)endpoint[k]=u->eye.pos[k]+fwd[k]*i->range;
        float wall;if(s->game.col && hta_collision_ray(s->game.col,u->eye.pos,fwd,i->range,&wall,NULL,NULL))for(int k=0;k<3;k++)endpoint[k]=u->eye.pos[k]+fwd[k]*wall;
        worked=true; /* A missed offensive spell still casts/spends mana; XP comes only from damage. */
        if(target>=0){float center[3];hta_game_centre(&s->game,target,center);memcpy(endpoint,center,12);
            for(unsigned k=0;k<s->game.unit_count;k++) {hta_unit *v=&s->game.units[k];if(!v->alive||v->kind!=HTA_UNIT_BOT||v->team==u->team)continue;
                float pt[3];hta_game_centre(&s->game,(int)k,pt);
                if((int)k!=target && (i->effect!=MM_EFFECT_CHAIN || distance(center,pt)>i->radius || !visible(&s->game,center,pt)))continue;
                uint8_t old=s->game.practice_skill;s->game.practice_skill=MM_DESTRUCTION;
                hta_game_hurt(&s->game,(int)k,unit,power,pt);s->game.practice_skill=old;
                if(i->effect==MM_EFFECT_FROST)v->slow=i->duration;
            }worked=true;}
    }
    if(worked){
        hta_wfx_spell(&s->wfx,i->effect,u->eye.pos,endpoint);
        if(s->net_hosting){hta_net_fx fx={.kind=HTA_NET_FX_SPELL,.entity=(uint16_t)unit,.weapon=(uint8_t)n,.material=i->effect};memcpy(fx.pos,u->eye.pos,12);memcpy(fx.dir,endpoint,12);hta_net_server_fx(&s->host_server,&fx);}
        u->mana-=i->cost;u->rpg_cast_wait=.6f;s->survival.dirty[unit]=true;hta_log("[survival] unit %d cast %s",unit,i->name);}return worked;
}
void hta_survival_event(hta_session *s,const hta_game_event *e) {
    mm_survival *r=&s->survival;if(!r->active||!authority(s))return;
    if(e->kind==HTA_EV_KILL && e->a>=0 && e->a<16 && s->game.units[e->a].kind==HTA_UNIT_BOT) {
        if(e->b>=0 && e->b<16 && r->bound[e->b]) {
            mm_progression *p=&r->profile[e->b];unsigned type=r->enemy_type[e->a];
            uint64_t gold=definition(s)->enemy[type].gold;
            gold+=(uint64_t)((double)gold*.25*mm_skill_bonus(p,MM_ATHLETICS))+(uint64_t)sqrt((double)p->upgrade[MM_FORTUNE]);
            mm_award_gold(p,gold);if(p->kills<UINT64_MAX)p->kills++;r->dirty[e->b]=true;
        }
        hta_game_remove(&s->game,e->a);
    }
    if(e->kind==HTA_EV_HIT_UNIT && e->a>=0 && e->a<16 && r->bound[e->a])r->dirty[e->a]=true;
    if(e->kind==HTA_EV_HIT_UNIT && e->b>=0 && e->b<16 && r->bound[e->b])r->dirty[e->b]=true;
}
static void rest(hta_session *s,bool wipe) {
    mm_survival *r=&s->survival;const mm_survival_def *d=definition(s);
    for(unsigned i=0;i<s->game.unit_count;i++)if(s->game.units[i].kind==HTA_UNIT_BOT)hta_game_remove(&s->game,(int)i);
    r->queued=0;r->phase=wipe?MM_WIPE:MM_REST;r->timer=wipe?8:d->rest_seconds;
    if(wipe)r->wave=0;
    else for(unsigned i=0;i<16;i++)if(r->bound[i]){mm_award_gold(&r->profile[i],25+(uint64_t)r->wave*5);if(r->profile[i].waves<UINT64_MAX)r->profile[i].waves++;r->dirty[i]=true;}
    hta_survival_flush(s);
    if(!wipe)for(unsigned i=0;i<16;i++)if(r->bound[i]){hta_game_spawn(&s->game,(int)i);equipment(s,(int)i,true);}
    hta_log("[survival] %s, wave %u",wipe?"party defeated":"wave clear",r->wave);
}
/* Environmental defeat uses the normal death/replication path. No practice
 * or gold is awarded for escaping the playable world's bounds. */
void hta_survival_guard(hta_session *s) {
    if(!s->survival.active || !authority(s) || !s->mesh.vertex_count)return;
    for(unsigned k=0;k<s->game.unit_count;k++) {
        hta_unit *u=&s->game.units[k];if(!u->alive || u->kind==HTA_UNIT_NONE)continue;
        bool escaped=false;
        for(unsigned c=0;c<3;c++)if(!isfinite(u->body.pos[c]))escaped=true;
        if(u->body.pos[2]<s->mesh.bounds_min[2]-2)escaped=true;
        for(unsigned c=0;c<2;c++)if(u->body.pos[c]<s->mesh.bounds_min[c]-2 || u->body.pos[c]>s->mesh.bounds_max[c]+2)escaped=true;
        if(!escaped)continue;
        hta_log("[survival] unit %u escaped at (%.2f %.2f %.2f), velocity (%.2f %.2f %.2f)",k,u->body.pos[0],u->body.pos[1],u->body.pos[2],u->body.velocity[0],u->body.velocity[1],u->body.velocity[2]);
        u->last_attacker=-1;u->vitals.health=u->vitals.shield=0;u->vitals.died=true;
        memset(u->knock,0,sizeof(u->knock));
    }
}
void hta_survival_tick(hta_session *s,float dt) {
    mm_survival *r=&s->survival;if(!r->active||!authority(s)||dt<=0)return;dt=fminf(dt,.1f);
    const mm_survival_def *d=definition(s);unsigned players=0,living=0,enemies=0;
    for(unsigned k=0;k<s->game.unit_count;k++) {
        hta_unit *u=&s->game.units[k];if(u->kind==HTA_UNIT_NONE)continue;
        u->ward=fmaxf(0,u->ward-dt);u->slow=fmaxf(0,u->slow-dt);u->rpg_cast_wait=fmaxf(0,u->rpg_cast_wait-dt);
        if(u->kind==HTA_UNIT_BOT){if(u->alive)enemies++;continue;}
        if(!r->bound[k])continue;
        players++;if(u->alive)living++;
        if(!u->alive){u->respawn=INFINITY;continue;}
        mm_progression *p=u->progression;u->mana=fminf(max_mana(p),u->mana+5*dt);u->stamina=fminf(max_stamina(p),u->stamina+15*dt);
        float moved=distance(u->body.pos,r->pos[k]);
        if(u->body.on_ground && r->ground[k] && !u->body.fly && u->vehicle<0 && moved<dt*20 && moved>.001f && (fabsf(u->in.move.move_forward)+fabsf(u->in.move.move_right)>.05f)) {
            r->practice[k]+=moved*10;uint64_t xp=(uint64_t)r->practice[k];if(xp){mm_practice(p,MM_ATHLETICS,xp);r->practice[k]-=(float)xp;r->dirty[k]=true;}}
        if(r->ground[k] && !u->body.on_ground && u->body.velocity[2]>0 && u->in.move.jump && u->stamina>=10){r->jumped[k]=true;u->stamina-=10;}
        if(!r->ground[k] && u->body.on_ground && r->jumped[k]){mm_practice(p,MM_ACROBATICS,25);r->dirty[k]=true;r->jumped[k]=false;}
        if(u->body.fly||u->vehicle>=0)r->jumped[k]=false;
        r->ground[k]=u->body.on_ground;memcpy(r->pos[k],u->body.pos,12);
        equipment(s,(int)k,false);
    }
    r->save_timer+=dt;if(r->save_timer>=10){r->save_timer=0;hta_survival_flush(s);}
    if(!players)return;
    if(r->phase!=MM_WAVE) {
        r->timer-=dt;if(r->timer>0)return;
        if(r->phase==MM_WIPE){r->phase=MM_REST;r->timer=d->rest_seconds;for(unsigned i=0;i<16;i++)if(r->bound[i]){hta_game_spawn(&s->game,(int)i);equipment(s,(int)i,true);}return;}
        if(r->wave<UINT32_MAX)r->wave++;
        uint64_t total=d->base_enemies+(uint64_t)d->wave_enemies*(r->wave-1);total+=(players-1)*total/2;
        r->queued=total>UINT32_MAX?UINT32_MAX:(uint32_t)total;r->spawned=0;r->phase=MM_WAVE;r->spawn_timer=0;
        hta_log("[survival] wave %u, %u enemies",r->wave,r->queued);
    }
    if(!living){rest(s,true);return;}
    if(!enemies && !r->queued){rest(s,false);return;}
    r->spawn_timer-=dt;
    if(r->queued && enemies<8 && r->spawn_timer<=0) {
        unsigned type=r->spawned%d->enemy_count;const mm_enemy_def *e=&d->enemy[type];
        int unit=hta_game_add(&s->game,HTA_UNIT_BOT,e->name,HTA_TEAM_BLUE);
        if(unit>=0) {
            hta_unit *u=&s->game.units[unit];u->rpg_health=e->health*(1+.15f*sqrtf((float)r->wave));u->rpg_speed=e->speed;u->rpg_damage=e->damage*(1+.08f*sqrtf((float)r->wave));
            for(unsigned c=0;c<s->game.character_count;c++)if(!strcmp(s->game.characters[c]->display,e->character)){u->character=(int8_t)c;break;}
            hta_game_spawn(&s->game,unit);u->protect=0;
            /* Generic bot spawning can roll a random two-gun class. Wave
             * definitions own the entire loadout, including empty slots. */
            for(unsigned slot=0;slot<2;slot++){u->carry[slot].weapon=-1;memset(&u->carry[slot].ammo,0,sizeof(u->carry[slot].ammo));}
            u->grenades=0;u->slot=0;
            int w=weapon(&s->game,e->weapon);if(w>=0){u->carry[0].weapon=w;hta_ammo_init(&u->carry[0].ammo,&s->game.weapons[w].def);}
            memcpy(u->body.pos,d->gates[r->spawned%d->gate_count],12);memcpy(u->eye.pos,u->body.pos,12);u->eye.pos[2]+=u->body.eye_height;
            r->enemy_type[unit]=(uint8_t)type;r->spawned++;r->queued--;r->spawn_timer=d->spawn_interval;
            hta_log("[survival] gate %u spawned %s",(r->spawned-1)%d->gate_count,e->name);
        }
    }
}
void hta_survival_text(const hta_session *s,int unit,char *out,size_t n) {
    const mm_survival *r=&s->survival;if(!r->active||unit<0||unit>=16){if(n)out[0]=0;return;}
    const mm_progression *p=&r->profile[unit];
    unsigned remaining=r->queued;
    if(authority(s) && r->phase==MM_WAVE)for(unsigned k=0;k<s->game.unit_count;k++)if(s->game.units[k].kind==HTA_UNIT_BOT && s->game.units[k].alive)remaining++;
    char phase[64];if(r->phase==MM_WAVE)snprintf(phase,sizeof(phase),"Enemies %u",remaining);
    else snprintf(phase,sizeof(phase),"%s %.0fs",r->phase==MM_WIPE?"Defeated":"Shop",r->timer);
    snprintf(out,n,"Gatebound | Wave %u | %s | Gold %" PRIu64 " | Level %u | Prestige %u\nMagicka %.0f | Stamina %.0f",r->wave,phase,p->gold,mm_character_level(p),p->prestige,s->game.units[unit].mana,s->game.units[unit].stamina);
}

void hta_survival_snapshot(const hta_session *s,int unit,hta_net_rpg *out) {
    const mm_survival *r=&s->survival;const mm_progression *p=&r->profile[unit];const hta_unit *u=&s->game.units[unit];
    *out=(hta_net_rpg){0};out->wave=r->wave;out->queued=r->queued;out->phase=r->phase;out->timer=fmaxf(0,r->timer);
    if(r->phase==MM_WAVE)for(unsigned k=0;k<s->game.unit_count;k++)if(s->game.units[k].kind==HTA_UNIT_BOT && s->game.units[k].alive)out->queued++;
    out->serial=r->request_seen[unit];out->result=r->result[unit];out->gold=p->gold;out->experience=p->experience;out->prestige=p->prestige;
    memcpy(out->skill,p->skill,sizeof(out->skill));memcpy(out->upgrade,p->upgrade,sizeof(out->upgrade));out->mana=u->mana;out->stamina=u->stamina;out->max_health=u->vitals.max_health;
    for(unsigned i=0;i<definition(s)->item_count;i++)out->quantity[i]=mm_item_quantity(p,definition(s)->item[i].id);
    for(unsigned k=0;k<4;k++){int i=find_item(s,p->equipment[k]);out->equipment[k]=i<0?255:(uint8_t)i;}
}
void hta_survival_apply(hta_session *s,int unit,const hta_net_rpg *in) {
    if(authority(s)||unit<0||unit>=16)return;
    mm_survival *r=&s->survival;mm_progression *p=&r->profile[unit];
    r->active=true;r->wave=in->wave;r->queued=in->queued;r->phase=in->phase;r->timer=in->timer;r->request_seen[unit]=in->serial;r->result[unit]=in->result;
    mm_progression_init(p);p->gold=in->gold;p->experience=in->experience;p->prestige=in->prestige;
    memcpy(p->skill,in->skill,sizeof(p->skill));memcpy(p->upgrade,in->upgrade,sizeof(p->upgrade));
    for(unsigned i=0;i<definition(s)->item_count;i++)if(in->quantity[i])mm_item_add(p,definition(s)->item[i].id,in->quantity[i]);
    for(unsigned i=0;i<4;i++)if(in->equipment[i]<definition(s)->item_count)strcpy(p->equipment[i],definition(s)->item[in->equipment[i]].id);
    s->game.units[unit].mana=in->mana;s->game.units[unit].stamina=in->stamina;equipment(s,unit,false);s->game.units[unit].vitals.max_health=in->max_health;
}
