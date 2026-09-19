/* htaview — load a cache file, extract the first structure BSP, render it
 * offscreen with Vulkan, and write PPM images from a few camera angles.
 *
 * This is the host-side visual check. It needs no window system, so it works
 * over SSH and doubles as an automated renderer test.
 *
 *   htaview <cache.map> [--out prefix] [--width N] [--height N] [--shots N]
 *                       [--fp [clip]] [--eye X Y Z] [--yaw DEG] [--pitch DEG]
 *                       [--flash] [--ammo N]
 *
 * --fp stands at a player spawn with the animated first-person viewmodel up,
 * stepping one animation frame per shot. That is the visual check for the
 * hands/weapon skinning; "clip" defaults to idle.
 *
 * --eye puts the camera at an exact eye position instead, which is how you
 * reproduce a player's screenshot from the coordinates in their HUD readout:
 * pass their feet Z plus the standing eye height (0.62).
 */
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/bitmap.h"
#include "asset/model.h"
#include "asset/weapon.h"
#include "engine/camera.h"
#include "engine/viewmodel.h"
#include "engine/hud.h"
#include "asset/biped.h"
#include "gfx/gfx.h"
#include "engine/scene_light.h"
#include "platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *b = malloc((size_t)n);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    *out = (size_t)n;
    return b;
}

static bool write_ppm(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t i = 0; i < w * h; i++) fwrite(&rgba[i * 4], 1, 3, f);
    fclose(f);
    return true;
}

/* fraction of pixels that differ from the clear colour — our "did anything
 * actually draw?" metric */
