#include "asset/external_map.h"
#include "engine/player.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void u32(unsigned char *p,uint32_t x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
static void f32(unsigned char *p,float v){uint32_t x;memcpy(&x,&v,4);u32(p,x);}
int main(void)
{
    unsigned char b[64+120+12+16+12+16+16]={0};size_t n=sizeof(b);
    memcpy(b,"OALM",4);u32(b+4,1);u32(b+12,3);u32(b+16,3);u32(b+20,1);u32(b+24,1);u32(b+28,1);
    f32(b+48,1);f32(b+52,1);
    size_t at=64;
    float xyz[3][3]={{0,0,0},{1,0,0},{0,1,0}};
    for(int i=0;i<3;i++){for(int k=0;k<3;k++)f32(b+at+i*40+k*4,xyz[i][k]);f32(b+at+i*40+20,1);}
    at+=120;u32(b+at,0);u32(b+at+4,1);u32(b+at+8,2);at+=12;
    u32(b+at,0);u32(b+at+4,3);u32(b+at+8,0);at+=16;
    u32(b+at,2);u32(b+at+4,2);u32(b+at+8,16);at+=12;
    for(int i=0;i<16;i++)b[at+i]=255;at+=16;
    f32(b+at,.2f);f32(b+at+4,.2f);
    assert(at+16==n);
    char path[]="/tmp/oalmap-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    FILE *f=fdopen(fd,"wb");assert(f);assert(fwrite(b,1,n,f)==n);fclose(f);
    char err[256];hta_external_map m;
    assert(hta_external_map_load(path,&m,err,sizeof(err)));
    assert(m.mesh.vertex_count==3&&m.mesh.index_count==3&&m.spawn_count==1);
    hta_collision col;assert(hta_collision_build(&col,&m.mesh));float z;
    assert(hta_collision_ground(&col,.2f,.2f,.1f,&z)&&z==0);
    assert(m.spawns[0].team_index==HTA_EXTERNAL_TEAM_ANY);
    hta_collision_free(&col);hta_external_map_free(&m);
    /* The manifest's own "team" decides, not the Source class name. */
    {
        static const char man[]="{\"entities\":[{\"classname\":\"info_player_terrorist\",\"team\":0}],"
            "\"spawn_points\":[{\"classname\":\"anything\",\"position\":[0,0,0],\"team\":1}],"
            "\"flag_points\":[{\"team\":1,\"position\":[1.5, -2.0, 0.25]}]}";
        size_t ml=sizeof(man)-1;unsigned char *w=malloc(n+ml);assert(w);
        memcpy(w,b,64);memcpy(w+64,man,ml);memcpy(w+64+ml,b+64,n-64);u32(w+8,(uint32_t)ml);
        assert(hta_external_map_load_memory(w,n+ml,&m,err,sizeof(err)));
        assert(m.spawn_count==1&&m.spawns[0].team_index==1&&m.key);
        /* The map's own flag stand, blue's only. */
        assert(!m.has_flag[0]&&m.has_flag[1]&&m.flag[1][0]==1.5f&&m.flag[1][1]==-2.0f&&m.flag[1][2]==0.25f);
        assert(m.mesh.submeshes[0].draw_mode==HTA_DRAW_OPAQUE);
        assert(m.breakable_count==0&&m.weather==-1&&m.submesh_breakable[0]==0);
        uint32_t key0=m.key;hta_external_map_free(&m);
        /* Group flag bit 1: drawn alpha-blended. */
        size_t g=64+ml+3*40+3*4;
        w[g+12]|=HTA_EXTERNAL_GROUP_ALPHA;
        assert(hta_external_map_load_memory(w,n+ml,&m,err,sizeof(err)));
        assert(m.mesh.submeshes[0].draw_mode==HTA_DRAW_ALPHA&&m.solid_index_count==3&&m.key==key0);
        w[g+12]&=(unsigned char)~HTA_EXTERNAL_GROUP_ALPHA;
        uint32_t key=m.key;hta_external_map_free(&m);
        w[64+ml-40]^=1;assert(hta_external_map_load_memory(w,n+ml,&m,err,sizeof(err))&&m.key!=key);
        hta_external_map_free(&m);free(w);
    }
    /* Breakables and weather from the manifest, the group's breakable bits. */
    {
        static const char man[]="{\"breakables\":[{\"blast_damage\":150.0,\"blast_radius\":3.0,\"bounds\":{\"max\":[1.5,2,0.75],"
            "\"min\":[0.5,-1e-1,0]},\"classname\":\"prop_physics\",\"explosive\":true,\"health\":35.0,\"index\":0,"
            "\"material\":\"metal\",\"model\":\"models/{odd}/barrel.mdl\"}],\"weather\":{\"intensity\":0.7,\"kind\":\"storm\","
            "\"source\":\"func_precipitation\"}}";
        size_t ml=sizeof(man)-1;unsigned char *w=malloc(n+ml);assert(w);
        memcpy(w,b,64);memcpy(w+64,man,ml);memcpy(w+64+ml,b+64,n-64);u32(w+8,(uint32_t)ml);
        size_t g=64+ml+3*40+3*4;
        u32(w+g+12,HTA_EXTERNAL_GROUP_BREAKABLE|(1u<<8));
        assert(hta_external_map_load_memory(w,n+ml,&m,err,sizeof(err)));
        assert(m.breakable_count==1&&m.submesh_breakable[0]==1);
        const hta_external_breakable *br=&m.breakables[0];
        assert(br->material==1&&br->explosive&&br->health==35.0f&&br->blast_radius==3.0f);
        assert(br->min[0]==0.5f&&fabsf(br->min[1]+0.1f)<1e-6f&&br->max[1]==2.0f&&br->max[2]==0.75f);
        assert(m.weather==2&&fabsf(m.weather_intensity-0.7f)<1e-6f);
        hta_external_map_free(&m);
        /* A group naming a breakable the manifest lacks is scenery. */
        u32(w+g+12,HTA_EXTERNAL_GROUP_BREAKABLE|(5u<<8));
        assert(hta_external_map_load_memory(w,n+ml,&m,err,sizeof(err))&&m.submesh_breakable[0]==0);
        hta_external_map_free(&m);free(w);
    }
    f=fopen(path,"r+b");assert(f);fseek(f,4,SEEK_SET);unsigned char bad[4]={2,0,0,0};assert(fwrite(bad,1,4,f)==4);fclose(f);
    assert(!hta_external_map_load(path,&m,err,sizeof(err)));
    f=fopen(path,"r+b");assert(f);fseek(f,4,SEEK_SET);unsigned char good_version[4]={1,0,0,0};assert(fwrite(good_version,1,4,f)==4);fseek(f,64+120,SEEK_SET);unsigned char bad_index[4]={99,0,0,0};assert(fwrite(bad_index,1,4,f)==4);fclose(f);
    assert(!hta_external_map_load(path,&m,err,sizeof(err)));
    unlink(path);puts("external map loader OK");return 0;
}
