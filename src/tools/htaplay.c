/* Thin desktop LAN client. Portable engine + offscreen Vulkan shown in SDL2.
 * It intentionally keeps the Android gameplay loop intact while proving that
 * two real Blood Gulch windows can share a live session. */
#define _POSIX_C_SOURCE 200809L
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/bitmap.h"
#include "asset/biped.h"
#include "engine/player.h"
#include "engine/actor.h"
#include "engine/scene_light.h"
#include "gfx/gfx.h"
#include "net/session.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH=800, HEIGHT=450 };
static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *f=fopen(path,"rb"); if (!f) return NULL;
    if (fseek(f,0,SEEK_END)) { fclose(f); return NULL; }
    long n=ftell(f); if (n<=0 || fseek(f,0,SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *p=malloc((size_t)n); if (!p) { fclose(f); return NULL; }
    if (fread(p,1,(size_t)n,f)!=(size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f); *size=(size_t)n; return p;
}
static uint32_t find_tag(const hta_cache *c, uint32_t cls, const char *part)
{
    for (uint32_t i=0;i<c->tag_count;i++) {
        hta_tag_entry t; char path[256];
        if (!hta_cache_tag(c,i,&t) || t.primary_class!=cls) continue;
        hta_cache_tag_path(c,&t,path,sizeof(path));
        if (strstr(path,part)) return t.tag_id;
    }
    return 0;
}
static bool ppm(const char *path,const uint8_t *rgba)
{
    FILE *f=fopen(path,"wb"); if (!f) return false;
    fprintf(f,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
    for (int i=0;i<WIDTH*HEIGHT;i++) fwrite(rgba+i*4,1,3,f);
    fclose(f); return true;
}
static void action(hta_net_client *net,uint8_t kind,uint8_t weapon,uint32_t *event_id)
{
    if (!net->connected) return;
    hta_net_event e={net->id,kind,weapon,++*event_id};
    hta_net_client_event(net,&e);
}

int main(int argc,char **argv)
{
    if (argc<3) {
        fprintf(stderr,"usage: htaplay <bloodgulch.map> <server IPv4> [port] [--auto seconds] [--shot path]\n");
        return 2;
    }
    unsigned port=32270; double auto_seconds=0; const char *shot=NULL;
    for (int i=3;i<argc;i++) {
        if (!strcmp(argv[i],"--auto") && i+1<argc) auto_seconds=atof(argv[++i]);
        else if (!strcmp(argv[i],"--shot") && i+1<argc) shot=argv[++i];
        else port=(unsigned)atoi(argv[i]);
    }
    if (port<1 || port>65535 || auto_seconds<0 || auto_seconds>3600) return 2;
    char err[HTA_ERRLEN]={0}; size_t map_size=0,bm_size=0;
    uint8_t *map_data=slurp(argv[1],&map_size);
    if (!map_data) { fprintf(stderr,"cannot read map\n"); return 1; }
    hta_cache cache;
    if (!hta_cache_open(&cache,map_data,map_size,err,sizeof(err))) {
        fprintf(stderr,"map: %s\n",err); return 1;
    }
    char bmpath[1024]; snprintf(bmpath,sizeof(bmpath),"%s",argv[1]);
    char *slash=strrchr(bmpath,'/');
    if (slash) snprintf(slash+1,sizeof(bmpath)-(size_t)(slash+1-bmpath),"bitmaps.map");
    else snprintf(bmpath,sizeof(bmpath),"bitmaps.map");
    uint8_t *bm_data=slurp(bmpath,&bm_size); hta_resource_map bm={0};
    if (bm_data) hta_resource_open(&bm,bm_data,bm_size,err,sizeof(err));
    hta_bsp_mesh mesh={0},sky={0},collision_mesh={0};
    if (!hta_bsp_load_first(&cache,&mesh,err,sizeof(err)) ||
        !hta_bsp_load_textures(&cache,bm.data ? &bm : NULL,&mesh,err,sizeof(err))) {
        fprintf(stderr,"BSP: %s\n",err); return 1;
    }
    hta_scenario_add_objects(&mesh,&cache,bm.data ? &bm : NULL,err,sizeof(err));
    hta_sky_load(&sky,&cache,bm.data ? &bm : NULL,err,sizeof(err));
    hta_collision col={0};
    if (!hta_bsp_load_collision(&cache,&collision_mesh,err,sizeof(err)) ||
        !hta_collision_build(&col,&collision_mesh)) {
        fprintf(stderr,"collision: %s\n",err); return 1;
    }
    hta_player player; hta_player_init(&player);
    hta_player_physics physics;
    if (hta_player_physics_load(&physics,&cache,err,sizeof(err))) {
        hta_player_apply_physics(&player,&physics);
        hta_collision_set_slope(&col,physics.max_slope);
    }
    hta_spawn_point spawns[64]; uint32_t spawn_count=hta_scenario_spawns(&cache,spawns,64);
    if (!spawn_count) { fprintf(stderr,"map has no spawn points\n"); return 1; }
    hta_camera cam; hta_camera_init(&cam); cam.aspect=(float)WIDTH/HEIGHT;
    cam.znear=0.02f; cam.zfar=(mesh.bounds_max[0]-mesh.bounds_min[0])*6;
    hta_player_spawn(&player,&spawns[0]); cam.yaw=spawns[0].facing;
    hta_scene scene={0}; hta_scene_light_from_bsp(&mesh,scene.light_dir,scene.light_color,scene.ambient);
    scene.clear[0]=0.42f; scene.clear[1]=0.55f; scene.clear[2]=0.72f;
    uint32_t bip=find_tag(&cache,HTA_FOURCC('b','i','p','d'),"cyborg_mp");
    hta_actor remote[2]={{0}};
    if (!bip) { fprintf(stderr,"no multiplayer Spartan\n"); return 1; }
    uint32_t models[2]={
        find_tag(&cache,HTA_FOURCC('m','o','d','2'),"weapons\\assault rifle\\assault rifle"),
        find_tag(&cache,HTA_FOURCC('m','o','d','2'),"weapons\\pistol\\pistol")};
    for (int slot=0;slot<2;slot++) {
        hta_actor *a=&remote[slot];
        if (!hta_actor_load(a,&cache,bm.data ? &bm : NULL,bip,err,sizeof(err))) {
            fprintf(stderr,"Spartan: %s\n",err); return 1;
        }
        if (models[slot]) hta_actor_hold(a,&cache,bm.data ? &bm : NULL,
                                          models[slot],"right hand",err,sizeof(err));
        hta_actor_play(a,slot ? "stand pistol idle" : "stand rifle idle",false);
    }
    hta_net_client net;
    if (!hta_net_client_open(&net,argv[2],(uint16_t)port)) {
        fprintf(stderr,"bad server address\n"); return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)) { fprintf(stderr,"SDL: %s\n",SDL_GetError()); return 1; }
    SDL_Window *window=SDL_CreateWindow("Blood Gulch LAN",SDL_WINDOWPOS_CENTERED,
                        SDL_WINDOWPOS_CENTERED,WIDTH,HEIGHT,0);
    SDL_Renderer *renderer=window ? SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE) : NULL;
    SDL_Texture *texture=renderer ? SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,
                                                      SDL_TEXTUREACCESS_STREAMING,WIDTH,HEIGHT) : NULL;
    if (!texture) { fprintf(stderr,"SDL video: %s\n",SDL_GetError()); return 1; }
    if (!auto_seconds) SDL_SetRelativeMouseMode(SDL_TRUE);
    hta_gfx *gfx=hta_gfx_create_offscreen(WIDTH,HEIGHT,err,sizeof(err));
    if (!gfx) { fprintf(stderr,"Vulkan: %s\n",err); return 1; }
    hta_gfx_mesh *world=hta_gfx_mesh_upload(gfx,&mesh,err,sizeof(err));
    hta_gfx_mesh *skygpu=sky.index_count ? hta_gfx_mesh_upload(gfx,&sky,err,sizeof(err)) : NULL;
    hta_gfx_mesh *actor_gpu[2];
    for (int slot=0;slot<2;slot++) actor_gpu[slot]=hta_gfx_mesh_upload_dynamic_world(
        gfx,&remote[slot].mesh,err,sizeof(err));
    if (!world || !actor_gpu[0] || !actor_gpu[1]) { fprintf(stderr,"GPU: %s\n",err); return 1; }
    uint8_t *rgba=malloc((size_t)WIDTH*HEIGHT*4u);
    if (!rgba) return 1;
    bool running=true, spawned=false, remote_visible=false, saw_remote=false;
    hta_net_player from={0},to={0}; uint8_t remote_id=0,weapon=0;
    uint64_t last_snapshots=0; uint32_t event_id=0; unsigned auto_actions=0;
    double start=(double)SDL_GetTicks64()/1000.0,last=start,last_send=0,last_log=start;
    double snapshot_time=0,action_until=0;
    float mouse_yaw=0,mouse_pitch=0;
    while (running) {
        double now=(double)SDL_GetTicks64()/1000.0;
        float dt=(float)(now-last); last=now; if (dt>0.05f) dt=0.05f;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_QUIT) running=false;
            if (event.type==SDL_MOUSEMOTION && !auto_seconds) {
                mouse_yaw-=event.motion.xrel*0.003f;
                mouse_pitch-=event.motion.yrel*0.003f;
            }
            if (event.type==SDL_MOUSEBUTTONDOWN) {
                if (event.button.button==SDL_BUTTON_LEFT) action(&net,HTA_NET_EVENT_FIRE,weapon,&event_id);
                if (event.button.button==SDL_BUTTON_RIGHT) action(&net,HTA_NET_EVENT_MELEE,weapon,&event_id);
            }
            if (event.type==SDL_KEYDOWN && !event.key.repeat) {
                SDL_Keycode key=event.key.keysym.sym;
                if (key==SDLK_ESCAPE) running=false;
                if (key==SDLK_g) action(&net,HTA_NET_EVENT_GRENADE,weapon,&event_id);
                if (key==SDLK_1 || key==SDLK_2) {
                    weapon=key==SDLK_1 ? 0 : 1;
                    action(&net,HTA_NET_EVENT_WEAPON,weapon,&event_id);
                }
            }
        }
        hta_net_client_pump(&net,now);
        if (net.connected && !spawned) {
            hta_spawn_point *sp=&spawns[(net.id-1u)%spawn_count];
            hta_player_spawn(&player,sp); cam.yaw=sp->facing;
            float z;
            if (hta_collision_ground(&col,player.pos[0],player.pos[1],player.pos[2]+8,&z))
                { player.pos[2]=z; player.on_ground=true; }
            spawned=true; printf("joined as player %u\n",net.id); fflush(stdout);
        }
        const uint8_t *keys=SDL_GetKeyboardState(NULL);
        hta_player_input input={0};
        input.move_forward=(keys[SDL_SCANCODE_W]?1.0f:0)-(keys[SDL_SCANCODE_S]?1.0f:0);
        input.move_right=(keys[SDL_SCANCODE_D]?1.0f:0)-(keys[SDL_SCANCODE_A]?1.0f:0);
        input.jump=keys[SDL_SCANCODE_SPACE]; input.crouch=keys[SDL_SCANCODE_LCTRL];
        input.look_yaw=mouse_yaw; input.look_pitch=mouse_pitch;
        mouse_yaw=mouse_pitch=0;
        if (auto_seconds) {
            double t=now-start;
            input.move_forward=1; input.look_yaw=dt*0.4f;
            input.jump=t>1.0 && t<1.2;
            input.crouch=t>2.0 && t<3.0;
            if (t>1.0 && !(auto_actions&1u)) {
                action(&net,HTA_NET_EVENT_FIRE,weapon,&event_id); auto_actions|=1u;
            }
            if (t>2.0 && !(auto_actions&2u)) {
                action(&net,HTA_NET_EVENT_MELEE,weapon,&event_id); auto_actions|=2u;
            }
            if (t>3.0 && !(auto_actions&4u)) {
                weapon=1; action(&net,HTA_NET_EVENT_WEAPON,weapon,&event_id);
                action(&net,HTA_NET_EVENT_GRENADE,weapon,&event_id); auto_actions|=4u;
            }
            if (t>4.0 && !(auto_actions&8u)) {
                action(&net,HTA_NET_EVENT_FIRE,weapon,&event_id); auto_actions|=8u;
            }
        }
        hta_player_update(&player,&cam,&col,&input,dt);
        if (net.connected && now-last_send>=0.05) {
            hta_net_player p={.id=net.id,.weapon=weapon,.yaw=cam.yaw,.pitch=cam.pitch};
            for (int k=0;k<3;k++) { p.pos[k]=player.pos[k]; p.velocity[k]=player.velocity[k]; }
            if (player.on_ground) p.flags|=HTA_NET_GROUNDED;
            if (player.crouch_t>0.5f) p.flags|=HTA_NET_CROUCH;
            hta_net_client_state(&net,&p); last_send=now;
        }
        if (net.stats.snapshots_in!=last_snapshots) {
            last_snapshots=net.stats.snapshots_in;
            bool was_visible=remote_visible; remote_visible=false;
            for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++) {
                if (!net.present[i] || i+1u==net.id) continue;
                hta_net_player next=net.players[i];
                from=was_visible && remote_id==next.id ? to : next;
                to=next; remote_id=next.id; snapshot_time=now;
                remote_visible=saw_remote=true; break;
            }
        }
        hta_net_event e;
        while (hta_net_client_pop_event(&net,&e)) {
            if (e.actor!=remote_id) continue;
            int slot=e.weapon==1 ? 1 : 0;
            const char *clip=e.kind==HTA_NET_EVENT_FIRE ?
                                (slot ? "stand pistol hp fire-1" : "stand rifle ar fire-1") :
                             e.kind==HTA_NET_EVENT_MELEE ?
                                (slot ? "stand pistol hp melee" : "stand rifle ar melee") :
                             e.kind==HTA_NET_EVENT_GRENADE ? "stand rifle throw-grenade" : NULL;
            if (clip && hta_actor_play(&remote[slot],clip,false)) action_until=now+0.35;
        }
        hta_gfx_dynamic dyn={0}; unsigned dyn_count=0;
        if (remote_visible) {
            int slot=to.weapon==1 ? 1 : 0;
            hta_actor *a=&remote[slot];
            const char *clip=(to.flags&HTA_NET_CROUCH) ?
                (slot ? "crouch pistol idle" : "crouch rifle idle") :
                (slot ? "stand pistol idle" : "stand rifle idle");
            if (!(to.flags&HTA_NET_GROUNDED)) clip=(to.flags&HTA_NET_CROUCH)
                ? "crouch rifle airborne" : (slot ? "stand pistol airborne" : "stand rifle airborne");
            else if (hypotf(to.velocity[0],to.velocity[1])>0.2f)
                clip=(to.flags&HTA_NET_CROUCH) ?
                    (slot ? "crouch pistol move-front" : "crouch rifle move-front") :
                    (slot ? "stand pistol move-front" : "stand rifle move-front");
            if (now>=action_until && (a->clip<0 || strcmp(a->graph.anims[a->clip].name,clip)))
                hta_actor_play(a,clip,false);
            float t=(float)((now-snapshot_time)/0.05); if (t<0) t=0; if (t>1) t=1;
            float pos[3]; for (int k=0;k<3;k++) pos[k]=from.pos[k]+(to.pos[k]-from.pos[k])*t;
            float yaw=from.yaw+remainderf(to.yaw-from.yaw,6.2831853f)*t;
            hta_actor_update(a,dt); hta_actor_place(a,pos,yaw);
            dyn.mesh=actor_gpu[slot]; dyn.vertices=a->posed; dyn.vertex_count=a->mesh.vertex_count;
            dyn.lit=true; dyn_count=1;
        }
        if (!hta_gfx_draw(gfx,&cam,&scene,world,skygpu,NULL,&dyn,dyn_count,NULL,NULL) ||
            !hta_gfx_readback(gfx,rgba,(size_t)WIDTH*HEIGHT*4u)) {
            fprintf(stderr,"render failed\n"); running=false; break;
        }
        SDL_UpdateTexture(texture,NULL,rgba,WIDTH*4);
        SDL_RenderCopy(renderer,texture,NULL,NULL); SDL_RenderPresent(renderer);
        if (now-last_log>=2) {
            printf("id=%u remote=%u pos=%.2f %.2f %.2f ping=%.1fms bytes=%llu/%llu\n",
                   net.id,remote_visible?remote_id:0,player.pos[0],player.pos[1],player.pos[2],
                   net.stats.ping_ms,(unsigned long long)net.stats.bytes_in,
                   (unsigned long long)net.stats.bytes_out);
            fflush(stdout); last_log=now;
        }
        if (auto_seconds && now-start>=auto_seconds) running=false;
    }
    if (shot) ppm(shot,rgba);
    bool ok=net.connected && (!auto_seconds || saw_remote);
    hta_net_client_close(&net);
    free(rgba);
    for (int slot=0;slot<2;slot++) hta_gfx_mesh_free(gfx,actor_gpu[slot]);
    hta_gfx_mesh_free(gfx,skygpu);
    hta_gfx_mesh_free(gfx,world); hta_gfx_destroy(gfx);
    SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    for (int slot=0;slot<2;slot++) hta_actor_free(&remote[slot]);
    hta_collision_free(&col);
    hta_bsp_free(&collision_mesh); hta_bsp_free(&sky); hta_bsp_free(&mesh);
    free(bm_data); free(map_data);
    return ok ? 0 : 1;
}
