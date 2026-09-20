/* The playable roster: every weapon the map places must be usable.
 *
 * Needs the Trial's own bloodgulch.map -- there is nothing synthetic to
 * check here, the whole point is that the tags say what we think they say.
 */
#include "asset/cache.h"
#include "asset/weapon.h"
#include "engine/viewmodel.h"
#include "asset/anim.h"
#include "engine/ammo.h"
#include "asset/effect.h"
#include "asset/model.h"
#include "asset/bsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)sz);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f); *n = (size_t)sz; return b;
}

static const char *leaf(const char *path)
{
    const char *s = strrchr(path, '\\');
    return s ? s + 1 : path;
}

int main(int argc, char **argv)
{
    printf("weapons\n");
    if (argc < 2) {
        printf("  skip: pass bloodgulch.map\n");
        return 0;
    }

    size_t n = 0;
    uint8_t *data = slurp(argv[1], &n);
    if (!data) { printf("  FAIL: cannot read %s\n", argv[1]); return 1; }

    hta_cache c;
    char err[HTA_ERRLEN] = {0};
    if (!hta_cache_open(&c, data, n, err, sizeof(err))) {
        printf("  FAIL: %s\n", err); return 1;
    }

    /* bitmaps.map beside the cache, for the meshes' textures. */
    char bmp[512];
    snprintf(bmp, sizeof(bmp), "%s", argv[1]);
    char *bslash = strrchr(bmp, '/');
    if (bslash) snprintf(bslash + 1, sizeof(bmp) - (size_t)(bslash + 1 - bmp), "bitmaps.map");
    else snprintf(bmp, sizeof(bmp), "bitmaps.map");
    size_t bsz = 0;
    uint8_t *bdata = slurp(bmp, &bsz);
    hta_resource_map bm;
    int have_bitmaps = bdata && hta_resource_open(&bm, bdata, bsz, err, sizeof(err));

    uint32_t ids[32];
    uint32_t count = hta_weapon_list_playable(&c, ids, 32);
    printf("\n[roster]\n");
    printf("  %u playable weapon(s)\n", count);
    CHECK(count == 9, "nine weapons a player can actually carry");
    {
        /* `needler` and `mp_needler` are distinct tags that share a
         * first-person model, animation graph, HUD and magazine -- the same
         * gun twice in the swap order. */
        int clash = 0;
        for (uint32_t i = 0; i < count; i++) {
            hta_weapon_def a;
            if (!hta_weapon_load_id(&c, NULL, ids[i], &a, NULL, err, sizeof(err))) continue;
            for (uint32_t j = i + 1; j < count; j++) {
                hta_weapon_def b;
                if (!hta_weapon_load_id(&c, NULL, ids[j], &b, NULL, err, sizeof(err))) continue;
                if (a.fp_model_id == b.fp_model_id && a.fp_anim_id == b.fp_anim_id) {
                    printf("  FAIL: %s and %s are the same gun\n",
                           leaf(a.path), leaf(b.path));
                    clash++;
                }
            }
        }
        CHECK(!clash, "no weapon appears twice under two names");
    }
    {
        /* The fuel rod gun (`plasma_cannon`) is the one weapon whose
         * first-person model carries its own arms instead of wearing the
         * globals hands. Loading both drew two sets of arms AND let the
         * gun model overwrite the hands' bind pose, so it is kept out of
         * the roster entirely -- Halo never hands it to the player. */
        int self_contained = 0;
        for (uint32_t i = 0; i < count; i++) {
            hta_weapon_def w;
            if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
            if (hta_model_has_node(&c, w.fp_model_id, "frame l wriste")) {
                printf("  FAIL: %s brings its own arms\n", leaf(w.path));
                self_contained++;
            }
        }
        CHECK(!self_contained, "every weapon in the roster wears the globals hands");
    }

    printf("\n[every weapon animates]\n");
    /* The flamethrower's antr genuinely has no fire clip -- it is the one
     * weapon Bungie animated with a held pose and a particle jet instead.
     * Every other weapon must have one, and before the clip-name fallbacks
     * only the assault rifle did: it is alone in calling its clip "firing"
     * where the rest say "fire-1". */
    int no_fire = 0, zoomers = 0;
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, have_bitmaps ? &bm : NULL, ids[i], &w, NULL, err, sizeof(err))) {
            printf("  FAIL: %s\n", err); failures++; checks++; continue;
        }
        hta_viewmodel vm;
        if (!hta_viewmodel_load(&vm, &c, have_bitmaps ? &bm : NULL, &w,
                                err, sizeof(err))) {
            printf("  FAIL: no viewmodel for %s: %s\n", leaf(w.path), err);
            failures++; checks++; continue;
        }
        printf("  %-16s ready %c  idle %c  fire %c  reload %c  melee %c",
               leaf(w.path),
               vm.clip[HTA_VM_READY]  >= 0 ? 'y' : '-',
               vm.clip[HTA_VM_IDLE]   >= 0 ? 'y' : '-',
               vm.clip[HTA_VM_FIRE]   >= 0 ? 'y' : '-',
               vm.clip[HTA_VM_RELOAD] >= 0 ? 'y' : '-',
               vm.clip[HTA_VM_MELEE]  >= 0 ? 'y' : '-');
        if (w.zoom_levels > 0) {
            printf("   zoom %dx", w.zoom_levels);
            zoomers++;
        }
        printf("\n");

        if (vm.clip[HTA_VM_FIRE] < 0) {
            no_fire++;
            if (!strstr(w.path, "flamethrower")) {
                printf("  FAIL: %s has no fire clip\n", leaf(w.path));
                failures++;
            }
            checks++;
        } else { CHECK(1, "has a firing animation"); }

        CHECK(vm.clip[HTA_VM_IDLE] >= 0, "  and an idle");
        CHECK(vm.clip[HTA_VM_MELEE] >= 0, "  and a melee");
        hta_viewmodel_free(&vm);
    }
    CHECK(no_fire <= 1, "at most one weapon lacks a fire clip");

    printf("\n[zoom comes from the tag]\n");
    /* `zoom levels` at weap+986, `zoom magnification range` at +988. Only
     * three weapons in the Trial zoom, and only the sniper does it twice. */
    struct { const char *name; int levels; float lo, hi; } want[] = {
        { "pistol",          1, 2.0f, 2.0f },
        { "rocket launcher", 1, 2.0f, 2.0f },
        { "sniper rifle",    2, 2.0f, 8.0f },
    };
    int checked = 0;
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
        for (size_t k = 0; k < sizeof(want) / sizeof(want[0]); k++) {
            if (strcmp(leaf(w.path), want[k].name)) continue;
            printf("  %-16s %d level(s), %.1fx .. %.1fx\n",
                   leaf(w.path), w.zoom_levels, w.zoom_mag[0], w.zoom_mag[1]);
            CHECK(w.zoom_levels == want[k].levels, "the tagged zoom level count");
            CHECK(fabsf(w.zoom_mag[0] - want[k].lo) < 0.01f, "  low magnification");
            CHECK(fabsf(w.zoom_mag[1] - want[k].hi) < 0.01f, "  high magnification");
            checked++;
        }
        if (w.zoom_levels == 0) {
            /* A weapon that does not zoom must not report a magnification
             * we would then divide the field of view by. */
            CHECK(w.zoom_mag[0] == 0.0f || w.zoom_mag[0] == 1.0f,
                  "a non-zooming weapon has no magnification");
        }
    }
    CHECK(checked == 3, "all three zooming weapons are in the roster");
    CHECK(zoomers == 3, "and nothing else claims to zoom");

    printf("\n[detail maps on models]\n");
    {
        /* A submesh with no detail map must say ~0u, not 0. Zero is a
         * VALID texture index, and leaving it there had the renderer bind
         * each mesh's first texture as its own detail map and multiply it
         * in -- which quietly halved the brightness of every weapon and
         * every piece of scenery. The memset that clears a submesh does
         * not do this for you. */
        int weapons_seen = 0, with_detail = 0, bad = 0;
        for (uint32_t i = 0; i < count; i++) {
            hta_weapon_def w;
            if (!hta_weapon_load_id(&c, have_bitmaps ? &bm : NULL, ids[i], &w,
                                    NULL, err, sizeof(err))) continue;
            hta_viewmodel vm;
            if (!hta_viewmodel_load(&vm, &c, have_bitmaps ? &bm : NULL, &w,
                                    err, sizeof(err))) continue;
            weapons_seen++;
            for (uint32_t k = 0; k < vm.mesh.submesh_count; k++) {
                const hta_submesh *sm = &vm.mesh.submeshes[k];
                if (sm->detail_tex == ~0u) {
                    if (sm->detail_scale != 0.0f) bad++;   /* scale with no map */
                    continue;
                }
                with_detail++;
                if (sm->detail_scale <= 0.0f) bad++;       /* map with no scale */
                if (sm->detail_tex >= vm.mesh.texture_count) bad++;
            }
            hta_viewmodel_free(&vm);
        }
        printf("  %d submesh(es) across %d weapons carry a detail map\n",
               with_detail, weapons_seen);
        CHECK(!bad, "no submesh claims a detail map it does not have");

        /* And the mask that gates it. Nothing a player HOLDS uses one --
         * the cyborg's hands say "none" -- so this is checked on the
         * scenery and vehicles, where Blood Gulch uses the reflection
         * channel to keep detail off shiny panels. */
        {
            hta_bsp_mesh world;
            memset(&world, 0, sizeof(world));
            if (hta_bsp_load_first(&c, &world, err, sizeof(err))) {
                hta_bsp_load_textures(&c, have_bitmaps ? &bm : NULL, &world,
                                      err, sizeof(err));
                uint32_t bsp_only = world.submesh_count;
                hta_scenario_add_objects(&world, &c, have_bitmaps ? &bm : NULL,
                                         err, sizeof(err));
                uint32_t masked = 0, broken = 0;
                for (uint32_t k = 0; k < world.submesh_count; k++) {
                    const hta_submesh *sm = &world.submeshes[k];
                    if (!sm->detail_mask) continue;
                    masked++;
                    if (sm->multi_tex == ~0u ||
                        sm->multi_tex >= world.texture_count) broken++;
                    if (sm->detail_tex == ~0u) broken++;
                    if (sm->detail_mask > 8u) broken++;
                }
                printf("  %u masked submesh(es) among the placed objects\n", masked);
                CHECK(masked > 0, "the map's objects use detail masks");
                CHECK(!broken, "and every mask has a multipurpose map to read");
                /* The BSP's own surfaces are `senv` and mask differently. */
                uint32_t bsp_masked = 0;
                for (uint32_t k = 0; k < bsp_only; k++)
                    if (world.submeshes[k].detail_mask) bsp_masked++;
                CHECK(bsp_masked == 0, "and the BSP's own shaders use none");
                hta_bsp_free(&world);
            }
        }
        /* The cyborg's first-person hands have one, so every weapon does. */
        CHECK(with_detail >= weapons_seen,
              "the hands' detail map reaches every weapon");
    }

    printf("\n[the needler wears its magazine]\n");
    {
        /* Halo poses the needler's sixteen needle bones with an OVERLAY
         * clip, `first-person ammunition`: 21 frames for a 20-round
         * magazine, frame 0 full and frame 20 empty. It is the only Trial
         * weapon with one. */
        int found = 0;
        for (uint32_t i = 0; i < count; i++) {
            hta_weapon_def w;
            if (!hta_weapon_load_id(&c, have_bitmaps ? &bm : NULL, ids[i], &w,
                                    NULL, err, sizeof(err))) continue;
            hta_viewmodel vm;
            if (!hta_viewmodel_load(&vm, &c, have_bitmaps ? &bm : NULL, &w,
                                    err, sizeof(err))) continue;
            if (strstr(w.path, "needler")) {
                found = 1;
                CHECK(vm.clip_ammo >= 0, "the needler has an ammunition overlay");
                if (vm.clip_ammo >= 0) {
                    const hta_animation *a = &vm.graph.anims[vm.clip_ammo];
                    printf("    %u frames for a %d-round magazine\n",
                           a->frame_count, w.rounds_loaded_max);
                    CHECK(a->frame_count == 21, "21 frames");

                    hta_viewmodel_set_ammo(&vm, 1.0f);
                    CHECK(vm.ammo_frame == 20.0f, "a full magazine is the LAST frame");
                    hta_viewmodel_set_ammo(&vm, 0.0f);
                    CHECK(vm.ammo_frame == 0.0f, "an empty one is frame 0");
                    hta_viewmodel_set_ammo(&vm, 0.5f);
                    CHECK(vm.ammo_frame == 10.0f, "and half is halfway");
                    /* Out of range must not walk off the clip. */
                    hta_viewmodel_set_ammo(&vm, 2.0f);
                    CHECK(vm.ammo_frame == 20.0f, "over-full clamps");
                    hta_viewmodel_set_ammo(&vm, -1.0f);
                    CHECK(vm.ammo_frame == 0.0f, "and negative clamps");

                    /* The needles must actually move, and the arms must not:
                     * an overlay's unkeyframed nodes hold its own default
                     * pose, so composing all of them would flatten them. */
                    int needles = 0, arms = 0;
                    for (uint32_t k = 0; k < vm.graph.node_count; k++) {
                        if (!hta_anim_animates(&vm.graph, (uint32_t)vm.clip_ammo, k))
                            continue;
                        if (strstr(vm.graph.nodes[k].name, "needle")) needles++;
                        else if (strstr(vm.graph.nodes[k].name, "upperarm") ||
                                 strstr(vm.graph.nodes[k].name, "forearm") ||
                                 strstr(vm.graph.nodes[k].name, "wriste")) arms++;
                    }
                    printf("    overlay keyframes %d needle bones, %d arm bones\n",
                           needles, arms);
                    CHECK(needles == 16, "sixteen needles");
                    CHECK(arms == 0, "and no arm bones, which it must not move");

                    /* The clip POSES the needle bones; it does not offset
                     * them, and the FULL end is its last frame, not its
                     * first. Both are easy to get backwards -- at the full
                     * end every needle bone shares one transform, which
                     * reads as "collapsed" until you remember each needle
                     * is skinned against its own bind pose.
                     *
                     * So the check is on the posed geometry, and it is
                     * monotonic: needles leave the rack as it empties. */
                    {
                        /* Measured on the idle, which is how it is held. */
                        hta_viewmodel_play(&vm, HTA_VM_IDLE);

                        /* THE invariant: the idle already holds the complete
                         * needle rack, so a FULL magazine must leave the base
                         * pose alone. The clip is a delta away from its own
                         * LAST frame, which makes that true by construction.
                         * Referencing it to frame 0 instead splayed the
                         * needles as the magazine emptied. */
                        {
                            hta_transform a[HTA_ANIM_MAX_NODES];
                            hta_transform b[HTA_ANIM_MAX_NODES];
                            int32_t idle = vm.clip[HTA_VM_IDLE];
                            hta_anim_sample(&vm.graph, (uint32_t)idle, 0.0f, a);
                            memcpy(b, a, sizeof(hta_transform) * vm.graph.node_count);
                            hta_viewmodel_set_ammo(&vm, 1.0f);
                            hta_viewmodel_apply_ammo(&vm, b);
                            float drift = 0.0f;
                            for (uint32_t k = 0; k < vm.graph.node_count; k++)
                                for (int q = 0; q < 3; q++)
                                    drift += fabsf(a[k].t[q] - b[k].t[q]);
                            printf("    full-magazine drift %.6f\n", drift);
                            CHECK(drift < 1e-4f,
                                  "a full magazine leaves the idle's rack untouched");
                            memcpy(b, a, sizeof(hta_transform) * vm.graph.node_count);
                            hta_viewmodel_set_ammo(&vm, 0.0f);
                            hta_viewmodel_apply_ammo(&vm, b);
                            drift = 0.0f;
                            for (uint32_t k = 0; k < vm.graph.node_count; k++)
                                for (int q = 0; q < 3; q++)
                                    drift += fabsf(a[k].t[q] - b[k].t[q]);
                            CHECK(drift > 1e-3f, "and an empty one moves them");
                        }

                        uint32_t nv = vm.hands_verts + vm.gun_verts;
                        int prev = -1, falls = 0, steps = 0;
                        int at_full = 0, at_empty = 0;
                        for (int r = 20; r >= 0; r -= 2) {
                            hta_viewmodel_set_ammo(&vm, (float)r / 20.0f);
                            hta_viewmodel_update(&vm, 0.0f);
                            int up = 0;
                            for (uint32_t v = vm.hands_verts; v < nv; v++)
                                if (vm.posed[v].pos[2] > -0.02f) up++;
                            if (r == 20) at_full = up;
                            if (r == 0)  at_empty = up;
                            if (prev >= 0) { steps++; if (up <= prev) falls++; }
                            prev = up;
                        }
                        printf("    needles showing: %d full -> %d empty\n",
                               at_full, at_empty);
                        CHECK(at_full > at_empty,
                              "a full magazine shows more needles than an empty one");
                        CHECK(at_full - at_empty > 100,
                              "and by enough to see");
                        CHECK(falls == steps,
                              "and they only ever leave, never come back");
                    }

                    /* And the pose really differs between full and empty. */
                    uint32_t nv = vm.hands_verts + vm.gun_verts;
                    float *full = (float *)malloc((size_t)nv * 3 * sizeof(float));
                    hta_viewmodel_set_ammo(&vm, 1.0f);
                    hta_viewmodel_update(&vm, 0.0f);
                    for (uint32_t v = 0; v < nv; v++)
                        for (int k = 0; k < 3; k++)
                            full[v * 3 + k] = vm.posed[v].pos[k];

                    hta_viewmodel_set_ammo(&vm, 0.0f);
                    hta_viewmodel_update(&vm, 0.0f);
                    float worst = 0.0f;
                    uint32_t moved = 0;
                    for (uint32_t v = 0; v < nv; v++) {
                        float dd = 0.0f;
                        for (int k = 0; k < 3; k++) {
                            float dv = vm.posed[v].pos[k] - full[v * 3 + k];
                            dd += dv * dv;
                        }
                        if (dd > 1e-8f) moved++;
                        if (dd > worst) worst = dd;
                    }
                    free(full);
                    printf("    %u of %u vertices move, worst %.3f m\n",
                           moved, nv, sqrtf(worst));
                    CHECK(moved > 0, "the needles move between full and empty");
                    CHECK(moved < nv / 2, "and the rest of the gun does not");
                    CHECK(sqrtf(worst) > 0.005f, "by enough to see");
                }
            } else {
                CHECK(vm.clip_ammo < 0, "no other weapon has one");
            }
            hta_viewmodel_free(&vm);
        }
        CHECK(found, "the needler is in the roster");
    }

    printf("\n[every weapon flashes]\n");
    /* The shotgun shipped with no visible muzzle flash. Its firing effect
     * lists `sparks trail` -- six to nine sprites 1.6 cm across, thrown at
     * 15 world units a second -- BEFORE its actual 12.5 cm flash, and we
     * took the first match. A flash sits on the muzzle; sparks are thrown. */
    int flashes = 0;
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
        hta_effect_particle f;
        if (!hta_effect_fp_flash(&c, w.firing_fx_id, "primary trigger", &f)) {
            /* The flamethrower hangs its first-person flame off `spawn
             * fire` rather than `primary trigger`, so the viewmodel falls
             * back to any marker. Nothing else needs to. */
            CHECK(strstr(w.path, "flamethrower") != NULL,
                  "only the flamethrower uses another marker");
            CHECK(hta_effect_fp_flash(&c, w.firing_fx_id, NULL, &f),
                  "  and it does have one there");
        }
        float radius = (f.radius_min + f.radius_max) * 0.5f;
        printf("  %-16s %.3f m radius, %.3f s\n", leaf(w.path), radius, f.lifespan);
        CHECK(radius > 0.02f, "the flash is big enough to see");
        CHECK(radius < 0.60f, "and not big enough to fill the screen");
        CHECK(f.lifespan > 0.0f, "and lasts a measurable time");
        CHECK(f.blend == HTA_FX_BLEND_ADD, "and adds light rather than blending");
        if (!strcmp(leaf(w.path), "shotgun")) {
            /* The exact regression: the spark cloud is 0.0159 at the top. */
            CHECK(f.radius_max > 0.1f, "the shotgun gets its flash, not its sparks");
        }
        flashes++;
    }
    CHECK(flashes == (int)count, "every weapon flashes");

    printf("\n[every weapon makes a noise]\n");
    /* Almost every weapon's gunshot hangs off its trigger's firing effect.
     * The flamethrower's firing effect has no sound in it at all -- its roar
     * is a looping sound attached to the weapon object's `primary trigger`,
     * and that is the only weapon we should fall back to it for. The plasma
     * pistol hangs its OVERCHARGE whine on that same marker, so falling back
     * unconditionally would make it hum every time it fired. */
    int loops = 0, silent = 0;
    for (uint32_t i = 0; i < count; i++) {
        hta_weapon_def w;
        if (!hta_weapon_load_id(&c, NULL, ids[i], &w, NULL, err, sizeof(err))) continue;
        uint32_t shot = hta_effect_first_sound(&c, w.firing_fx_id);
        hta_loop_sound ls;
        bool has_loop = hta_object_loop_sound(&c, ids[i], "primary trigger", &ls);
        if (!shot) {
            silent++;
            printf("  %-16s no effect sound; loop %s\n", leaf(w.path),
                   has_loop ? "yes" : "NO");
            CHECK(has_loop, "a weapon with no shot sound has a looping one");
            CHECK(ls.loop != 0 || ls.start != 0, "  and the track names a sound");
            CHECK(ls.gain > 0.0f, "  at an audible gain");
            CHECK(strstr(w.path, "flamethrower") != NULL,
                  "  and it is the flamethrower");
            loops++;
        }
        if (has_loop && shot) {
            /* Fine to exist -- it just must not be treated as the gunshot. */
            printf("  %-16s has both a shot sound and a loop (not the gunshot)\n",
                   leaf(w.path));
        }
    }
    CHECK(silent == 1, "exactly one weapon has no firing-effect sound");
    CHECK(loops == 1, "and exactly one continuous weapon");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
