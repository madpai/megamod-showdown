#include "../src/engine/arcade_racer.h"
#include "../src/game/race.h"
#include "../src/asset/world_def.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void package_parser(void)
{
    const char *valid="{\"world_entities\":{\"schema\":8,\"entities\":[],\"racing\":{"
        "\"laps\":3,\"grid\":[{\"position\":[-2,0,0],\"yaw\":0}],"
        "\"gates\":["
        "{\"id\":\"gate_00\",\"position\":[0,0,0],\"forward\":[1,0],\"half_width\":5,\"half_height\":3,\"recovery\":[-1,0,0],\"recovery_yaw\":0},"
        "{\"id\":\"gate_01\",\"position\":[10,0,0],\"forward\":[1,0],\"half_width\":5,\"half_height\":3,\"recovery\":[9,0,0],\"recovery_yaw\":0}],"
        "\"pads\":[{\"position\":[5,0,0],\"radius\":2}],\"vehicle\":{\"max_speed\":38}}}}";
    hta_world_defs *d=calloc(1,sizeof(*d));
    char err[512]; assert(d);
    if (!hta_world_defs_parse((const uint8_t *)valid,strlen(valid),d,err,sizeof(err))) {
        fprintf(stderr,"race package parser: %s\n",err); abort();
    }
    assert(d->has_racing && d->racing.gate_count==2 && d->racing.pad_count==1);
    char invalid[2048];
    snprintf(invalid,sizeof(invalid),"%s",valid);
    char *p=strstr(invalid,"\"schema\":8"); assert(p); p[9]='7';
    assert(!hta_world_defs_parse((const uint8_t *)invalid,strlen(invalid),d,err,sizeof(err)));
    assert(strstr(err,"needs schema 8"));
    /* Treat authored race data as untrusted: mutated JSON must either
     * fail cleanly or resolve to a still-valid bounded track. */
    unsigned rng=0x10ace123u;
    for (unsigned i=0;i<2000;i++) {
        snprintf(invalid,sizeof(invalid),"%s",valid);
        rng=rng*1664525u+1013904223u;
        unsigned at=rng%(unsigned)strlen(invalid);
        rng=rng*1664525u+1013904223u;
        invalid[at]=(char)(32u+rng%95u);
        if (hta_world_defs_parse((const uint8_t *)invalid,strlen(invalid),d,err,sizeof(err)) &&
            d->has_racing)
            assert(hta_race_track_valid(&d->racing));
    }
    free(d);
}

static hta_race_track route(void)
{
    hta_race_track t={0};
    t.gate_count=3; t.lap_count=2; t.grid_count=2;
    t.vehicle=hta_arcade_default_tuning();
    t.grid[0][0]=-2; t.grid[1][0]=-2;
    t.grid[1][1]=1.5f;
    for (int i=0;i<3;i++) {
        t.gate[i].pos[0]=(float)i*10;
        t.gate[i].forward[0]=1;
        t.gate[i].half_width=3; t.gate[i].half_height=2;
        t.gate[i].recovery[0]=t.gate[i].pos[0]-1;
    }
    return t;
}

static bool move(hta_race *r, int who, float x, float y)
{
    float p[3]={x,y,0}; return hta_race_advance(r,(uint8_t)who,p);
}

