#include "viewmodel.h"
#include "../asset/effect.h"
#include "../asset/bitmap.h"
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

/* Find the first-person muzzle flash and reserve geometry for it.
 *
 * Failing is fine and common -- a vehicle turret has no FP flash -- so this
 * returns nothing and simply leaves have_flash false. */
static void setup_flash(hta_viewmodel *vm, const hta_cache *c,
                        const hta_resource_map *bitmaps,
                        const hta_weapon_def *weap)
{
    if (!weap->firing_fx_id) return;

    /* Which marker the flash hangs from is the effect's business; where that
     * marker sits is the model's. Ask the effect first so we never hardcode
     * "primary trigger" for a weapon whose tag says otherwise. */
    hta_effect_particle flash;
    if (!hta_effect_fp_flash(c, weap->firing_fx_id, "primary trigger", &flash))
        return;

    char node_name[32];
    float offset[3];
    if (!hta_model_marker(c, weap->fp_model_id, flash.marker, node_name, offset))
        return;

    /* A model's node indices are its own; names are what bind it to the
     * animation graph, exactly as the skinning does. */
    int32_t node = hta_anim_node_index(&vm->graph, node_name);
    if (node < 0) return;

    uint32_t tex = hta_mesh_intern_bitmap(&vm->mesh, c, bitmaps, flash.bitmap_id, 0);
    if (tex == ~0u) return;

    /* Collect the sprite variants that live on the sheet we interned. Halo
     * spreads nine flashes over two sheets; taking the six on sheet 0 keeps
     * this to one texture and one submesh, and no one can tell which six. */
    uint32_t seqs = hta_bitmap_sequence_count(c, flash.bitmap_id);
    for (uint32_t i = 0; i < seqs && vm->flash_sprite_count < 12; i++) {
        hta_bitmap_sprite sp;
        if (!hta_bitmap_sprite_at(c, flash.bitmap_id, i, &sp)) continue;
        if (sp.bitmap_index != 0) continue;
        if (sp.u1 <= sp.u0 || sp.v1 <= sp.v0) continue;
        vm->flash_sprite[vm->flash_sprite_count++] = sp;
    }
    if (!vm->flash_sprite_count) return;

    uint32_t base_v = vm->mesh.vertex_count;
    uint32_t base_i = vm->mesh.index_count;
    hta_vertex *nv = (hta_vertex *)realloc(vm->mesh.vertices,
                                           (size_t)(base_v + 4) * sizeof(hta_vertex));
    if (!nv) return;
    vm->mesh.vertices = nv;
    uint32_t *ni = (uint32_t *)realloc(vm->mesh.indices,
                                       (size_t)(base_i + 6) * sizeof(uint32_t));
    if (!ni) return;
    vm->mesh.indices = ni;
    hta_submesh *ns = (hta_submesh *)realloc(vm->mesh.submeshes,
                                             (size_t)(vm->mesh.submesh_count + 1)
                                             * sizeof(hta_submesh));
    if (!ns) return;
    vm->mesh.submeshes = ns;

    /* The skinning arrays are indexed by vertex, so they have to grow too --
     * these four are posed by hand, but hta_viewmodel_pose still walks them. */
    hta_skin_vertex *nk = (hta_skin_vertex *)realloc(vm->skin,
                                                     (size_t)(base_v + 4)
                                                     * sizeof(hta_skin_vertex));
    if (!nk) return;
    vm->skin = nk;
    memset(&vm->skin[base_v], 0, 4 * sizeof(hta_skin_vertex));

    static const float quad_uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
    for (uint32_t i = 0; i < 4; i++) {
        hta_vertex *v = &vm->mesh.vertices[base_v + i];
        memset(v, 0, sizeof(*v));
        v->normal[2] = 1.0f;
        v->uv[0] = quad_uv[i][0];
        v->uv[1] = quad_uv[i][1];
    }
    uint32_t tris[6] = { base_v + 0, base_v + 1, base_v + 2,
                         base_v + 0, base_v + 2, base_v + 3 };
    memcpy(&vm->mesh.indices[base_i], tris, sizeof(tris));

    hta_submesh *sm = &vm->mesh.submeshes[vm->mesh.submesh_count++];
    memset(sm, 0, sizeof(*sm));
    sm->first_index = base_i;
    sm->index_count = 6;
    sm->albedo_tex = tex;
    sm->lightmap_tex = ~0u;
    sm->lightmap_index = 0xFFFFu;
    sm->draw_mode = HTA_DRAW_ADD;

    vm->mesh.vertex_count = base_v + 4;
    vm->mesh.index_count = base_i + 6;

    vm->have_flash = true;
    vm->flash_node = node;
    vm->flash_offset[0] = offset[0];
    vm->flash_offset[1] = offset[1];
    vm->flash_offset[2] = offset[2];
    vm->flash_radius = flash.radius_max > 0.0f ? flash.radius_max : 0.1f;
    vm->flash_life = flash.lifespan > 0.0f ? flash.lifespan : 0.08f;
    vm->flash_first_vertex = base_v;
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

    /* The muzzle flash: four more vertices and one more submesh on the same
     * mesh. Everything about it comes from the weapon's own firing effect. */
    setup_flash(vm, c, bitmaps, weap);

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

void hta_viewmodel_flash(hta_viewmodel *vm)
{
    if (!vm || !vm->have_flash) return;
    vm->flash_timer = vm->flash_life;
    /* A different variant each shot, as Halo does. */
    vm->flash_rng = vm->flash_rng * 1664525u + 1013904223u;
    vm->flash_pick = (vm->flash_rng >> 16) % vm->flash_sprite_count;
}

void hta_viewmodel_play(hta_viewmodel *vm, hta_vm_state s)
{
    if (!vm || s >= HTA_VM_STATE_COUNT || vm->clip[s] < 0) return;
    vm->state = s;
    vm->frame = 0.0f;
    vm->sound_cue = 0;
}

/* Places the muzzle-flash quad for this frame.
 *
 * The particle tag orients this one "parallel to direction" -- along the
 * barrel, not facing the screen -- so the quad is built in the muzzle node's
 * own frame: the barrel runs down local +X, and the quad spans the other two
 * axes. When the flash is not lit, all four vertices collapse onto the muzzle
 * so the two triangles have zero area and rasterise nothing. Collapsing beats
 * skipping the submesh: the index buffer is uploaded once and never edited.
 */
static void pose_flash(hta_viewmodel *vm, const hta_transform *world)
{
    if (!vm->have_flash) return;
    hta_vertex *q = &vm->posed[vm->flash_first_vertex];

    const hta_transform *m = &world[vm->flash_node];
    float centre[3];
    hta_xf_point(centre, m, vm->flash_offset);

    if (vm->flash_timer <= 0.0f) {
        for (int i = 0; i < 4; i++) {
            q[i].pos[0] = centre[0];
            q[i].pos[1] = centre[1];
            q[i].pos[2] = centre[2];
        }
        return;
    }

    /* The quad's plane: the two node axes that are not the barrel. */
    const float ay[3] = { 0.0f, 1.0f, 0.0f };
    const float az[3] = { 0.0f, 0.0f, 1.0f };
    float u[3], v[3];
    hta_xf_vector(u, m, ay);
    hta_xf_vector(v, m, az);

    /* Halo grows the flash over its life; fading is the renderer's additive
     * blend doing the work, so shrinking to nothing is the fade. */
    float t = vm->flash_timer / (vm->flash_life > 0.0f ? vm->flash_life : 1.0f);
    if (t > 1.0f) t = 1.0f;
    float r = vm->flash_radius * t;

    for (int i = 0; i < 3; i++) {
        q[0].pos[i] = centre[i] - u[i]*r - v[i]*r;
        q[1].pos[i] = centre[i] + u[i]*r - v[i]*r;
        q[2].pos[i] = centre[i] + u[i]*r + v[i]*r;
        q[3].pos[i] = centre[i] - u[i]*r + v[i]*r;
    }
    /* The sheet holds several flashes; only this shot's rectangle may be
     * sampled, or the quad shows the whole sheet at once. */
    const hta_bitmap_sprite *sp = &vm->flash_sprite[vm->flash_pick];
    const float su[4] = { sp->u0, sp->u1, sp->u1, sp->u0 };
    const float sv[4] = { sp->v0, sp->v0, sp->v1, sp->v1 };
    for (int i = 0; i < 4; i++) {
        q[i].normal[0] = 0.0f; q[i].normal[1] = 0.0f; q[i].normal[2] = 1.0f;
        q[i].uv[0] = su[i];
        q[i].uv[1] = sv[i];
    }
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
    pose_flash(vm, world);
}

void hta_viewmodel_update(hta_viewmodel *vm, float dt)
{
    if (!vm || !vm->loaded) return;
    int32_t ci = vm->clip[vm->state];
    if (ci < 0) { vm->state = HTA_VM_IDLE; ci = vm->clip[HTA_VM_IDLE]; }
    const hta_animation *a = &vm->graph.anims[ci];

    if (vm->flash_timer > 0.0f) {
        vm->flash_timer -= dt;
        if (vm->flash_timer < 0.0f) vm->flash_timer = 0.0f;
    }

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
