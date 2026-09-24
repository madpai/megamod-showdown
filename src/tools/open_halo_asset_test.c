/* Host proof for .oalasset: load, pose every clip, render it offscreen.
 *
 *   open-halo-asset-test <asset.oalasset> <output-prefix> [held.oalasset]
 *
 * A character is drawn in each of its roles, halfway through the clip,
 * seen from front-left, on a grey floor; with a weapon asset as the third
 * argument it holds that weapon's world model. A weapon is drawn as its
 * world model and then in first person, one frame per clip. One PPM per
 * shot; the log says what was drawn.
 */
#include "asset/oal_asset.h"
#include "engine/camera.h"
#include "gfx/gfx.h"
#include "game/imported.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ppm(const char *path, const unsigned char *p, unsigned w, unsigned h)
{
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (size_t i = 0; i < (size_t)w * h; i++) fwrite(p + 4 * i, 1, 3, f);
    fclose(f); return 1;
}

/* A 4 x 4 wu grey floor, the only static geometry. */
static void floor_mesh(hta_bsp_mesh *m)
{
    static hta_vertex v[4];
    static uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    static hta_submesh sub;
    static hta_bsp_texture tex;
    static uint8_t px[4] = { 110, 110, 105, 255 };
    float c[4][2] = { { -2, -2 }, { 2, -2 }, { 2, 2 }, { -2, 2 } };
    for (int i = 0; i < 4; i++) {
        memset(&v[i], 0, sizeof(v[i]));
        v[i].pos[0] = c[i][0]; v[i].pos[1] = c[i][1]; v[i].normal[2] = 1;
        v[i].uv[0] = c[i][0]; v[i].uv[1] = c[i][1];
    }
    hta_submesh_init(&sub);
    sub.first_index = 0; sub.index_count = 6; sub.albedo_tex = 0; sub.scene_lit = true;
    tex.width = tex.height = 1; tex.rgba = px;
    memset(m, 0, sizeof(*m));
    m->vertices = v; m->vertex_count = 4; m->indices = idx; m->index_count = 6;
    m->submeshes = &sub; m->submesh_count = 1; m->textures = &tex; m->texture_count = 1;
    m->bounds_min[0] = m->bounds_min[1] = -2; m->bounds_max[0] = m->bounds_max[1] = 2;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s asset.oalasset out-prefix [held-weapon.oalasset]\n", argv[0]); return 2; }
    static hta_oal_asset a, held;
    char err[256];
    if (!hta_oal_load(argv[1], &a, err, sizeof(err))) { fprintf(stderr, "asset: %s\n", err); return 1; }
    bool have_held = argc > 3 && hta_oal_load(argv[3], &held, err, sizeof(err)) && !strcmp(held.kind, "weapon");
    printf("%s '%s' (%s): %u models, %u sounds\n", a.kind, a.name, a.display, a.model_count, a.sound_count);
    for (uint32_t i = 0; i < a.model_count; i++) {
        const hta_oal_model *m = &a.models[i];
        printf("  model %u: %u verts, %u tris, %u bones, %u attachments, clips:", i, m->mesh.vertex_count,
               m->mesh.index_count / 3, m->bone_count, m->att_count);
        for (uint32_t c = 0; c < m->clip_count; c++)
            printf(" %s(%u@%.0f)", m->clips[c].role, m->clips[c].frames, m->clips[c].fps);
        printf("\n");
    }
    const unsigned W = getenv("OAL_PREVIEW") ? 1040u : 640u, H = 480;
    hta_gfx *gfx = hta_gfx_create_offscreen(W, H, err, sizeof(err));
    if (!gfx) { fprintf(stderr, "renderer: %s\n", err); return 1; }
    hta_bsp_mesh ground;
    floor_mesh(&ground);
    hta_gfx_mesh *gfloor = hta_gfx_mesh_upload(gfx, &ground, err, sizeof(err));
    hta_scene scene = { .light_dir = { 0.35f, 0.4f, 0.85f }, .light_color = { 1, 1, 1 },
                        .ambient = { 0.6f, 0.6f, 0.6f }, .clear = { 0.1f, 0.15f, 0.23f } };
    unsigned char *px = malloc((size_t)W * H * 4);
    int shots = 0, ok = 1;
    static float world[HTA_OAL_MAX_BONES][12];
    char path[1024];

    if (!strcmp(a.kind, "character") && getenv("OAL_PREVIEW")) {
        /* The class screen's framing: OAL_PREVIEW=yaw_degrees. */
        const hta_oal_model *m = &a.models[0];
        hta_gfx_mesh *gm = hta_gfx_mesh_upload_dynamic_world(gfx, &m->mesh, err, sizeof(err));
        hta_gfx_mesh *gw = have_held ? hta_gfx_mesh_upload(gfx, &held.models[0].mesh, err, sizeof(err)) : NULL;
        hta_vertex *posed = malloc(m->mesh.vertex_count * sizeof(hta_vertex));
        float hi = 0.0f;
        for (uint32_t v = 0; v < m->mesh.vertex_count; v++) if (m->mesh.vertices[v].pos[2] > hi) hi = m->mesh.vertices[v].pos[2];
        float root[12];
        hta_camera cam;
        hta_preview_frame(hi, (float)W / H, strtof(getenv("OAL_PREVIEW"), NULL) / 57.29578f, root, &cam);
        int32_t idle = hta_oal_clip_find(m, "idle");
        hta_oal_pose(m, idle, 0.3f, world);
        hta_oal_skin(m, (const float (*)[12])world, root, posed);
        hta_gfx_dynamic dyn = { .mesh = gm, .vertices = posed, .vertex_count = m->mesh.vertex_count, .lit = true };
        hta_gfx_instance inst; uint32_t ni = 0; float hand[12];
        if (gw && hta_imported_hold_matrix(m, (const float (*)[12])world, root, &held.models[0], hand)) {
            inst.mesh = gw; hta_oal_to_mat4(hand, inst.model);
            inst.first_submesh = inst.submesh_count = 0; inst.lit = true; ni = 1;
        }
        hta_gfx_set_instances(gfx, &inst, ni);
        hta_scene ps = scene; ps.clear[0] = 0.06f; ps.clear[1] = 0.05f; ps.clear[2] = 0.05f;
        if (hta_gfx_draw(gfx, &cam, &ps, NULL, NULL, NULL, &dyn, 1, NULL, NULL) &&
            hta_gfx_readback(gfx, px, (size_t)W * H * 4)) {
            snprintf(path, sizeof(path), "%s_preview.ppm", argv[2]);
            ppm(path, px, W, H); shots++;
            printf("  preview: height %.2f wu\n", hi);
        }
        free(posed);
        hta_gfx_mesh_free(gfx, gm);
        if (gw) hta_gfx_mesh_free(gfx, gw);
    } else if (!strcmp(a.kind, "character")) {
        const hta_oal_model *m = &a.models[0];
        hta_gfx_mesh *gm = hta_gfx_mesh_upload_dynamic_world(gfx, &m->mesh, err, sizeof(err));
        hta_gfx_mesh *gw = have_held ? hta_gfx_mesh_upload(gfx, &held.models[0].mesh, err, sizeof(err)) : NULL;
        hta_vertex *posed = malloc(m->mesh.vertex_count * sizeof(hta_vertex));
        const float root[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
        for (int32_t c = -1; c < (int32_t)m->clip_count; c++) {
            float t = c >= 0 ? 0.5f * hta_oal_clip_length(m, c) : 0.0f;
            hta_oal_pose(m, c, t, world);
            hta_oal_skin(m, (const float (*)[12])world, root, posed);
            float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
            for (uint32_t v = 0; v < m->mesh.vertex_count; v++)
                for (int k = 0; k < 3; k++) {
                    if (posed[v].pos[k] < lo[k]) lo[k] = posed[v].pos[k];
                    if (posed[v].pos[k] > hi[k]) hi[k] = posed[v].pos[k];
                }
            hta_camera cam; hta_camera_init(&cam); cam.aspect = (float)W / H; cam.znear = 0.02f; cam.zfar = 50;
            cam.pos[0] = 0.75f; cam.pos[1] = 0.55f; cam.pos[2] = 0.45f;
            cam.yaw = atan2f(-cam.pos[1], -cam.pos[0]); cam.pitch = -0.12f;
            hta_gfx_dynamic dyn = { .mesh = gm, .vertices = posed, .vertex_count = m->mesh.vertex_count, .lit = true };
            hta_gfx_instance inst;
            uint32_t ni = 0;
            float hand[12];
            if (gw && hta_imported_hold_matrix(m, (const float (*)[12])world, root,
                                               &held.models[0], hand)) {
                inst.mesh = gw; hta_oal_to_mat4(hand, inst.model);
                inst.first_submesh = inst.submesh_count = 0; inst.lit = true; ni = 1;
            }
            hta_gfx_set_instances(gfx, &inst, ni);
            if (!hta_gfx_draw(gfx, &cam, &scene, gfloor, NULL, NULL, &dyn, 1, NULL, NULL) ||
                !hta_gfx_readback(gfx, px, (size_t)W * H * 4)) { ok = 0; break; }
            snprintf(path, sizeof(path), "%s_%s.ppm", argv[2], c >= 0 ? m->clips[c].role : "bind");
            ppm(path, px, W, H);
            printf("  %s: bounds z %.2f..%.2f (height %.2f wu), x %.2f..%.2f%s\n", c >= 0 ? m->clips[c].role : "bind",
                   lo[2], hi[2], hi[2] - lo[2], lo[0], hi[0], ni ? ", holding" : "");
            shots++;
        }
        free(posed);
        hta_gfx_mesh_free(gfx, gm);
        if (gw) hta_gfx_mesh_free(gfx, gw);
    } else {
        const hta_oal_model *vm = &a.models[1];
        hta_gfx_mesh *gv = hta_gfx_mesh_upload_dynamic(gfx, &vm->mesh, err, sizeof(err));
        hta_vertex *posed = malloc(vm->mesh.vertex_count * sizeof(hta_vertex));
        const float ident[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
        for (int32_t c = 0; c < (int32_t)vm->clip_count; c++) {
            hta_oal_pose(vm, c, 0.4f * hta_oal_clip_length(vm, c), world);
            hta_oal_skin(vm, (const float (*)[12])world, ident, posed);
            hta_camera cam; hta_camera_init(&cam); cam.aspect = (float)W / H; cam.znear = 0.005f; cam.zfar = 50;
            cam.pos[0] = -1.5f; cam.pos[2] = 0.6f; cam.yaw = 0; cam.pitch = 0;
            hta_gfx_viewmodel v = { gv, posed, vm->mesh.vertex_count, { 0, 0, 0 } };
            hta_gfx_set_instances(gfx, NULL, 0);
            if (!hta_gfx_draw(gfx, &cam, &scene, gfloor, NULL, NULL, NULL, 0, &v, NULL) ||
                !hta_gfx_readback(gfx, px, (size_t)W * H * 4)) { ok = 0; break; }
            snprintf(path, sizeof(path), "%s_view_%s.ppm", argv[2], vm->clips[c].role);
            ppm(path, px, W, H);
            printf("  view %s drawn\n", vm->clips[c].role);
            shots++;
        }
        free(posed);
        hta_gfx_mesh_free(gfx, gv);
    }
    printf("%d images\n", shots);
    free(px);
    hta_gfx_mesh_free(gfx, gfloor);
    hta_gfx_destroy(gfx);
    hta_oal_free(&a);
    if (have_held) hta_oal_free(&held);
    return ok && shots ? 0 : 1;
}
