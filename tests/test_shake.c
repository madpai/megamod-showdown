/* The camera shaken by the Trial's own damage effects: a tank shell's
 * blast kicks and shakes whoever is near it and nobody far away, a
 * cannon's firing shock wave rattles its own gunner, and it all dies away.
 * Also the tracer rhythm: the rifle's trigger draws one round in four. */
#include "engine/shake.h"
#include "asset/cache.h"
#include "asset/weapon.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks, failures;
#define CHECK(c,msg) do { checks++; if(!(c)){failures++;printf("FAIL: %s\n",msg);} else printf("ok:   %s\n",msg);}while(0)
static unsigned char *slurp(const char *path,size_t *n)
{
    FILE*f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    unsigned char*d=malloc((size_t)size);if(fread(d,1,(size_t)size,f)!=(size_t)size){free(d);fclose(f);return NULL;}
    fclose(f);*n=(size_t)size;return d;
}
static uint32_t find(const hta_cache *c,uint32_t cls,const char *path)
{
    for(uint32_t i=0;i<c->tag_count;i++){hta_tag_entry t;char p[256];
        if(hta_cache_tag(c,i,&t)&&t.primary_class==cls&&hta_cache_tag_path(c,&t,p,sizeof(p))&&!strcmp(p,path))return t.tag_id;}
    return 0;
}
int main(int argc,char**argv)
{
    /* Without a map: the mechanics. */
    hta_shake s;hta_shake_init(&s);
    hta_camera cam;hta_camera_init(&cam);
    hta_damage_shake d={{1,3},0.2f,0.175f,0.1f,1.0f,0.1f,0.02f};
    float at[3]={2,0,0};
    hta_shake_add(&s,&d,&cam,at);
    CHECK(hta_shake_amount(&s)>0.1f,"a blast two units away moves the camera");
    hta_camera c2=cam;hta_shake_update(&s,1.f/60);hta_shake_apply(&s,&c2);
    CHECK(fabsf(c2.pitch-cam.pitch)>0.01f || fabsf(c2.pos[0]-cam.pos[0])>0.005f,"the drawn view is thrown");
    CHECK(cam.pitch==0 && cam.pos[0]==0,"the camera itself is not moved");
    for(int i=0;i<90;i++)hta_shake_update(&s,1.f/60);
    CHECK(hta_shake_amount(&s)==0,"and a second and a half later it is still");
    float far[3]={10,0,0};hta_shake_add(&s,&d,&cam,far);
    CHECK(hta_shake_amount(&s)==0,"a blast beyond its radius does nothing");
    float edge[3]={2.9f,0,0};hta_shake_add(&s,&d,&cam,edge);float weak=hta_shake_amount(&s);
    hta_shake_init(&s);float near[3]={0.5f,0,0};hta_shake_add(&s,&d,&cam,near);
    CHECK(weak>0 && weak<hta_shake_amount(&s)*0.3f,"and falls off across its radius");
    if(argc<2){printf("%d checks, %d failures\n",checks,failures);return failures?1:0;}

    size_t n;unsigned char *m=slurp(argv[1],&n);hta_cache c;char err[HTA_ERRLEN];
    if(!m||!hta_cache_open(&c,m,n,err,sizeof(err))){printf("FAIL: map\n");return 1;}
    hta_damage_shake sh[4];
    uint32_t shell=find(&c,HTA_FOURCC('e','f','f','e'),"vehicles\\scorpion\\shell explosion");
    uint32_t k=hta_effect_shakes(&c,shell,sh,4);
    CHECK(k==2,"a tank shell's explosion carries a blast and a shock wave");
    CHECK(k>=1 && fabsf(sh[0].impulse_rot-0.1745f)<0.01f && fabsf(sh[0].radius[1]-3.25f)<0.01f,
          "the blast kicks the view ten degrees within 3.25 wu");
    CHECK(k>=2 && fabsf(sh[1].shake_time-1.5f)<0.01f && fabsf(sh[1].radius[1]-8.0f)<0.01f,
          "the shock wave shakes it a second and a half out to 8 wu");
    uint32_t cannon=find(&c,HTA_FOURCC('w','e','a','p'),"vehicles\\scorpion\\scorpion cannon");
    hta_weapon_def wd;
    CHECK(hta_weapon_load_trigger(&c,cannon,0,&wd) && wd.firing_damage_id,"the cannon's trigger has a firing damage effect");
    CHECK(hta_damage_shake_read(&c,wd.firing_damage_id,sh) && sh[0].shake_time>0.4f,"firing it shakes the gunner");
    uint32_t ar=find(&c,HTA_FOURCC('w','e','a','p'),"weapons\\assault rifle\\assault rifle");
    CHECK(hta_weapon_load_trigger(&c,ar,0,&wd) && wd.between_contrails==3,"the rifle draws a tracer on one round in four");
    uint32_t pistol=find(&c,HTA_FOURCC('w','e','a','p'),"weapons\\pistol\\pistol");
    CHECK(hta_weapon_load_trigger(&c,pistol,0,&wd) && wd.between_contrails==0,"the pistol on every round");
    free(m);
    printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
