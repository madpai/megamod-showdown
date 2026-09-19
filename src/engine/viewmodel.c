#include "viewmodel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Clip names in the Trial's FP graphs. Matched as case-insensitive substrings
 * so "first-person reload-full" and "first-person reload-full2" both resolve. */
static const char *const CLIP_NAME[HTA_VM_STATE_COUNT] = {
    "ready", "idle", "firing", "reload", "melee"
};

static void free_partial(hta_viewmodel *vm)
{
    hta_bsp_free(&vm->mesh);
    free(vm->skin);
    free(vm->posed);
    hta_anim_free(&vm->graph);
    memset(vm, 0, sizeof(*vm));
}

bool hta_viewmodel_load(hta_viewmodel *vm, const hta_cache *c,
                        const hta_resource_map *bitmaps,
                        const hta_weapon_def *weap, char *err, size_t errlen)
{
    if (!vm || !c || !weap) return false;
    memset(vm, 0, sizeof(*vm));
    for (int i = 0; i < HTA_VM_STATE_COUNT; i++) vm->clip[i] = -1;

    if (!weap->fp_anim_id || !weap->fp_model_id) {
        if (err) snprintf(err, errlen, "weapon has no first-person model/animations");
        return false;
    }
    if (!hta_anim_load(&vm->graph, c, weap->fp_anim_id, err, errlen)) return false;

    vm->mesh.textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
    if (!vm->mesh.textures) { free_partial(vm); if (err) snprintf(err, errlen, "oom"); return false; }

    /* Hands first so the gun draws over them where they overlap. */
    uint32_t hands = hta_globals_fp_hands(c);
    if (hands && !hta_model_append_skinned(&vm->mesh, &vm->skin, c, bitmaps, hands,
                                           &vm->graph, vm->rest_inv, vm->have_rest,
                                           err, errlen)) {
        free_partial(vm);
        return false;
    }
    vm->hands_verts = vm->mesh.vertex_count;

    if (!hta_model_append_skinned(&vm->mesh, &vm->skin, c, bitmaps, weap->fp_model_id,
                                  &vm->graph, vm->rest_inv, vm->have_rest, err, errlen)) {
        free_partial(vm);
        return false;
    }
    vm->gun_verts = vm->mesh.vertex_count - vm->hands_verts;

    if (vm->mesh.vertex_count == 0) {
        if (err) snprintf(err, errlen, "first-person meshes are empty");
        free_partial(vm);
        return false;
    }
    vm->posed = (hta_vertex *)malloc((size_t)vm->mesh.vertex_count * sizeof(hta_vertex));
    if (!vm->posed) { free_partial(vm); if (err) snprintf(err, errlen, "oom"); return false; }
    memcpy(vm->posed, vm->mesh.vertices, (size_t)vm->mesh.vertex_count * sizeof(hta_vertex));

    for (int i = 0; i < HTA_VM_STATE_COUNT; i++)
        vm->clip[i] = hta_anim_find(&vm->graph, CLIP_NAME[i]);
    if (vm->clip[HTA_VM_IDLE] < 0) {
        if (err) snprintf(err, errlen, "graph '%s' has no idle clip", vm->graph.path);
        free_partial(vm);
        return false;
    }

    vm->loaded = true;
    hta_viewmodel_play(vm, vm->clip[HTA_VM_READY] >= 0 ? HTA_VM_READY : HTA_VM_IDLE);
    hta_viewmodel_update(vm, 0.0f);
    return true;
}

void hta_viewmodel_free(hta_viewmodel *vm)
{
    if (!vm) return;
    free_partial(vm);
}

void hta_viewmodel_play(hta_viewmodel *vm, hta_vm_state s)
{
    if (!vm || s >= HTA_VM_STATE_COUNT || vm->clip[s] < 0) return;
    vm->state = s;
    vm->frame = 0.0f;
    vm->sound_cue = 0;
}

/* Skins every vertex by the two bones the tag gave it. */
void hta_viewmodel_pose(hta_viewmodel *vm, const hta_transform *world)
{
    hta_transform d[HTA_ANIM_MAX_NODES];
    for (uint32_t i = 0; i < vm->graph.node_count; i++) {
        if (vm->have_rest[i]) hta_xf_mul(&d[i], &world[i], &vm->rest_inv[i]);
        else                  hta_xf_identity(&d[i]);
    }
    for (uint32_t v = 0; v < vm->mesh.vertex_count; v++) {
        const hta_vertex      *src = &vm->mesh.vertices[v];
        const hta_skin_vertex *sw  = &vm->skin[v];
        hta_vertex *out = &vm->posed[v];
        *out = *src;
        float p[3] = {0,0,0}, n[3] = {0,0,0};
        for (int k = 0; k < 2; k++) {
            uint16_t gn = sw->node[k];
            float w = sw->weight[k];
            if (gn == HTA_SKIN_NONE || w <= 0.0f || gn >= vm->graph.node_count) continue;
            float tp[3], tn[3];
            hta_xf_point(tp, &d[gn], src->pos);
            hta_xf_vector(tn, &d[gn], src->normal);
            for (int j = 0; j < 3; j++) { p[j] += tp[j]*w; n[j] += tn[j]*w; }
        }
        out->pos[0]=p[0]; out->pos[1]=p[1]; out->pos[2]=p[2];
        out->normal[0]=n[0]; out->normal[1]=n[1]; out->normal[2]=n[2];
    }
}

void hta_viewmodel_update(hta_viewmodel *vm, float dt)
{
    if (!vm || !vm->loaded) return;
    int32_t ci = vm->clip[vm->state];
    if (ci < 0) { vm->state = HTA_VM_IDLE; ci = vm->clip[HTA_VM_IDLE]; }
    const hta_animation *a = &vm->graph.anims[ci];

    float prev = vm->frame;
    vm->frame += dt * HTA_ANIM_FPS;

    if (a->sound_index >= 0 && (uint32_t)a->sound_index < vm->graph.sound_count) {
        float cue = (float)(a->sound_frame > 0 ? a->sound_frame : 0);
        if (prev <= cue && vm->frame > cue)
            vm->sound_cue = vm->graph.sound_ids[a->sound_index];
    }

    float last = (float)(a->frame_count > 0 ? a->frame_count - 1 : 0);
    if (vm->frame > last) {
        if (vm->state == HTA_VM_IDLE) {
            /* Halo loops the idle from `loop frame index`, not from 0. */
            float loop = (a->loop_frame > 0 && a->loop_frame < a->frame_count)
                       ? (float)a->loop_frame : 0.0f;
            float span = last - loop;
            vm->frame = span > 0.0f ? loop + fmodf(vm->frame - loop, span) : loop;
        } else {
            hta_viewmodel_play(vm, HTA_VM_IDLE);
            ci = vm->clip[HTA_VM_IDLE];
            a = &vm->graph.anims[ci];
        }
    }

    hta_transform local[HTA_ANIM_MAX_NODES], world[HTA_ANIM_MAX_NODES];
    if (!hta_anim_sample(&vm->graph, (uint32_t)ci, vm->frame, local)) return;
    hta_anim_world(&vm->graph, local, world);
    hta_viewmodel_pose(vm, world);
}
