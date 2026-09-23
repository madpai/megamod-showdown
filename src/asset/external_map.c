#include "external_map.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_MAX (256u*1024u*1024u)
static uint32_t u32(const unsigned char *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static float f32(const unsigned char *p) { uint32_t v=u32(p); float f; memcpy(&f,&v,4); return f; }
static bool take(size_t *at, size_t n, size_t size) { if (n>size-*at) return false; *at+=n; return true; }
static bool fail(char *err,size_t n,const char *s) { if(err && n) snprintf(err,n,"%s",s); return false; }
void hta_external_map_free(hta_external_map *m) { if(!m)return; hta_bsp_free(&m->mesh); free(m->spawns); memset(m,0,sizeof(*m)); }

/* The manifest is JSON written by Asset Lab; its spawn_points are in the
 * same order as the binary spawn records. Only their classnames matter. */
static uint16_t team_of(const char *cls, size_t n)
{
    static const char ct[]="info_player_counterterrorist", t[]="info_player_terrorist";
    if(n==sizeof(ct)-1 && !memcmp(cls,ct,n)) return 1;
    if(n==sizeof(t)-1 && !memcmp(cls,t,n)) return 0;
    return HTA_EXTERNAL_TEAM_ANY;
}
static void spawn_teams(const unsigned char *m, size_t ml, hta_spawn_point *sp, uint32_t sc)
{
    for(uint32_t i=0;i<sc;i++) sp[i].team_index=HTA_EXTERNAL_TEAM_ANY;
    static const char key[]="\"spawn_points\"", cls[]="\"classname\"";
    const unsigned char *end=m+ml, *at=NULL;
    for(const unsigned char *p=m;p+sizeof(key)-1<=end;p++)
        if(!memcmp(p,key,sizeof(key)-1)){at=p+sizeof(key)-1;break;}
    for(uint32_t i=0;at && i<sc;i++){
        const unsigned char *hit=NULL;
        for(const unsigned char *p=at;p+sizeof(cls)-1<=end;p++)
            if(!memcmp(p,cls,sizeof(cls)-1)){hit=p+sizeof(cls)-1;break;}
        if(!hit) return;
        while(hit<end && (*hit==' '||*hit==':')) hit++;
        if(hit>=end || *hit!='"') return;
        const unsigned char *q=++hit;
        while(q<end && *q!='"') q++;
        sp[i].team_index=team_of((const char *)hit,(size_t)(q-hit));
        at=q;
    }
}

bool hta_external_map_load(const char *path, hta_external_map *out, char *err, size_t errlen)
{
    if(!path||!out)return fail(err,errlen,"invalid arguments");
    memset(out,0,sizeof(*out));
    FILE *f=fopen(path,"rb");
    if(!f)return fail(err,errlen,"cannot open package");
    if(fseek(f,0,SEEK_END)||ftell(f)<0){fclose(f);return fail(err,errlen,"cannot size package");}
    long len=ftell(f);
    if(len<64||len>FILE_MAX){fclose(f);return fail(err,errlen,"package size out of range");}
    rewind(f);
    unsigned char *data=malloc((size_t)len);
    if(!data){fclose(f);return fail(err,errlen,"out of memory");}
    if(fread(data,1,(size_t)len,f)!=(size_t)len){fclose(f);free(data);return fail(err,errlen,"truncated package read");}
    fclose(f);
    bool ok=hta_external_map_load_memory(data,(size_t)len,out,err,errlen);
    free(data);
    return ok;
}

bool hta_external_map_load_memory(const uint8_t *data, size_t size, hta_external_map *out,
                                  char *err, size_t errlen)
{
    if(!data||!out)return fail(err,errlen,"invalid arguments");
    memset(out,0,sizeof(*out));
    if(size<64||size>FILE_MAX)return fail(err,errlen,"package size out of range");
    size_t at=64;
    bool ok=false;
    if(memcmp(data,"OALM",4)||u32(data+4)!=1){fail(err,errlen,"unsupported OALMAP package version");goto done;}
    uint32_t ml=u32(data+8),vc=u32(data+12),ic=u32(data+16),gc=u32(data+20),tc=u32(data+24),sc=u32(data+28);
    if(ml>4u*1024u*1024u||vc==0||vc>5000000u||ic==0||ic>15000000u||ic%3||gc==0||gc>100000u||tc==0||tc>10000u||sc>100000u){fail(err,errlen,"package counts out of range");goto done;}
    for(int k=0;k<3;k++){
        out->mesh.bounds_min[k]=f32(data+36+k*4);
        out->mesh.bounds_max[k]=f32(data+48+k*4);
        if(!isfinite(out->mesh.bounds_min[k])||!isfinite(out->mesh.bounds_max[k])||out->mesh.bounds_max[k]<out->mesh.bounds_min[k]){fail(err,errlen,"invalid world bounds");goto done;}
    }
    if(!take(&at,ml,size)||vc>(size-at)/40){fail(err,errlen,"truncated vertices");goto done;}
    out->mesh.vertices=calloc(vc,sizeof(hta_vertex));
    if(!out->mesh.vertices){fail(err,errlen,"out of memory");goto done;}
    out->mesh.vertex_count=vc;
    for(uint32_t i=0;i<vc;i++){
        const unsigned char *p=data+at+i*40;
        float *dst=(float*)&out->mesh.vertices[i];
        for(int j=0;j<10;j++){dst[j]=f32(p+j*4);if(!isfinite(dst[j])){fail(err,errlen,"non-finite vertex");goto done;}}
    }
    at+=(size_t)vc*40;
    if(ic>(size-at)/4){fail(err,errlen,"truncated indices");goto done;}
    out->mesh.indices=malloc((size_t)ic*4);
    if(!out->mesh.indices){fail(err,errlen,"out of memory");goto done;}
    out->mesh.index_count=ic;
    for(uint32_t i=0;i<ic;i++){
        uint32_t v=u32(data+at+i*4);
        if(v>=vc){fail(err,errlen,"invalid vertex index");goto done;}
        out->mesh.indices[i]=v;
    }
    at+=(size_t)ic*4;
    if(gc>(size-at)/16){fail(err,errlen,"truncated material groups");goto done;}
    out->mesh.submeshes=calloc(gc,sizeof(hta_submesh));
    if(!out->mesh.submeshes){fail(err,errlen,"out of memory");goto done;}
    out->mesh.submesh_count=gc;
    uint32_t end=0;
    for(uint32_t i=0;i<gc;i++){
        const unsigned char *p=data+at+i*16;
        uint32_t first=u32(p),count=u32(p+4),tex=u32(p+8);
        if(first!=end||count==0||count%3||count>ic-first||tex>=tc){fail(err,errlen,"invalid material group");goto done;}
        hta_submesh *s=&out->mesh.submeshes[i];hta_submesh_init(s);
        s->first_index=first;s->index_count=count;s->albedo_tex=tex;
        end=first+count;
    }
    if(end!=ic){fail(err,errlen,"material groups do not cover indices");goto done;}
    at+=(size_t)gc*16;
    out->mesh.textures=calloc(tc,sizeof(hta_bsp_texture));
    if(!out->mesh.textures){fail(err,errlen,"out of memory");goto done;}
    out->mesh.texture_count=tc;
    size_t texture_bytes=0;
    for(uint32_t i=0;i<tc;i++){
        if(!take(&at,12,size)){fail(err,errlen,"truncated texture header");goto done;}
        const unsigned char *p=data+at-12;
        uint32_t w=u32(p),h=u32(p+4),n=u32(p+8);
        if(w==0||h==0||w>2048||h>2048||(uint64_t)w*h*4!=n||n>size-at||texture_bytes+n>128u*1024u*1024u){fail(err,errlen,"invalid texture size");goto done;}
        hta_bsp_texture *t=&out->mesh.textures[i];t->width=w;t->height=h;t->rgba=malloc(n);
        if(!t->rgba){fail(err,errlen,"out of memory");goto done;}
        memcpy(t->rgba,data+at,n);at+=n;texture_bytes+=n;
    }
    if(sc>(size-at)/16){fail(err,errlen,"truncated spawn points");goto done;}
    if(sc){out->spawns=calloc(sc,sizeof(hta_spawn_point));if(!out->spawns){fail(err,errlen,"out of memory");goto done;}}
    out->spawn_count=sc;
    for(uint32_t i=0;i<sc;i++){
        for(int k=0;k<3;k++)out->spawns[i].position[k]=f32(data+at+i*16+k*4);
        out->spawns[i].facing=f32(data+at+i*16+12);
        if(!isfinite(out->spawns[i].position[0])||!isfinite(out->spawns[i].position[1])||!isfinite(out->spawns[i].position[2])||!isfinite(out->spawns[i].facing)){fail(err,errlen,"non-finite spawn");goto done;}
    }
    at+=(size_t)sc*16;
    spawn_teams(data+64,ml,out->spawns,sc);
    out->key=2166136261u;
    for(uint32_t i=0;i<ml;i++) out->key=(out->key^data[64+i])*16777619u;
    if(at!=size){fail(err,errlen,"unexpected trailing package bytes");goto done;}
    out->mesh.ambient[0]=out->mesh.ambient[1]=out->mesh.ambient[2]=0.8f;
    ok=true;
done:
    if(!ok)hta_external_map_free(out);
    return ok;
}
