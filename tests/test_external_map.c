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
/* ---- the world key: what it covers and what it leaves out ---- */

/* A v3 package: base package `b` (n bytes, ml 0) with manifest `man`. */
static unsigned char *with_manifest(const unsigned char *b,size_t n,const char *man,size_t *out_n)
{
    size_t group=64+120+12, ml=strlen(man), n3=n+4+ml;
    unsigned char *w=malloc(n3);assert(w);
    memcpy(w,b,64);memcpy(w+64,man,ml);
    memcpy(w+64+ml,b+64,group+16-64);u32(w+ml+group+16,UINT32_MAX);
    memcpy(w+ml+group+20,b+group+16,n-group-16);
    u32(w+4,3);u32(w+8,(uint32_t)ml);
    *out_n=n3;return w;
}

static uint64_t digest_of(const unsigned char *w,size_t n)
{
    hta_external_map m;char err[256];
    if(!hta_external_map_load_memory(w,n,&m,err,sizeof(err))){fprintf(stderr,"%s\n",err);assert(0);}
    uint64_t d=m.digest;
    assert(m.key==hta_world_key_fold(d)&&d);
    hta_external_map_free(&m);return d;
}

static void world_key_tests(const unsigned char *b,size_t n)
{
    static const char base[]="{\"display_name\":\"Lab\",\"importer_version\":\"original_world-0.1.0\","
        "\"source_provenance\":\"ours\",\"spawn_points\":[{\"position\":[0.2,0.2,0],\"team\":null,\"yaw_degrees\":0}],"
        "\"warnings\":[\"a\",\"b\"],\"world_entities\":{\"entities\":["
        "{\"definition\":\"t:mover/slide\",\"id\":\"t:entity/d\",\"kind\":\"mover\",\"links\":[],\"position\":[0.5,0.5,0.5]},"
        "{\"id\":\"t:entity/r\",\"kind\":\"relay\",\"links\":[{\"event\":\"fired\",\"input\":\"open\",\"target\":\"t:entity/d\"}]},"
        "{\"bounds\":{\"max\":[3,3,1],\"min\":[2,2,0]},\"id\":\"t:entity/t\",\"kind\":\"trigger\",\"links\":[{\"event\":\"entered\",\"input\":\"teleport\",\"target\":\"t:entity/p\"}]},"
        "{\"id\":\"t:entity/p\",\"kind\":\"teleport\",\"links\":[],\"position\":[-1,-1,0],\"yaw_degrees\":0}"
        "],\"mover_definitions\":[{\"id\":\"t:mover/slide\",\"move\":[0,1,0],\"size\":[1,1,1],\"speed\":1}],\"schema\":2}}";
    size_t n0;unsigned char *w=with_manifest(b,n,base,&n0);
    uint64_t d0=digest_of(w,n0);
    /* The same bytes: the same key; loading twice changes nothing. */
    assert(digest_of(w,n0)==d0);
    const size_t ml=strlen(base), vtx=64+ml, idx=vtx+120, grp=idx+12, tex=grp+20, spawn=tex+12+16;
    assert(spawn+16==n0);
    /* Geometry: one vertex coordinate. */
    f32(w+vtx+40+4,0.5f);assert(digest_of(w,n0)!=d0);f32(w+vtx+40+4,0);
    /* Group flags (collision, alpha, owner bits share the field; the only
     * group cannot turn non-solid here, OAL's tests change a solid box). */
    u32(w+grp+12,HTA_EXTERNAL_GROUP_ALPHA);assert(digest_of(w,n0)!=d0);u32(w+grp+12,0);
    /* Index order. */
    u32(w+idx,1);u32(w+idx+4,0);assert(digest_of(w,n0)!=d0);u32(w+idx,0);u32(w+idx+4,1);
    /* A spawn record. */
    f32(w+spawn,.25f);assert(digest_of(w,n0)!=d0);f32(w+spawn,.2f);
    /* World bounds in the header (nav is built inside them). */
    f32(w+48,2);assert(digest_of(w,n0)!=d0);f32(w+48,1);
    /* Texture pixels: appearance only, not in the key. */
    w[tex+12]=7;assert(digest_of(w,n0)==d0);w[tex+12]=255;
    assert(digest_of(w,n0)==d0);
    free(w);
    /* Manifest edits, each against the base. Played members change the
     * key; provenance, reports and names do not. */
    static const struct { const char *from, *to; bool same; } edits[]={
        {"\"source_provenance\":\"ours\"","\"source_provenance\":\"somewhere else entirely\"",true},
        {"\"importer_version\":\"original_world-0.1.0\"","\"importer_version\":\"original_world-9.9.9\"",true},
        {"\"warnings\":[\"a\",\"b\"]","\"warnings\":[\"b\",\"a\"]",true},
        {"\"display_name\":\"Lab\"","\"display_name\":\"Renamed Lab\",\"source_path\":\"/home/x/y.bsp\"",true},
        {"\"spawn_points\":[{\"position\":[0.2,0.2,0],\"team\":null","\"spawn_points\":[{\"position\":[0.2,0.2,0],\"team\":1",false},
        {"\"position\":[0.5,0.5,0.5]","\"position\":[0.5,0.5,0.75]",false},              /* placement */
        {"\"move\":[0,1,0]","\"move\":[0,1.5,0]",false},                                  /* definition */
        {"\"size\":[1,1,1]","\"size\":[1,1,1.25]",false},
        {"\"speed\":1}","\"speed\":2}",false},
        {"\"input\":\"open\"","\"input\":\"toggle\"",false},                               /* event link */
        {"\"max\":[3,3,1]","\"max\":[3,3,1.5]",false},                                    /* trigger volume */
        {"\"position\":[-1,-1,0]","\"position\":[-1,-1.5,0]",false},                      /* teleport destination */
        {"\"yaw_degrees\":0}]","\"yaw_degrees\":90}]",false},
    };
    for(size_t i=0;i<sizeof(edits)/sizeof(edits[0]);i++){
        static char man[4096];
        const char *at=strstr(base,edits[i].from);assert(at);
        snprintf(man,sizeof(man),"%.*s%s%s",(int)(at-base),base,edits[i].to,at+strlen(edits[i].from));
        size_t n1;unsigned char *v=with_manifest(b,n,man,&n1);
        uint64_t d1=digest_of(v,n1);
        if((d1==d0)!=edits[i].same){fprintf(stderr,"world key: edit %zu (%s) %s\n",i,edits[i].to,edits[i].same?"changed the key":"kept the key");assert(0);}
        free(v);
    }
    puts("  world key: covers geometry, groups, spawns, bounds, entities, definitions; not provenance or textures");
}

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
    {
        /* V2 extends each group with a checked lightmap texture index. */
        size_t group=64+120+12;
        unsigned char v2[sizeof(b)+4];
        memcpy(v2,b,group+16); u32(v2+4,2); u32(v2+group+16,0);
        memcpy(v2+group+20,b+group+16,n-group-16);
        assert(hta_external_map_load_memory(v2,sizeof(v2),&m,err,sizeof(err)));
        assert(m.mesh.submeshes[0].lightmap_tex==0 && m.solid_index_count==3);
        hta_external_map_free(&m);
        u32(v2+group+16,1);
        assert(!hta_external_map_load_memory(v2,sizeof(v2),&m,err,sizeof(err)));
        u32(v2+group+16,UINT32_MAX);
        assert(hta_external_map_load_memory(v2,sizeof(v2),&m,err,sizeof(err)));
        assert(m.mesh.submeshes[0].lightmap_tex==UINT32_MAX);
        hta_external_map_free(&m);
    }
    /* V3: v2's layout plus world entities, parsed whole or refused. V1/v2
     * never read the section (an older package keeps its old meaning). */
    {
        static const char man[]="{\"world_entities\":{\"entities\":["
            "{\"id\":\"t:entity/b\",\"kind\":\"interactable\",\"links\":[{\"event\":\"used\",\"input\":\"open\",\"target\":\"t:entity/d\"}],\"position\":[0,0,0.5],\"reach\":1},"
            "{\"bounds\":{\"max\":[1,1,1],\"min\":[0,0,0]},\"id\":\"t:entity/d\",\"kind\":\"mover\",\"links\":[],\"move\":[0,0,1],\"speed\":2}"
            "],\"schema\":1}}";
        size_t group=64+120+12, ml=sizeof(man)-1, n3=n+4+ml;
        unsigned char *w=malloc(n3);assert(w);
        memcpy(w,b,64);memcpy(w+64,man,ml);
        memcpy(w+64+ml,b+64,group+16-64);u32(w+ml+group+16,UINT32_MAX);
        memcpy(w+ml+group+20,b+group+16,n-group-16);
        u32(w+4,3);u32(w+8,(uint32_t)ml);
        assert(hta_external_map_load_memory(w,n3,&m,err,sizeof(err))||(fprintf(stderr,"%s\n",err),0));
        assert(m.version==3&&m.world_defs.count==2&&m.world_defs.link_count==1&&m.world_defs.link[0].target==1);
        assert(m.world_defs.entity[1].kind==HTA_WDEF_MOVER&&m.submesh_entity[0]==0);
        hta_external_map_free(&m);
        /* An entity group must be a mover's, and out of static collision. */
        u32(w+ml+group+12,HTA_EXTERNAL_GROUP_ENTITY|(2u<<8));
        assert(!hta_external_map_load_memory(w,n3,&m,err,sizeof(err))&&strstr(err,"invalid world entity group"));
        u32(w+ml+group+12,0);
        /* A broken link refuses the whole package, and says why. */
        char *t=strstr((char*)w+64,"t:entity/d\"}");assert(t);t[9]='x';
        assert(!hta_external_map_load_memory(w,n3,&m,err,sizeof(err)));
        assert(strstr(err,"world entities: t:entity/b references missing target t:entity/x"));
        t[9]='d';
        /* The same manifest in a v2 package: the section is not read. */
        u32(w+4,2);
        assert(hta_external_map_load_memory(w,n3,&m,err,sizeof(err))&&m.world_defs.count==0);
        hta_external_map_free(&m);
        /* A version this runtime does not know is refused. */
        u32(w+4,4);
        assert(!hta_external_map_load_memory(w,n3,&m,err,sizeof(err)));
        free(w);
    }
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
        /* Group records are in the world key whole (flags included). */
        assert(m.mesh.submeshes[0].draw_mode==HTA_DRAW_ALPHA&&m.solid_index_count==3&&m.key!=key0);
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
    world_key_tests(b,n);
    f=fopen(path,"r+b");assert(f);fseek(f,4,SEEK_SET);unsigned char bad[4]={2,0,0,0};assert(fwrite(bad,1,4,f)==4);fclose(f);
    assert(!hta_external_map_load(path,&m,err,sizeof(err)));
    f=fopen(path,"r+b");assert(f);fseek(f,4,SEEK_SET);unsigned char good_version[4]={1,0,0,0};assert(fwrite(good_version,1,4,f)==4);fseek(f,64+120,SEEK_SET);unsigned char bad_index[4]={99,0,0,0};assert(fwrite(bad_index,1,4,f)==4);fclose(f);
    assert(!hta_external_map_load(path,&m,err,sizeof(err)));
    unlink(path);puts("external map loader OK");return 0;
}
