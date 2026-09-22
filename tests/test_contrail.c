/* Contrails from the Trial's own `cont` tags: tracers for hitscan rounds,
 * ribbons behind rounds that fly, faded by the tags' point states. */
#include "engine/contrail.h"
#include "asset/cache.h"
#include "asset/bitmap.h"
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
static float area(const hta_contrails *c,uint32_t type)
{
    float a=0;uint32_t per=HTA_CONT_TRAILS*HTA_CONT_POINTS;
    for(uint32_t q=type*per;q<(type+1)*per;q++){
        const hta_vertex *v=&c->mesh.vertices[q*4];
        float e1[3],e2[3];for(int k=0;k<3;k++){e1[k]=v[1].pos[k]-v[0].pos[k];e2[k]=v[3].pos[k]-v[0].pos[k];}
        float x=e1[1]*e2[2]-e1[2]*e2[1],y=e1[2]*e2[0]-e1[0]*e2[2],z=e1[0]*e2[1]-e1[1]*e2[0];
        a+=sqrtf(x*x+y*y+z*z);
    }
    return a;
}
int main(int argc,char**argv)
{
    if(argc<2){printf("skip: pass bloodgulch.map\n0 checks, 0 failures\n");return 0;}
    size_t n;unsigned char *d=slurp(argv[1],&n);hta_cache c;char err[HTA_ERRLEN];
    if(!d||!hta_cache_open(&c,d,n,err,sizeof(err))){printf("FAIL: map\n");return 1;}
    /* The art lives in bitmaps.map beside the level. */
    char bp[1024];snprintf(bp,sizeof(bp),"%s",argv[1]);char *sl=strrchr(bp,'/');
    if(sl)snprintf(sl+1,sizeof(bp)-(size_t)(sl+1-bp),"bitmaps.map");
    size_t bn=0;unsigned char *bd=slurp(bp,&bn);hta_resource_map bmr={0};
    const hta_resource_map *bm=bd&&hta_resource_open(&bmr,bd,bn,err,sizeof(err))?&bmr:NULL;
    CHECK(bm!=NULL,"bitmaps.map opens");
    static hta_contrails cc;hta_contrails_init(&cc);
    uint32_t ar=hta_contrails_for_projectile(&cc,&c,bm,find(&c,HTA_FOURCC('p','r','o','j'),"weapons\\assault rifle\\bullet"));
    uint32_t shell=hta_contrails_for_projectile(&cc,&c,bm,find(&c,HTA_FOURCC('p','r','o','j'),"vehicles\\scorpion\\tank shell"));
    uint32_t bolt=hta_contrails_for_projectile(&cc,&c,bm,find(&c,HTA_FOURCC('p','r','o','j'),"vehicles\\ghost\\ghost bolt"));
    uint32_t pistol=hta_contrails_for_projectile(&cc,&c,bm,find(&c,HTA_FOURCC('p','r','o','j'),"weapons\\pistol\\bullet"));
    CHECK(ar!=HTA_CONT_NONE && shell!=HTA_CONT_NONE && bolt!=HTA_CONT_NONE,"rifle, shell and bolt contrails from their projectiles");
    CHECK(pistol==HTA_CONT_NONE,"the pistol's bullet carries none");
    CHECK(ar==hta_contrails_for_projectile(&cc,&c,bm,find(&c,HTA_FOURCC('p','r','o','j'),"vehicles\\warthog\\bullet")),
          "the chaingun shares the rifle's tracer");
    if(ar==HTA_CONT_NONE||shell==HTA_CONT_NONE||bolt==HTA_CONT_NONE)return 1;
    CHECK(cc.type[ar].additive && cc.type[bolt].additive,"tracer and bolt glow add");
    CHECK(fabsf(cc.type[shell].state[1].width-0.1f)<1e-4f && cc.type[shell].life>0.9f,
          "the shell's smoke widens and lingers, as tagged");
    CHECK(cc.type[ar].life>=0.06f-1e-5f,"a rifle tracer lives long enough to draw");
    CHECK(hta_contrails_build(&cc,err,sizeof(err)),"contrails build");
    printf("  %s\n",err);
    hta_camera cam;hta_camera_init(&cam);cam.pos[0]=0;cam.pos[1]=-5;cam.pos[2]=1;
    /* A shell flying down +x for half a second. */
    float p[3]={0,0,1};
    for(int i=0;i<30;i++){p[0]=i*100.0f/60.0f;hta_contrails_feed(&cc,shell,77,p,i/60.0f);hta_contrails_update(&cc,&cam,1.f/60);}
    CHECK(area(&cc,shell)>0.5f,"a flying shell leaves a visible ribbon");
    CHECK(hta_contrails_live(&cc)==1,"one round, one trail");
    for(int i=0;i<200;i++)hta_contrails_update(&cc,&cam,1.f/60);
    CHECK(hta_contrails_live(&cc)==0 && area(&cc,shell)<1e-6f,"stopped, it fades away and frees its trail");
    /* A slot reused by a new round starts afresh rather than streaking. */
    p[0]=0;hta_contrails_feed(&cc,shell,5,p,1.0f);hta_contrails_update(&cc,&cam,1.f/60);
    p[0]=50;hta_contrails_feed(&cc,shell,5,p,0.0f);hta_contrails_update(&cc,&cam,1.f/60);
    float maxx=-1e9f,minx=1e9f;
    for(uint32_t k=0;k<HTA_CONT_TRAILS;k++){const hta_contrail *t=&cc.trail[shell][k];if(!t->used)continue;
        for(uint32_t q=0;q<t->count;q++){if(t->pt[q].pos[0]>maxx)maxx=t->pt[q].pos[0];if(t->pt[q].pos[0]<minx)minx=t->pt[q].pos[0];}}
    CHECK(minx>40,"a reused projectile slot does not draw a streak from the old round");
    for(int i=0;i<200;i++)hta_contrails_update(&cc,&cam,1.f/60);
    /* A hitscan tracer runs from the muzzle to the impact and goes. */
    float a[3]={0,0,1},b[3]={30,0,1};
    hta_contrails_tracer(&cc,ar,a,b,324.0f);
    hta_contrails_update(&cc,&cam,1.f/60);
    CHECK(area(&cc,ar)>0.01f,"a rifle tracer draws at once");
    float head=cc.trail[ar][0].head[0];
    CHECK(head>4 && head<7,"and its head moves at the round's speed");
    for(int i=0;i<30;i++)hta_contrails_update(&cc,&cam,1.f/60);
    CHECK(hta_contrails_live(&cc)==0,"and is gone a moment after it lands");
    /* Colour per vertex, alpha fading with age. */
    hta_contrails_tracer(&cc,ar,a,b,324.0f);
    hta_contrails_update(&cc,&cam,1.f/60);hta_contrails_update(&cc,&cam,1.f/60);
    const hta_vertex *v=&cc.mesh.vertices[(ar*HTA_CONT_TRAILS*HTA_CONT_POINTS)*4];
    CHECK(v[0].normal[0]>0.9f && v[0].normal[2]<0.5f,"the rifle tracer's head is yellow");
    hta_contrails_free(&cc);free(d);free(bd);
    printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