int main(void)
{
    package_parser();
    hta_arcade_tuning t=hta_arcade_default_tuning();
    assert(hta_arcade_tuning_valid(&t));
    t.max_speed=NAN; assert(!hta_arcade_tuning_valid(&t));
    t=hta_arcade_default_tuning();
    hta_arcade_racer a={0},b={0};
    float home[3]={0,0,0}; hta_arcade_reset(&a,home,0); a.grounded=true;
    for (int i=0;i<180;i++) hta_arcade_step(&a,&t,(hta_arcade_input){.throttle=1},NULL,1.0f/60);
    assert(a.pos[0]>25 && a.vel[0]>25 && a.vel[0]<=t.max_speed+0.01f);
    float speed=a.vel[0];
    for (int i=0;i<60;i++) hta_arcade_step(&a,&t,(hta_arcade_input){.throttle=1,.brake=true},NULL,1.0f/60);
    assert(a.vel[0]<speed*0.5f);
    for (int i=0;i<80;i++) hta_arcade_step(&a,&t,(hta_arcade_input){.throttle=-1},NULL,1.0f/60);
    assert(a.vel[0]<-2 && a.vel[0]>=-t.reverse_speed-0.01f);
    hta_arcade_reset(&a,home,0); a.grounded=true;
    for (int i=0;i<170;i++) hta_arcade_step(&a,&t,(hta_arcade_input){.throttle=1,.drift=true},NULL,1.0f/60);
    hta_arcade_step(&a,&t,(hta_arcade_input){.throttle=1},NULL,1.0f/60);
    assert(a.boost_tier==0 && a.boost_time==0); /* straight button hold earns nothing */
    hta_arcade_reset(&a,home,0); a.grounded=true;
    for (int i=0;i<140;i++) hta_arcade_step(&a,&t,(hta_arcade_input){.throttle=1},NULL,1.0f/60);
    for (int i=0;i<85;i++) hta_arcade_step(&a,&t,(hta_arcade_input){.throttle=1,.steer=0.55f,.drift=true},NULL,1.0f/60);
    unsigned charged=hta_arcade_charge_tier(&a);
    assert(charged>0);
    hta_arcade_step(&a,&t,(hta_arcade_input){.throttle=1},NULL,1.0f/60);
    assert(a.boost_tier==charged && a.boost_time>0);
    hta_arcade_boost_pad(&a,&t);
    assert(a.boost_time>=t.boost_seconds-0.0001f);
    hta_arcade_reset(&b,home,0); b.pos[0]=a.pos[0]+0.2f; b.pos[1]=a.pos[1];
    hta_arcade_contact(&a,&b,&t);
    assert(hypotf(a.pos[0]-b.pos[0],a.pos[1]-b.pos[1])>=2*t.radius-0.001f);
    /* Two cars crossing within one 60 Hz host frame must still touch. */
    float old_a[3]={-0.8f,0,0}, old_b[3]={0.8f,0,0};
    hta_arcade_reset(&a,home,0); hta_arcade_reset(&b,home,0);
    a.pos[0]=0.8f; b.pos[0]=-0.8f;
    a.vel[0]=48; b.vel[0]=-48;
    hta_arcade_contact_swept(&a,&b,old_a,old_b,&t);
    assert(a.collision_speed>0 && b.collision_speed>0);
    assert(hypotf(a.pos[0]-b.pos[0],a.pos[1]-b.pos[1])>=2*t.radius-0.001f);

    hta_race_track track=route();
    assert(hta_race_track_valid(&track));
    track.gate[1].forward[0]=NAN;
    assert(!hta_race_track_valid(&track));
    track=route();
    hta_race race;
    assert(hta_race_init(&race,&track,2));
    assert(hta_race_position(&race,0)==1);
    hta_race_start(&race);
    assert(!move(&race,0,11,0)); /* countdown cannot advance */
    for (int i=0;i<181;i++) hta_race_step(&race,1.0f/60);
    assert(race.phase==HTA_RACE_GO);
    assert(!hta_race_join(&race,1)); /* late entrants wait for the next round */
    assert(!move(&race,0,21,0)); /* skipped expected checkpoint */
    assert(race.racer[0].next_gate==1);
    move(&race,0,9,0); assert(move(&race,0,11,0));
    move(&race,0,19,0); assert(move(&race,0,21,0));
    move(&race,0,-1,0); assert(move(&race,0,1,0));
    assert(race.racer[0].lap==2 && !race.racer[0].finished);
    float recover[3],yaw;
    assert(hta_race_recovery(&race,0,recover,&yaw));
    assert(fabsf(recover[0]+1)<0.001f);
    move(&race,0,9,0); assert(move(&race,0,11,0));
    move(&race,0,19,0); assert(move(&race,0,21,0));
    move(&race,0,-1,0); assert(move(&race,0,1,0));
    assert(race.racer[0].finished && race.racer[0].finish_order==1);
    assert(hta_race_position(&race,0)==1 && hta_race_position(&race,1)==2);
    hta_race_leave(&race,0);
    assert(race.finish_order_next==1 && race.finished_count==0);
    move(&race,1,9,0); assert(move(&race,1,11,0));
    move(&race,1,19,0); assert(move(&race,1,21,0));
    move(&race,1,-1,0); assert(move(&race,1,1,0));
    move(&race,1,9,0); assert(move(&race,1,11,0));
    move(&race,1,19,0); assert(move(&race,1,21,0));
    move(&race,1,-1,0); assert(move(&race,1,1,0));
    assert(race.racer[1].finished && race.racer[1].finish_order==2);
    assert(hta_race_init(&race,&track,2));
    assert(race.phase==HTA_RACE_READY && race.racer[0].lap==1);
    puts("racing core: motion, drift, boost, contact, rules, invalid shortcut, finish, reset OK");
    return 0;
}
