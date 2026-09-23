/* Host proof for OALMAP v1: real Open Halo Vulkan upload, draw and collision. */
#include "asset/external_map.h"
#include "engine/player.h"
#include "engine/camera.h"
#include "gfx/gfx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}

static int ppm(const char *path,const unsigned char *p,unsigned w,unsigned h)
{
    FILE *f=fopen(path,"wb");if(!f)return 0;
    fprintf(f,"P6\n%u %u\n255\n",w,h);
    for(size_t i=0;i<(size_t)w*h;i++)fwrite(p+4*i,1,3,f);
    fclose(f);return 1;
}
int main(int argc,char **argv)
{
    if(argc<2||argc>4){fprintf(stderr,"usage: %s map.oalmap [output-prefix] [spawn-index]\n",argv[0]);return 2;}
    const char *prefix=argc>2?argv[2]:"oalmap";
    char err[256]={0};hta_external_map map;double t0=now();
    if(!hta_external_map_load(argv[1],&map,err,sizeof(err))){fprintf(stderr,"package: %s\n",err);return 1;}
    printf("host loading: %.2f ms\n",(now()-t0)*1000.0);
    hta_bsp_mesh *mesh=&map.mesh;
    printf("package: vertices %u, triangles %u, materials %u, textures %u, spawns %u\n",
      mesh->vertex_count,mesh->index_count/3,mesh->submesh_count,mesh->texture_count,map.spawn_count);
    hta_collision col;double tc=now();
    if(!hta_collision_build(&col,mesh)){fprintf(stderr,"collision grid build failed\n");hta_external_map_free(&map);return 1;}
    printf("collision: %u triangles, %ux%u cells, %.2f ms\n",col.tri_count,col.nx,col.ny,(now()-tc)*1000.0);
    unsigned usable=0;
    for(unsigned i=0;i<map.spawn_count;i++){
        float *p=map.spawns[i].position,ground=0;
        int hit=hta_collision_ground(&col,p[0],p[1],p[2]+1.0f,&ground);
        int good=hit && fabsf(p[2]-ground)<1.5f;
        usable+=good;
        printf("spawn %u: %.3f %.3f %.3f yaw %.1f deg; ground %s %.3f; %s\n",i,p[0],p[1],p[2],map.spawns[i].facing*57.29578f,hit?"at":"missing",ground,good?"usable":"unverified");
    }
    printf("usable spawns: %u/%u\n",usable,map.spawn_count);
    if(!usable){fprintf(stderr,"no usable spawn over collision\n");hta_collision_free(&col);hta_external_map_free(&map);return 1;}
    const unsigned W=960,H=540;
    hta_gfx *gfx=hta_gfx_create_offscreen(W,H,err,sizeof(err));
    if(!gfx){fprintf(stderr,"renderer: %s\n",err);hta_collision_free(&col);hta_external_map_free(&map);return 1;}
    printf("renderer: %s\n",hta_gfx_device_name(gfx));
    double tu=now();hta_gfx_mesh *gm=hta_gfx_mesh_upload(gfx,mesh,err,sizeof(err));
    if(!gm){fprintf(stderr,"mesh upload: %s\n",err);hta_gfx_destroy(gfx);hta_collision_free(&col);hta_external_map_free(&map);return 1;}
    printf("GPU upload: %.2f ms\n",(now()-tu)*1000.0);
    hta_scene scene={.light_dir={0.35f,0.4f,0.85f},.light_color={1,1,1},.ambient={0.7f,0.7f,0.7f},.clear={0.1f,0.15f,0.23f}};
    hta_camera cam;hta_camera_init(&cam);cam.aspect=(float)W/H;cam.znear=0.03f;
    float ctr[3],span=0;
    for(int k=0;k<3;k++){ctr[k]=(mesh->bounds_min[k]+mesh->bounds_max[k])*0.5f;float d=mesh->bounds_max[k]-mesh->bounds_min[k];if(d>span)span=d;}
    cam.zfar=fmaxf(100.f,span*8.f);
    cam.pos[0]=ctr[0]-span*0.75f;cam.pos[1]=ctr[1]-span*0.85f;cam.pos[2]=ctr[2]+span*0.45f;
    float dx=ctr[0]-cam.pos[0],dy=ctr[1]-cam.pos[1],dz=ctr[2]-cam.pos[2];
    cam.yaw=atan2f(dy,dx);cam.pitch=atan2f(dz,hypotf(dx,dy));
    unsigned char *pixels=malloc((size_t)W*H*4);
    int good=1;
    for(int shot=0;shot<2;shot++){
        if(shot){
            unsigned target=0;
            if(argc>3)target=(unsigned)atoi(argv[3]);
            if(target>=map.spawn_count){fprintf(stderr,"spawn index out of range\n");good=0;break;}
            const hta_spawn_point *sp=&map.spawns[target];
            memcpy(cam.pos,sp->position,sizeof(cam.pos));cam.pos[2]+=0.55f;
            cam.yaw=sp->facing;cam.pitch=0;cam.znear=0.02f;
            /* OALMAP_CAMERA="x y z yaw_deg [pitch_deg]" reproduces a device report's eye position. */
            const char *view=getenv("OALMAP_CAMERA");float v[5]={0};
            if(view&&sscanf(view,"%f %f %f %f %f",&v[0],&v[1],&v[2],&v[3],&v[4])>=4){
                memcpy(cam.pos,v,sizeof(cam.pos));cam.yaw=v[3]/57.29578f;cam.pitch=v[4]/57.29578f;
            }
        }
        if(!hta_gfx_draw(gfx,&cam,&scene,gm,NULL,NULL,NULL,0,NULL,NULL)||!hta_gfx_readback(gfx,pixels,(size_t)W*H*4)){fprintf(stderr,"draw/readback failed\n");good=0;break;}
        char path[1024];snprintf(path,sizeof(path),"%s_%s.ppm",prefix,shot?"spawn":"overview");
        if(!ppm(path,pixels,W,H)){fprintf(stderr,"cannot write %s\n",path);good=0;break;}
        unsigned long visible=0;
        for(size_t i=0;i<(size_t)W*H;i++){
            int dr=(int)pixels[4*i]-26,dg=(int)pixels[4*i+1]-38,db=(int)pixels[4*i+2]-59;
            if(dr*dr+dg*dg+db*db>48)visible++;
        }
        printf("image: %s, coverage %.2f%%\n",path,100.0*(double)visible/(W*H));
        if(visible<W*H/1000){fprintf(stderr,"image appears empty\n");good=0;break;}
    }
    printf("GPU allocation: %.1f MiB\n",hta_gfx_device_memory_used(gfx)/(1024.0*1024.0));
    free(pixels);hta_gfx_mesh_free(gfx,gm);hta_gfx_destroy(gfx);
    hta_collision_free(&col);hta_external_map_free(&map);
    return good?0:1;
}