static double coverage(const uint8_t *rgba, uint32_t w, uint32_t h,
                       const float clear[3])
{
    uint8_t cr = (uint8_t)(clear[0] * 255.0f + 0.5f);
    uint8_t cg = (uint8_t)(clear[1] * 255.0f + 0.5f);
    uint8_t cb = (uint8_t)(clear[2] * 255.0f + 0.5f);
    uint64_t hit = 0, total = (uint64_t)w * h;
    for (uint64_t i = 0; i < total; i++) {
        int dr = (int)rgba[i*4+0] - cr, dg = (int)rgba[i*4+1] - cg, db = (int)rgba[i*4+2] - cb;
        if (dr*dr + dg*dg + db*db > 48) hit++;
    }
    return total ? (double)hit / (double)total : 0.0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <cache.map> [--out prefix] [--width N] [--height N] [--shots N]\n", argv[0]);
        return 2;
    }
    const char *prefix = "bloodgulch";
    const char *fp_clip = "idle";
    int fp_mode = 0;
    int have_eye = 0, want_flash = 0, ammo = -1;
    const char *want_weapon = NULL;
    float shield = 1.0f, health = 1.0f;
    float eye[3] = {0.0f, 0.0f, 0.0f}, eye_yaw = 0.0f, eye_pitch = 0.0f;
    uint32_t W = 1280, H = 720, shots = 4;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--out")    && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "--width")  && i + 1 < argc) W = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) H = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shots")  && i + 1 < argc) shots = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--eye") && i + 3 < argc) {
            eye[0] = strtof(argv[i+1], NULL);
            eye[1] = strtof(argv[i+2], NULL);
            eye[2] = strtof(argv[i+3], NULL);
            have_eye = 1;
            i += 3;
        }
        else if (!strcmp(argv[i], "--yaw") && i + 1 < argc)
            eye_yaw = strtof(argv[++i], NULL) * 0.01745329f;
        else if (!strcmp(argv[i], "--pitch") && i + 1 < argc)
            eye_pitch = strtof(argv[++i], NULL) * 0.01745329f;
        else if (!strcmp(argv[i], "--flash")) want_flash = 1;
        else if (!strcmp(argv[i], "--weapon") && i + 1 < argc) want_weapon = argv[++i];
        else if (!strcmp(argv[i], "--ammo") && i + 1 < argc) ammo = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shield") && i + 1 < argc) shield = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--health") && i + 1 < argc) health = strtof(argv[++i], NULL);
        else if (!strcmp(argv[i], "--fp")) {
            fp_mode = 1;
            if (i + 1 < argc && argv[i+1][0] != '-') fp_clip = argv[++i];
        }
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (W == 0 || H == 0 || W > 8192 || H > 8192) { fprintf(stderr, "bad size\n"); return 2; }
    if (shots > 64) shots = 64;

    size_t size = 0;
    uint8_t *data = slurp(argv[1], &size);
    if (!data) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    char err[HTA_ERRLEN] = {0};
    hta_cache c;
    if (!hta_cache_open(&c, data, size, err, sizeof(err))) {
        fprintf(stderr, "cache: %s\n", err); free(data); return 1;
    }
    printf("map            %s (%s, engine %u)\n", c.name,
           c.is_demo_layout ? "Trial layout" : "retail layout", c.engine);

    double t0 = hta_time_seconds();
    hta_bsp_mesh mesh;
    if (!hta_bsp_load_first(&c, &mesh, err, sizeof(err))) {
        fprintf(stderr, "bsp: %s\n", err); free(data); return 1;
    }
    double t_parse = hta_time_seconds() - t0;

    printf("geometry       %u verts, %u tris, %u submeshes  (parsed in %.1f ms)\n",
           mesh.vertex_count, mesh.index_count / 3, mesh.submesh_count, t_parse * 1000.0);
    printf("bounds         (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)\n",
           mesh.bounds_min[0], mesh.bounds_min[1], mesh.bounds_min[2],
           mesh.bounds_max[0], mesh.bounds_max[1], mesh.bounds_max[2]);

    /* bitmaps.map lives next to the cache on Gearbox Trial */
    hta_resource_map rm;
    memset(&rm, 0, sizeof(rm));
    uint8_t *bmdata = NULL; size_t bmsz = 0;
    {
        char bmpath[1024];
        snprintf(bmpath, sizeof(bmpath), "%s", argv[1]);
        char *slash = strrchr(bmpath, '/');
        if (slash) snprintf(slash + 1, sizeof(bmpath) - (size_t)(slash + 1 - bmpath), "bitmaps.map");
        else snprintf(bmpath, sizeof(bmpath), "bitmaps.map");
        bmdata = slurp(bmpath, &bmsz);
        if (bmdata && hta_resource_open(&rm, bmdata, bmsz, err, sizeof(err)))
            printf("bitmaps.map    %s (%.1f MiB)\n", bmpath, bmsz / (1024.0 * 1024.0));
        else
            printf("bitmaps.map    not found next to cache (untextured)\n");
    }
    if (!hta_bsp_load_textures(&c, rm.data ? &rm : NULL, &mesh, err, sizeof(err)))
        printf("textures       failed: %s\n", err);
    else
        printf("textures       %u unique decoded\n", mesh.texture_count);
    if (hta_scenario_add_objects(&mesh, &c, rm.data ? &rm : NULL, err, sizeof(err)))
        printf("objects        %s  (%u verts, %u submeshes)\n", err, mesh.vertex_count, mesh.submesh_count);
    hta_bsp_mesh sky;
    memset(&sky, 0, sizeof(sky));
    int have_sky = 0;
    if (hta_sky_load(&sky, &c, rm.data ? &rm : NULL, err, sizeof(err))) {
        have_sky = 1;
        printf("sky            %u verts, %u submeshes\n", sky.vertex_count, sky.submesh_count);
    } else {
        printf("sky            %s\n", err);
    }

    t0 = hta_time_seconds();
    hta_gfx *g = hta_gfx_create_offscreen(W, H, err, sizeof(err));
    if (!g) { fprintf(stderr, "vulkan: %s\n", err); hta_bsp_free(&mesh); free(data); free(bmdata); return 1; }
    double t_gfx = hta_time_seconds() - t0;
    printf("gpu            %s (init %.1f ms)\n", hta_gfx_device_name(g), t_gfx * 1000.0);

    t0 = hta_time_seconds();
    hta_gfx_mesh *gm = hta_gfx_mesh_upload(g, &mesh, err, sizeof(err));
    hta_gfx_mesh *gs = NULL;
    if (have_sky) {
        gs = hta_gfx_mesh_upload(g, &sky, err, sizeof(err));
        if (!gs) fprintf(stderr, "sky upload: %s\n", err);
    }
    if (!gm) { fprintf(stderr, "upload: %s\n", err); hta_gfx_destroy(g); hta_bsp_free(&mesh); free(data); free(bmdata); return 1; }
    double t_upload = hta_time_seconds() - t0;
    printf("upload         %.1f ms, device memory %.2f MiB\n",
           t_upload * 1000.0, hta_gfx_device_memory_used(g) / (1024.0*1024.0));

    hta_viewmodel vm;
    hta_weapon_def wdef;
    hta_gfx_mesh *gvm = NULL;
    int32_t fp_anim = -1;
    memset(&vm, 0, sizeof(vm));
    memset(&wdef, 0, sizeof(wdef));
    if (fp_mode) {
        int wok = 0;
        if (want_weapon) {
            uint32_t ids[32];
            uint32_t n = hta_weapon_list_playable(&c, ids, 32);
            for (uint32_t k = 0; k < n && !wok; k++) {
                hta_weapon_def probe;
                if (!hta_weapon_load_id(&c, NULL, ids[k], &probe, NULL, NULL, 0)) continue;
                if (!strstr(probe.path, want_weapon)) continue;
                wok = hta_weapon_load_id(&c, rm.data ? &rm : NULL, ids[k], &wdef,
                                         NULL, err, sizeof(err));
            }
            if (!wok) printf("weapon         no playable weapon matching '%s'\n", want_weapon);
        } else {
            wok = hta_weapon_load_default(&c, rm.data ? &rm : NULL, &wdef, NULL,
                                          err, sizeof(err));
        }
        if (!wok)
            printf("weapon         %s\n", err);
        else if (!hta_viewmodel_load(&vm, &c, rm.data ? &rm : NULL, &wdef, err, sizeof(err)))
            printf("viewmodel      %s\n", err);
        else {
            fp_anim = hta_anim_find(&vm.graph, fp_clip);
            printf("viewmodel      %s: %u verts (%u hands + %u gun), clip '%s' %s\n",
                   wdef.path, vm.mesh.vertex_count, vm.hands_verts, vm.gun_verts,
                   fp_clip, fp_anim >= 0 ? vm.graph.anims[fp_anim].name : "NOT FOUND");
            gvm = hta_gfx_mesh_upload_dynamic(g, &vm.mesh, err, sizeof(err));
            if (!gvm) printf("viewmodel      upload failed: %s\n", err);
        }
    }

    /* frame the whole BSP: orbit the bounding sphere */
    float ctr[3], radius = 0.0f;
    for (int k = 0; k < 3; k++) ctr[k] = 0.5f * (mesh.bounds_min[k] + mesh.bounds_max[k]);
    for (int k = 0; k < 3; k++) {
        float half = 0.5f * (mesh.bounds_max[k] - mesh.bounds_min[k]);
        if (half > radius) radius = half;
    }
    if (radius < 1e-3f) radius = 1.0f;

    hta_scene scene;
    memset(&scene, 0, sizeof(scene));
    hta_scene_light_from_bsp(&mesh, scene.light_dir, scene.light_color, scene.ambient);
    scene.clear[0] = 0.42f; scene.clear[1] = 0.55f; scene.clear[2] = 0.72f;

    /* HUD from the weapon's own wphi. */
    hta_hud hud;
    memset(&hud, 0, sizeof(hud));
    hta_gfx_mesh *ghud = NULL;
    hta_gfx_overlay huddraw;
    memset(&huddraw, 0, sizeof(huddraw));
    if (vm.loaded) {
        char herr[HTA_ERRLEN] = {0};
        hta_hud_load(&hud, &c, rm.data ? &rm : NULL, &wdef, herr, sizeof(herr));
        if (hud.elem_count) {
            hta_hud_set_shield(&hud, shield);
            hta_hud_set_health(&hud, health);
            hta_hud_set_ammo(&hud, ammo >= 0 ? (float)ammo / 60.0f : 1.0f);
            hta_hud_layout(&hud, W, H);
            ghud = hta_gfx_mesh_upload_dynamic(g, &hud.mesh, herr, sizeof(herr));
            if (ghud) {
                huddraw.mesh = ghud;
                huddraw.vertices = hud.mesh.vertices;
                huddraw.vertex_count = hud.mesh.vertex_count;
                huddraw.submeshes = hud.mesh.submeshes;
                huddraw.submesh_count = hud.mesh.submesh_count;
                printf("hud            %u element(s); crosshair %s (%.0f px), unit hud %s\n",
                       hud.elem_count, hud.have_cross ? "yes" : "no",
                       hud.cross_px, hud.have_unit ? "yes" : "no");
                printf("               ammo block %s\n", hud.have_ammo ? "yes" : "no");
                for (uint32_t ei = 0; ei < hud.elem_count; ei++) {
                    const hta_hud_elem *el = &hud.elem[ei];
                    const hta_submesh *sm = &hud.mesh.submeshes[el->submesh];
                    const float *p0 = hud.mesh.vertices[el->vertex].pos;
                    const float *p2 = hud.mesh.vertices[el->vertex + 2].pos;
                    printf("   elem %u anchor %u off (%.0f,%.0f) native %.0fx%.0f"
                           " -> px (%.0f,%.0f)..(%.0f,%.0f) tint %.2f %.2f %.2f a %.2f meter %.2f\n",
                           ei, el->anchor, el->offset[0], el->offset[1], el->w_px, el->h_px,
                           (p0[0]+1.0f)*0.5f*W, (p0[1]+1.0f)*0.5f*H,
                           (p2[0]+1.0f)*0.5f*W, (p2[1]+1.0f)*0.5f*H,
                           sm->tint[0], sm->tint[1], sm->tint[2], sm->tint[3], sm->meter);
                }
            }
        } else {
            printf("hud            nothing to draw (%s)\n", herr);
        }
    }

    uint8_t *pixels = malloc((size_t)W * H * 4);
    if (!pixels) { fprintf(stderr, "oom\n"); return 1; }

    /* warm up, then time steady-state frames */
    hta_camera cam;
    hta_camera_init(&cam);
    cam.aspect = (float)W / (float)H;
    cam.zfar   = radius * 12.0f;
    cam.znear  = radius * 0.005f > 0.01f ? radius * 0.005f : 0.01f;

    printf("\n");
    double total_render = 0.0;
    uint32_t drawn = 0;
    hta_spawn_point spawn;
    int have_spawn = hta_scenario_spawns(&c, &spawn, 1) > 0;
    if (fp_mode || have_eye) {
        cam.znear = 0.02f;
        cam.zfar  = radius * 12.0f;
    }
    if (have_eye) fp_mode = 1;   /* --eye implies the first-person camera */
    for (uint32_t s = 0; s < shots; s++) {
        if (fp_mode) {
            /* Stand where a player spawns and look along the spawn facing --
             * unless --eye named an exact spot to reproduce. */
            if (have_eye) {
                cam.pos[0] = eye[0];
                cam.pos[1] = eye[1];
                cam.pos[2] = eye[2];
                cam.yaw = eye_yaw;
            } else if (have_spawn) {
                cam.pos[0] = spawn.position[0];
                cam.pos[1] = spawn.position[1];
                cam.pos[2] = spawn.position[2] + 0.62f;  /* Trial standing eye */
                cam.yaw = spawn.facing;
            } else {
                cam.pos[0] = ctr[0]; cam.pos[1] = ctr[1]; cam.pos[2] = ctr[2] + 1.0f;
                cam.yaw = 0.0f;
            }
            cam.pitch = have_eye ? eye_pitch : 0.0f;
            /* --flash lights the muzzle flash for the shot, so it can be
             * looked at without a device. */
            if (ammo >= 0 && vm.loaded) hta_viewmodel_set_counter(&vm, (uint32_t)ammo);
            if (want_flash && vm.loaded) {
                hta_viewmodel_flash(&vm);
                vm.flash_timer = vm.flash_life;
            }
            if (vm.loaded && fp_anim >= 0) {
                /* One animation frame per shot, straight through the clip. */
                const hta_animation *a = &vm.graph.anims[fp_anim];
                float f = a->frame_count > 1
                        ? (float)(a->frame_count - 1) * (float)s / (float)(shots > 1 ? shots - 1 : 1)
                        : 0.0f;
                hta_transform local[HTA_ANIM_MAX_NODES], world[HTA_ANIM_MAX_NODES];
                if (hta_anim_sample(&vm.graph, (uint32_t)fp_anim, f, local)) {
                    hta_anim_world(&vm.graph, local, world);
                    hta_viewmodel_pose(&vm, world);
                }
            }
        } else {
            float ang = 6.2831853f * (float)s / (float)(shots ? shots : 1);
            float dist = radius * 2.4f;
            cam.pos[0] = ctr[0] + cosf(ang) * dist;
            cam.pos[1] = ctr[1] + sinf(ang) * dist;
            cam.pos[2] = ctr[2] + radius * 0.85f;
            /* aim at the centre */
            float dx = ctr[0]-cam.pos[0], dy = ctr[1]-cam.pos[1], dz = ctr[2]-cam.pos[2];
            cam.yaw   = atan2f(dy, dx);
            cam.pitch = atan2f(dz, sqrtf(dx*dx + dy*dy));
        }

        hta_gfx_viewmodel vmdraw;
        memset(&vmdraw, 0, sizeof(vmdraw));
        if (gvm) {
            vmdraw.mesh = gvm;
            vmdraw.vertices = vm.posed;
            vmdraw.vertex_count = vm.mesh.vertex_count;
            for (int k = 0; k < 3; k++) vmdraw.offset[k] = wdef.fp_offset[k];
        }

        double r0 = hta_time_seconds();
        bool ok = hta_gfx_draw(g, &cam, &scene, gm, gs, NULL, gvm ? &vmdraw : NULL,
                               ghud ? &huddraw : NULL);
        double r1 = hta_time_seconds();
        if (!ok) { fprintf(stderr, "draw failed on shot %u\n", s); break; }
        total_render += (r1 - r0);
        drawn++;

        if (!hta_gfx_readback(g, pixels, (size_t)W * H * 4)) {
            fprintf(stderr, "readback failed\n"); break;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s_%02u.ppm", prefix, s);
        if (!write_ppm(path, pixels, W, H)) { fprintf(stderr, "cannot write %s\n", path); break; }
        printf("  %s  coverage %5.1f%%  (%.1f ms)\n", path,
               coverage(pixels, W, H, scene.clear) * 100.0, (r1 - r0) * 1000.0);
    }

    if (drawn) {
        printf("\nrender         %u frames, avg %.2f ms (%.0f fps equivalent, %ux%u, incl. readback)\n",
               drawn, total_render * 1000.0 / drawn, drawn / total_render, W, H);
        printf("triangles      %u per frame\n", mesh.index_count / 3);
    }

    free(pixels);
    if (gvm) hta_gfx_mesh_free(g, gvm);
    hta_viewmodel_free(&vm);
    if (gs) hta_gfx_mesh_free(g, gs);
    hta_gfx_mesh_free(g, gm);
    hta_gfx_destroy(g);
    hta_bsp_free(&mesh);
    hta_bsp_free(&sky);
    free(data);
    free(bmdata);
    return drawn ? 0 : 1;
}
