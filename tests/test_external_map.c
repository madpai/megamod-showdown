#include "asset/external_map.h"
#include "engine/player.h"
#include <assert.h>
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
    /* The manifest names each start's Source class: T plays red, CT blue. */
    {
        static const char man[]="{\"entities\":[{\"classname\":\"info_player_terrorist\"}],"
            "\"spawn_points\":[{\"classname\":\"info_player_counterterrorist\",\"position\":[0,0,0]}]}";
        size_t ml=sizeof(man)-1;unsigned char *w=malloc(n+ml);assert(w);
        memcpy(w,b,64);memcpy(w+64,man,ml);memcpy(w+64+ml,b+64,n-64);u32(w+8,(uint32_t)ml);
        assert(hta_external_map_load_memory(w,n+ml,&m,err,sizeof(err)));
        assert(m.spawn_count==1&&m.spawns[0].team_index==1&&m.key);
        uint32_t key=m.key;hta_external_map_free(&m);
        w[64+ml-40]^=1;assert(hta_external_map_load_memory(w,n+ml,&m,err,sizeof(err))&&m.key!=key);
        hta_external_map_free(&m);free(w);
    }
    f=fopen(path,"r+b");assert(f);fseek(f,4,SEEK_SET);unsigned char bad[4]={2,0,0,0};assert(fwrite(bad,1,4,f)==4);fclose(f);
    assert(!hta_external_map_load(path,&m,err,sizeof(err)));
    f=fopen(path,"r+b");assert(f);fseek(f,4,SEEK_SET);unsigned char good_version[4]={1,0,0,0};assert(fwrite(good_version,1,4,f)==4);fseek(f,64+120,SEEK_SET);unsigned char bad_index[4]={99,0,0,0};assert(fwrite(bad_index,1,4,f)==4);fclose(f);
    assert(!hta_external_map_load(path,&m,err,sizeof(err)));
    unlink(path);puts("external map loader OK");return 0;
}
