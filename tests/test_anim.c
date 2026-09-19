/* First-person animation graph + skinned viewmodel. Needs a real .map. */
#include "asset/anim.h"
#include "asset/cache.h"
#include "asset/model.h"
#include "asset/weapon.h"
#include "engine/viewmodel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    uint8_t *b = malloc((size_t)sz);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f); *n = (size_t)sz; return b;
}

int main(int argc, char **argv)
{
    printf("first-person animation + skinning\n\n");
    if (argc < 2) { printf("  skip: no map path (pass bloodgulch.map)\n"); return 0; }

    size_t sz = 0;
    uint8_t *data = slurp(argv[1], &sz);
    CHECK(data != NULL, "map reads");
    if (!data) return 1;

    hta_cache c;
    char err[HTA_ERRLEN];
    CHECK(hta_cache_open(&c, data, sz, err, sizeof(err)), "cache opens");

    hta_weapon_def w;
    CHECK(hta_weapon_load_default(&c, NULL, &w, NULL, err, sizeof(err)), "weapon loads");
    printf("  %s\n", w.path);
    printf("  fp_model=0x%08X fp_anim=0x%08X proj=0x%08X firing_fx=0x%08X pickup=0x%08X\n",
           w.fp_model_id, w.fp_anim_id, w.projectile_id, w.firing_fx_id, w.pickup_snd_id);
    printf("  mag: %d loaded / %d reserve / %d initial, reload %.2fs\n",
           w.rounds_loaded_max, w.rounds_reserve_max, w.rounds_initial, w.reload_time);

    CHECK(w.fp_anim_id != 0, "weapon has first-person animations");
    CHECK(w.projectile_id != 0, "trigger names a projectile");
    CHECK(w.firing_fx_id != 0, "trigger names a firing effect");
    CHECK(w.rounds_loaded_max == 60, "AR magazine holds 60");
    CHECK(fabsf(w.reload_time - 3.4f) < 0.01f, "AR reload is 3.4s");
    /* The tag really does leave this at zero: the hold is animated, not offset. */
    CHECK(w.fp_offset[0] == 0.0f && w.fp_offset[1] == 0.0f && w.fp_offset[2] == 0.0f,
          "trigger first-person offset is (0,0,0), not a hold position");

    /* ---- the animation graph ---- */
    hta_anim_graph g;
    CHECK(hta_anim_load(&g, &c, w.fp_anim_id, err, sizeof(err)), "antr loads");
    printf("  graph '%s': %u nodes, %u animations, %u sounds\n",
           g.path, g.node_count, g.anim_count, g.sound_count);
    CHECK(g.node_count == 42, "AR graph has 42 nodes (37 hand + 5 gun)");
    CHECK(g.anim_count >= 10, "graph has the full FP clip set");
    CHECK(g.sound_count == 3, "graph references 3 sounds (reload, melee, ready)");

    int32_t gun = hta_anim_node_index(&g, "frame gun");
    int32_t rw  = hta_anim_node_index(&g, "frame r wriste");
    int32_t lw  = hta_anim_node_index(&g, "frame l wriste");
    CHECK(gun >= 0 && rw >= 0 && lw >= 0, "gun and both wrists are in the graph");
    /* This parenting is the whole reason the gun is right-handed. */
    CHECK(gun >= 0 && g.nodes[gun].parent == rw, "`frame gun` hangs off `frame r wriste`");

    for (int i = 0; i < HTA_VM_STATE_COUNT; i++) { (void)i; }
    CHECK(hta_anim_find(&g, "idle")   >= 0, "graph has an idle clip");
    CHECK(hta_anim_find(&g, "firing") >= 0, "graph has a firing clip");
    CHECK(hta_anim_find(&g, "ready")  >= 0, "graph has a ready clip");
    CHECK(hta_anim_find(&g, "reload") >= 0, "graph has a reload clip");
    CHECK(hta_anim_find(&g, "melee")  >= 0, "graph has a melee clip");

    /* default_size + frame_size == node_count * 24 for every uncompressed clip.
     * hta_anim_load refuses the tag otherwise, so reaching here proves it, but
     * assert it explicitly: it is what pins the flag-field order. */
    int sized = 1, uncompressed = 0;
    for (uint32_t i = 0; i < g.anim_count; i++) {
        const hta_animation *a = &g.anims[i];
        if (a->flags & HTA_ANIM_FLAG_COMPRESSED) continue;
        uncompressed++;
        if (a->default_size &&
            a->default_size + a->frame_size != a->node_count * HTA_ANIM_NODE_STRIDE)
            sized = 0;
    }
    CHECK(uncompressed >= 10, "clips are uncompressed");
    CHECK(sized, "default + frame size == nodes * 24 for every clip");

    /* Sampling is deterministic and the skeleton stays sane. */
    int32_t idle = hta_anim_find(&g, "idle");
    hta_transform la[HTA_ANIM_MAX_NODES], lb[HTA_ANIM_MAX_NODES], wr[HTA_ANIM_MAX_NODES];
    CHECK(hta_anim_sample(&g, (uint32_t)idle, 0.0f, la), "idle samples");
    CHECK(hta_anim_sample(&g, (uint32_t)idle, 0.0f, lb), "idle resamples");
    int same = 1;
    for (uint32_t i = 0; i < g.node_count; i++)
        if (memcmp(&la[i], &lb[i], sizeof(hta_transform)) != 0) same = 0;
    CHECK(same, "sampling the same frame twice is identical");

    int unit = 1, scaled = 1;
    for (uint32_t i = 0; i < g.node_count; i++) {
        float n = sqrtf(la[i].q[0]*la[i].q[0] + la[i].q[1]*la[i].q[1] +
                        la[i].q[2]*la[i].q[2] + la[i].q[3]*la[i].q[3]);
        if (fabsf(n - 1.0f) > 1e-3f) unit = 0;
        if (fabsf(la[i].s - 1.0f) > 1e-3f) scaled = 0;
    }
    CHECK(unit, "every sampled rotation is a unit quaternion");
    CHECK(scaled, "FP nodes are unscaled");

    hta_anim_world(&g, la, wr);
    /* The right wrist must end up right of the left one: +Y is left in Halo. */
    printf("  idle f0: r wriste y=%+.4f   l wriste y=%+.4f   gun y=%+.4f\n",
           wr[rw].t[1], wr[lw].t[1], wr[gun].t[1]);
    CHECK(wr[rw].t[1] < wr[lw].t[1], "right wrist is right of the left wrist");

    /* Interpolation stays between its endpoints. */
    hta_transform mid[HTA_ANIM_MAX_NODES], nxt[HTA_ANIM_MAX_NODES];
    hta_anim_sample(&g, (uint32_t)idle, 1.0f, nxt);
    hta_anim_sample(&g, (uint32_t)idle, 0.5f, mid);
    int between = 1;
    for (uint32_t i = 0; i < g.node_count; i++)
        for (int k = 0; k < 3; k++) {
            float lo = la[i].t[k] < nxt[i].t[k] ? la[i].t[k] : nxt[i].t[k];
            float hi = la[i].t[k] > nxt[i].t[k] ? la[i].t[k] : nxt[i].t[k];
            if (mid[i].t[k] < lo - 1e-5f || mid[i].t[k] > hi + 1e-5f) between = 0;
        }
    CHECK(between, "fractional frames interpolate between their endpoints");

    /* ---- transform algebra ---- */
    hta_transform a1 = { {0.2f, -0.3f, 0.5f, 0.78f}, {0.1f, -0.2f, 0.3f}, 1.0f };
    hta_transform inv, rt;
    hta_xf_inverse(&inv, &a1);
    hta_xf_mul(&rt, &a1, &inv);
    float v[3] = {0.3f, -0.7f, 0.2f}, out[3];
    hta_xf_point(out, &rt, v);
    CHECK(fabsf(out[0]-v[0]) < 1e-3f && fabsf(out[1]-v[1]) < 1e-3f &&
          fabsf(out[2]-v[2]) < 1e-3f, "x . x^-1 is the identity");

    /* ---- the skinned viewmodel ---- */
    hta_viewmodel vm;
    /* The muzzle flash needs its bitmap, which lives in bitmaps.map beside
     * the cache. Without it the geometry still loads, just unlit and
     * flashless -- so the flash checks below are skipped, not failed. */
    char bmp[1024];
    snprintf(bmp, sizeof(bmp), "%s", argv[1]);
    char *bslash = strrchr(bmp, '/');
    if (bslash) snprintf(bslash + 1, sizeof(bmp) - (size_t)(bslash + 1 - bmp), "bitmaps.map");
    else snprintf(bmp, sizeof(bmp), "bitmaps.map");
    size_t bsz = 0;
    uint8_t *bdata = slurp(bmp, &bsz);
    hta_resource_map bm;
    memset(&bm, 0, sizeof(bm));
    int have_bitmaps = bdata && hta_resource_open(&bm, bdata, bsz, err, sizeof(err));
    if (!have_bitmaps) printf("  note: no bitmaps.map beside the cache\n");

    CHECK(hta_viewmodel_load(&vm, &c, have_bitmaps ? &bm : NULL, &w, err, sizeof(err)),
          "viewmodel loads");
    if (!vm.loaded) { printf("  (%s)\n", err); printf("\n%d checks, %d failures\n", checks, failures); return failures != 0; }
    printf("  viewmodel: %u verts (%u hands + %u gun), %u submeshes\n",
           vm.mesh.vertex_count, vm.hands_verts, vm.gun_verts, vm.mesh.submesh_count);
    CHECK(vm.hands_verts > 0, "hands mesh loaded from globals");
    CHECK(vm.gun_verts > 0, "gun mesh loaded from the weapon");

    /* Every SKINNED vertex must be bound, or the mesh would collapse to the
     * origin. The muzzle-flash quad at the end is placed by hand rather than
     * skinned, so it is deliberately unbound and not part of this. */
    uint32_t skinned = vm.have_flash ? vm.flash_first_vertex : vm.mesh.vertex_count;
    uint32_t unbound = 0, resty = 0;
    for (uint32_t i = 0; i < skinned; i++)
        if (vm.skin[i].weight[0] + vm.skin[i].weight[1] < 0.99f) unbound++;
    for (uint32_t i = 0; i < vm.graph.node_count; i++) if (vm.have_rest[i]) resty++;
    CHECK(unbound == 0, "every vertex has a full-weight binding");
    CHECK(resty == vm.graph.node_count,
          "hands and gun together cover every graph node");

    /* Posing must actually move geometry off the bind pose. */
    float moved = 0.0f;
    for (uint32_t i = 0; i < vm.mesh.vertex_count; i++) {
        float d = 0.0f;
        for (int k = 0; k < 3; k++) {
            float e = vm.posed[i].pos[k] - vm.mesh.vertices[i].pos[k];
            d += e*e;
        }
        if (sqrtf(d) > moved) moved = sqrtf(d);
    }
    CHECK(moved > 0.01f, "skinning displaces the bind pose");

    /* Nothing may fly off: the whole viewmodel lives within arm's reach. */
    float lo[3] = {1e9f,1e9f,1e9f}, hi[3] = {-1e9f,-1e9f,-1e9f};
    for (uint32_t i = 0; i < vm.mesh.vertex_count; i++)
        for (int k = 0; k < 3; k++) {
            if (vm.posed[i].pos[k] < lo[k]) lo[k] = vm.posed[i].pos[k];
            if (vm.posed[i].pos[k] > hi[k]) hi[k] = vm.posed[i].pos[k];
        }
    printf("  posed bounds  X %+.3f..%+.3f  Y %+.3f..%+.3f  Z %+.3f..%+.3f\n",
           lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    int sane = 1;
    for (int k = 0; k < 3; k++) if (lo[k] < -1.0f || hi[k] > 1.0f) sane = 0;
    CHECK(sane, "posed viewmodel stays within 1 world unit of the eye");

    /* The whole point of the slice: the gun is in the RIGHT hand, pointing
     * forward. Both fall out of the skeleton -- `frame gun` hangs off the
     * right wrist -- but only if the rotation sense is right. With the
     * quaternions taken as stored (not conjugated) the barrel comes out
     * pointing up and to the left, so these two checks are what pin it. */
    float glo = 1e9f, ghi = -1e9f, gx = -1e9f;
    for (uint32_t i = vm.hands_verts; i < vm.mesh.vertex_count; i++) {
        if (vm.posed[i].pos[1] < glo) glo = vm.posed[i].pos[1];
        if (vm.posed[i].pos[1] > ghi) ghi = vm.posed[i].pos[1];
        if (vm.posed[i].pos[0] > gx)  gx  = vm.posed[i].pos[0];
    }
    printf("  gun Y span %+.4f..%+.4f (centre %+.4f), reaches X %+.4f forward\n",
           glo, ghi, (glo+ghi)*0.5f, gx);
    CHECK(ghi < 0.0f, "the whole gun sits right of centre (+Y is left)");
    CHECK(gx > 0.15f, "the gun points forward, down the view axis");

    /* The barrel is the gun model's local +X; animated, it must stay forward. */
    hta_transform lp[HTA_ANIM_MAX_NODES], wp[HTA_ANIM_MAX_NODES];
    hta_anim_sample(&g, (uint32_t)idle, 0.0f, lp);
    hta_anim_world(&g, lp, wp);
    float axis[3] = {1.0f, 0.0f, 0.0f}, dir[3];
    hta_xf_vector(dir, &wp[gun], axis);
    printf("  barrel axis -> (%+.3f, %+.3f, %+.3f)\n", dir[0], dir[1], dir[2]);
    CHECK(dir[0] > 0.95f, "barrel points down +X (forward), not up or sideways");

    /* Hands straddle the gun: the left one supports the foregrip. */
    float hlo = 1e9f, hhi = -1e9f;
    for (uint32_t i = 0; i < vm.hands_verts; i++) {
        if (vm.posed[i].pos[1] < hlo) hlo = vm.posed[i].pos[1];
        if (vm.posed[i].pos[1] > hhi) hhi = vm.posed[i].pos[1];
    }
    CHECK(hhi > 0.0f && hlo < 0.0f, "arms reach both sides of centre");

    /* Nothing pokes behind the eye far enough to clip through the near plane. */
    int behind = 0;
    for (uint32_t i = 0; i < vm.mesh.vertex_count; i++)
        if (vm.posed[i].pos[0] < -0.30f) behind++;
    CHECK(behind == 0, "no viewmodel geometry sits far behind the eye");

    /* Playback: idle loops forever and stays in range; one-shots fall back. */
    hta_viewmodel_play(&vm, HTA_VM_IDLE);
    int in_range = 1;
    for (int i = 0; i < 600; i++) {
        hta_viewmodel_update(&vm, 1.0f/60.0f);
        const hta_animation *a = &vm.graph.anims[vm.clip[vm.state]];
        if (vm.frame < 0.0f || vm.frame > (float)a->frame_count) in_range = 0;
    }
    CHECK(in_range, "idle loops without running off the end");
    CHECK(vm.state == HTA_VM_IDLE, "idle stays idle");

    hta_viewmodel_play(&vm, HTA_VM_FIRE);
    CHECK(vm.state == HTA_VM_FIRE && vm.frame == 0.0f, "firing restarts at frame 0");
    const hta_animation *fa = &vm.graph.anims[vm.clip[HTA_VM_FIRE]];
    for (int i = 0; i < (int)(fa->frame_count * 3); i++) hta_viewmodel_update(&vm, 1.0f/30.0f);
    CHECK(vm.state == HTA_VM_IDLE, "firing returns to idle when it ends");

    /* --- melee --------------------------------------------------------- */
    printf("\n[melee]\n");
    {
        CHECK(vm.clip[HTA_VM_MELEE] >= 0, "the graph has a melee clip");
        if (vm.clip[HTA_VM_MELEE] >= 0) {
            const hta_animation *ma = &vm.graph.anims[vm.clip[HTA_VM_MELEE]];
            printf("    '%s', %u frames (%.2f s), sound index %d frame %d\n",
                   ma->name, ma->frame_count,
                   (float)ma->frame_count / HTA_ANIM_FPS,
                   (int)ma->sound_index, (int)ma->sound_frame);
            CHECK(ma->frame_count > 10 && ma->frame_count < 120,
                  "a swing is a second or so, not a frame and not a minute");

            /* It must actually move the weapon: compare the pose partway
             * through against idle. */
            hta_viewmodel_play(&vm, HTA_VM_IDLE);
            hta_viewmodel_update(&vm, 0.0f);
            float idle0[3] = { vm.posed[vm.hands_verts].pos[0],
                               vm.posed[vm.hands_verts].pos[1],
                               vm.posed[vm.hands_verts].pos[2] };

            hta_viewmodel_play(&vm, HTA_VM_MELEE);
            CHECK(vm.state == HTA_VM_MELEE && vm.frame == 0.0f,
                  "melee restarts at frame 0");
            uint32_t cues = 0;
            for (int i = 0; i < (int)ma->frame_count; i++) {
                hta_viewmodel_update(&vm, 1.0f / HTA_ANIM_FPS);
                if (vm.sound_cue) { cues++; vm.sound_cue = 0; }
            }
            /* Halo puts the swing's own sound on a frame of the clip. */
            printf("    sound cues during the swing: %u\n", cues);
            CHECK(cues >= 1, "the swing fires its tagged sound");

            hta_viewmodel_play(&vm, HTA_VM_MELEE);
            for (int i = 0; i < (int)(ma->frame_count / 2); i++)
                hta_viewmodel_update(&vm, 1.0f / HTA_ANIM_FPS);
            float moved = 0.0f;
            for (int k = 0; k < 3; k++) {
                float dd = vm.posed[vm.hands_verts].pos[k] - idle0[k];
                moved += dd * dd;
            }
            printf("    hands move %.3f wu from idle mid-swing\n", sqrtf(moved));
            CHECK(sqrtf(moved) > 0.02f, "the weapon actually swings");

            /* And it has to put itself away again. */
            for (int i = 0; i < (int)ma->frame_count * 2; i++)
                hta_viewmodel_update(&vm, 1.0f / HTA_ANIM_FPS);
            CHECK(vm.state == HTA_VM_IDLE, "and returns to idle when it ends");
        }
    }

    /* --- muzzle flash ------------------------------------------------- */
    printf("\n[muzzle flash]\n");
    if (!have_bitmaps) {
        printf("  skip: needs bitmaps.map for the flash sprite\n");
    } else {
    CHECK(vm.have_flash, "the AR's firing effect yields a first-person flash");
    if (vm.have_flash) {
        /* It must hang off the gun, not off a hand: the marker names a node
         * in the gun's own model, resolved into the merged skeleton by name. */
        CHECK(vm.flash_node >= 0 && (uint32_t)vm.flash_node < vm.graph.node_count,
              "its node is in the merged skeleton");
        printf("    node %d '%s', offset (%.3f %.3f %.3f), radius %.3f, %.0f ms,"
               " %u sprite(s)\n",
               (int)vm.flash_node, vm.graph.nodes[vm.flash_node].name,
               vm.flash_offset[0], vm.flash_offset[1], vm.flash_offset[2],
               vm.flash_radius, vm.flash_life * 1000.0f, vm.flash_sprite_count);
        CHECK(strcmp(vm.graph.nodes[vm.flash_node].name, "frame gun") == 0,
              "and it is the gun node");
        /* The barrel runs down local +X; the marker must be out along it. */
        CHECK(vm.flash_offset[0] > 0.15f, "the marker is out at the muzzle, not at the grip");
        CHECK(vm.flash_life > 0.01f && vm.flash_life < 0.5f,
              "a flash lasts a few frames, not a second");
        CHECK(vm.flash_radius > 0.01f && vm.flash_radius < 1.0f,
              "and is a sane size");
        CHECK(vm.flash_sprite_count > 1, "several sprite variants were found");

        uint32_t fv = vm.flash_first_vertex;
        CHECK(fv + 4 == vm.mesh.vertex_count, "its quad is the last four vertices");
        CHECK(vm.mesh.submeshes[vm.mesh.submesh_count-1].draw_mode == HTA_DRAW_ADD,
              "drawn additively, as the particle tag says");

        /* Unlit, the quad must have zero area so it rasterises nothing. */
        vm.flash_timer = 0.0f;
        hta_viewmodel_update(&vm, 0.0f);
        int collapsed = 1;
        for (int i = 1; i < 4; i++)
            for (int j = 0; j < 3; j++)
                if (vm.posed[fv+i].pos[j] != vm.posed[fv].pos[j]) collapsed = 0;
        CHECK(collapsed, "an unlit flash collapses to a point");

        /* Lit, it must open up around the muzzle. */
        hta_viewmodel_flash(&vm);
        CHECK(vm.flash_timer > 0.0f, "firing lights it");
        hta_viewmodel_update(&vm, 0.0f);
        float w = 0.0f;
        for (int j = 0; j < 3; j++) {
            float d = vm.posed[fv+2].pos[j] - vm.posed[fv].pos[j];
            w += d * d;
        }
        w = sqrtf(w);
        CHECK(w > vm.flash_radius, "a lit flash spans at least its radius");

        /* The sprite rectangle must be a sub-rectangle of the sheet, or the
         * quad shows every flash on it at once. */
        int sub = 1;
        for (int i = 0; i < 4; i++) {
            float u = vm.posed[fv+i].uv[0], v = vm.posed[fv+i].uv[1];
            if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) sub = 0;
        }
        CHECK(sub, "its UVs stay inside the sheet");
        float du = fabsf(vm.posed[fv+1].uv[0] - vm.posed[fv].uv[0]);
        float dv = fabsf(vm.posed[fv+3].uv[1] - vm.posed[fv].uv[1]);
        CHECK(du > 0.0f && du < 0.95f && dv > 0.0f && dv < 0.95f,
              "and cover one sprite, not the whole sheet");

        /* It must go out on its own. */
        hta_viewmodel_update(&vm, vm.flash_life + 0.01f);
        CHECK(vm.flash_timer == 0.0f, "and it burns out by itself");
    }
    }

    /* A triangle STRIP part stores a triangle count, not an index count:
     * N triangles is N + 2 indices. Reading only N drops the last two
     * triangles of every part -- invisible on a 1682-triangle gun body, and
     * fatal to a 2-triangle one. The assault rifle's ammo digits are exactly
     * that: 4 vertices, 2 triangles, 6 indices. If any submesh here is a
     * single quad, the off-by-two is not back. */
    printf("\n[triangle strips]\n");
    {
        uint32_t quads = 0, smallest = 0xFFFFFFFFu;
        for (uint32_t i = 0; i < vm.mesh.submesh_count; i++) {
            uint32_t n = vm.mesh.submeshes[i].index_count;
            if (!n) continue;
            if (n < smallest) smallest = n;
            if (n == 6) quads++;
        }
        printf("    smallest submesh is %u indices; %u single-quad part(s)\n",
               smallest, quads);
        CHECK(quads >= 2, "two-triangle strip parts survive the strip decoder");
    }

    /* --- the gun's own round counter --------------------------------- */
    printf("\n[on-gun round counter]\n");
    if (!have_bitmaps) {
        printf("  skip: needs bitmaps.map for the digit frames\n");
    } else {
        CHECK(vm.have_counter, "the assault rifle has a round counter on it");
        if (vm.have_counter) {
            printf("    %u digits, %u frames, submeshes %u and %u\n",
                   vm.counter_digits, vm.counter_frames,
                   vm.counter_submesh[0], vm.counter_submesh[1]);
            CHECK(vm.counter_digits == 2, "two digits, for a 60-round magazine");
            CHECK(vm.counter_frames == 10, "ten frames, one per digit");

            /* Both quads must hang off the display node, not the body. */
            int32_t disp = hta_anim_node_index(&vm.graph, "frame display");
            CHECK(disp >= 0, "the model has a 'frame display' node");
            int on_display = 1;
            for (uint32_t d = 0; d < 2; d++)
                for (uint32_t k = 0; k < 4; k++) {
                    uint32_t v = vm.counter_vertex[d][k];
                    if (vm.skin[v].node[0] != (uint16_t)disp) on_display = 0;
                }
            CHECK(on_display, "both digit quads are skinned to it");

            /* The more significant digit sits further +Y, which is left. */
            float y0 = 0.0f, y1 = 0.0f;
            for (uint32_t k = 0; k < 4; k++) {
                y0 += vm.mesh.vertices[vm.counter_vertex[0][k]].pos[1];
                y1 += vm.mesh.vertices[vm.counter_vertex[1][k]].pos[1];
            }
            CHECK(y0 > y1, "the tens digit is the left-hand quad");

            CHECK(vm.mesh.submeshes[vm.counter_submesh[0]].draw_mode == HTA_DRAW_ADD,
                  "the readout glows, as its shader declares");
            CHECK(vm.mesh.submeshes[vm.counter_submesh[0]].albedo_tex ==
                  vm.mesh.submeshes[vm.counter_submesh[1]].albedo_tex,
                  "both digits sample the same atlas");

            /* Each digit must land in its own tenth of the atlas. */
            struct { uint32_t value; uint32_t tens, units; } cases[] = {
                { 60, 6, 0 }, { 47, 4, 7 }, { 8, 0, 8 }, { 0, 0, 0 }, { 99, 9, 9 }
            };
            int placed = 1;
            for (uint32_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
                hta_viewmodel_set_counter(&vm, cases[i].value);
                hta_viewmodel_update(&vm, 0.0f);
                uint32_t want[2] = { cases[i].tens, cases[i].units };
                for (uint32_t d = 0; d < 2; d++)
                    for (uint32_t k = 0; k < 4; k++) {
                        float u = vm.posed[vm.counter_vertex[d][k]].uv[0];
                        uint32_t slot = (uint32_t)(u * 10.0f);
                        if (slot > 9u) slot = 9u;
                        if (slot != want[d]) placed = 0;
                    }
            }
            CHECK(placed, "every digit 0-9 lands in its own slot of the atlas");

            /* A value past the display's two digits must not read off the
             * end of the atlas. */
            hta_viewmodel_set_counter(&vm, 1234u);
            hta_viewmodel_update(&vm, 0.0f);
            int inside = 1;
            for (uint32_t d = 0; d < 2; d++)
                for (uint32_t k = 0; k < 4; k++) {
                    float u = vm.posed[vm.counter_vertex[d][k]].uv[0];
                    if (u < 0.0f || u > 1.0f) inside = 0;
                }
            CHECK(inside, "an over-large count still samples inside the atlas");
        }
    }

    hta_viewmodel_free(&vm);
    hta_anim_free(&g);
    free(data);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
