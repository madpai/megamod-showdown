#include "viewmodel.h"
#include "../asset/effect.h"
#include "../asset/bitmap.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Clip names in the Trial's FP graphs. Matched as case-insensitive substrings
 * so "first-person reload-full" and "first-person reload-full2" both resolve. */
/* Weapons do not agree on what to call their clips, and a slot has to try
 * every spelling its weapon might use. The assault rifle is the ONLY one in
 * the Trial that says "firing"; every other weapon says "fire-1", so a
 * single name left ten weapons with no firing animation at all.
 *
 * Order matters: the match is a substring, and "fire" alone would also hit
 * the plasma weapons' "misfire-1", so the specific spellings come first.
 * The same goes for reload, where "reload-full" is preferred over
 * "reload-empty" when a weapon has both. */
static const char *const CLIP_NAMES[HTA_VM_STATE_COUNT][4] = {
    { "ready",       NULL,           NULL,     NULL },
    { "idle",        NULL,           NULL,     NULL },
    { "fire-1",      "firing",       "fire-2", NULL },
    { "reload-full", "reload-empty", "reload", NULL },
    { "melee",       NULL,           NULL,     NULL },
};

static void free_partial(hta_viewmodel *vm)
{
    hta_bsp_free(&vm->mesh);
    free(vm->skin);
    free(vm->posed);
    hta_anim_free(&vm->graph);
    memset(vm, 0, sizeof(*vm));
}

/* Find the gun's own round counter.
 *
 * A readout quad is a chicago shader whose "numeric counter limit" equals
 * this weapon's magazine size -- which is the tag saying, in as many words,
 * "this counts that". The assault rifle's compass is a counter too, but its
 * limit is 8, so it is not confused with the ammo.
 *
 * The digits are a ten-frame bitmap, one image per digit. They are decoded
 * once into a wide atlas so picking a digit is a shift in U and costs
 * nothing per frame.
 */
static void setup_counter(hta_viewmodel *vm, const hta_cache *c,
                          const hta_resource_map *bitmaps,
                          const hta_weapon_def *weap)
{
    if (weap->rounds_loaded_max <= 0 || weap->rounds_loaded_max > 255) return;
    uint8_t want = (uint8_t)weap->rounds_loaded_max;

    uint32_t found[2];
    float meanY[2];
    uint32_t n = 0;
    uint32_t bitmap_id = 0;

    for (uint32_t i = 0; i < vm->mesh.submesh_count && n < 2; i++) {
        hta_submesh *sm = &vm->mesh.submeshes[i];
        if (!sm->index_count || sm->index_count > 12) continue;
        if (hta_shader_numeric_limit(c, sm->shader_tag_id) != want) continue;
        uint32_t bm = hta_shader_base_bitmap(c, sm->shader_tag_id);
        if (!bm) continue;
        uint32_t frames = hta_bitmap_frame_count(c, bm);
        if (frames < 10) continue;          /* a digit sheet is 0..9 */
        bitmap_id = bm;

        /* Which digit position this quad is: Halo FP axes put +Y to the
         * left, so the more +Y quad is the more significant digit. */
        uint32_t corner[4];
        uint32_t cn = 0;
        float sum = 0.0f;
        for (uint32_t k = 0; k < sm->index_count && cn < 4; k++) {
            uint32_t v = vm->mesh.indices[sm->first_index + k];
            int dup = 0;
            for (uint32_t q = 0; q < cn; q++) if (corner[q] == v) dup = 1;
            if (dup) continue;
            corner[cn++] = v;
            sum += vm->mesh.vertices[v].pos[1];
        }
        if (cn != 4) continue;   /* not a quad; not a digit */
        found[n] = i;
        meanY[n] = sum / 4.0f;
        for (uint32_t k = 0; k < 4; k++) vm->counter_vertex[n][k] = corner[k];
        n++;
    }
    if (n != 2 || !bitmap_id) return;

    uint32_t atlas = hta_mesh_intern_atlas(&vm->mesh, c, bitmaps, bitmap_id, 10);
    if (atlas == ~0u) return;

    /* Most significant first. */
    uint32_t order[2] = { 0, 1 };
    if (meanY[1] > meanY[0]) { order[0] = 1; order[1] = 0; }

    uint32_t corner_copy[2][4];
    memcpy(corner_copy, vm->counter_vertex, sizeof(corner_copy));
    for (uint32_t d = 0; d < 2; d++) {
        uint32_t src = order[d];
        vm->counter_submesh[d] = found[src];
        vm->mesh.submeshes[found[src]].albedo_tex = atlas;
        /* Keep the model's own UVs: the glyph sits in a sub-rectangle of
         * each frame, and that rectangle must survive the atlas shift. */
        for (uint32_t k = 0; k < 4; k++) {
            uint32_t v = corner_copy[src][k];
            vm->counter_vertex[d][k] = v;
            vm->counter_uv[d][k][0] = vm->mesh.vertices[v].uv[0];
            vm->counter_uv[d][k][1] = vm->mesh.vertices[v].uv[1];
        }
    }
    vm->counter_digits = 2;
    vm->counter_frames = 10;
    vm->have_counter = true;
    vm->counter_value = (uint32_t)weap->rounds_loaded_max;
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
    if (!hta_effect_fp_flash(c, weap->firing_fx_id, "primary trigger", &flash)) {
        /* Almost every weapon hangs its flash off `primary trigger`. The
         * flamethrower hangs its first-person flame off `spawn fire`
         * instead, which is why it had none at all. Falling back to any
         * marker costs nothing: the scorer still requires a first-person
         * additive particle, and the marker has to exist on the model. */
        if (!hta_effect_fp_flash(c, weap->firing_fx_id, NULL, &flash))
            return;
    }

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
    /* Halo picks each sprite's radius somewhere in the tagged range, so the
     * middle of it is the representative size for our single quad. Taking
     * the top put a 50 cm flare on the plasma cannon. */
    float radius = (flash.radius_min + flash.radius_max) * 0.5f;
    vm->flash_radius = radius > 0.0f ? radius : 0.1f;
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
    vm->clip_ammo = -1;

    if (!weap->fp_anim_id || !weap->fp_model_id) {
        if (err) snprintf(err, errlen, "weapon has no first-person model/animations");
        return false;
    }
    if (!hta_anim_load(&vm->graph, c, weap->fp_anim_id, err, errlen)) return false;

    vm->mesh.textures = (hta_bsp_texture *)calloc(256, sizeof(hta_bsp_texture));
    if (!vm->mesh.textures) { free_partial(vm); if (err) snprintf(err, errlen, "oom"); return false; }

    /* Hands first so the gun draws over them where they overlap.
     *
     * Unless the weapon's own first-person model already has arms. Almost
     * every Trial weapon's fp model is the gun alone -- 3 to 7 nodes -- and
     * the arms come from the globals hands model. The fuel rod gun (tagged
     * `plasma_cannon`) ships a complete first-person model instead: 41
     * nodes, 37 of them shared with the hands.
     *
     * Appending both drew two sets of arms, and worse, the second append
     * overwrote the FIRST's bind pose for every shared node -- including the
     * root, which differs by 7.8 cm -- so the globals hands came out lifted
     * to eye level beside a gun held at the wrong angle. That is what "the
     * weird gun" was. A model that has its own wrists does not want ours. */
    uint32_t hands = hta_globals_fp_hands(c);
    if (hta_model_has_node(c, weap->fp_model_id, "frame l wriste")) hands = 0;
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
    /* Only the needler has one, and it is not one of the states: it is
     * composed on top of them. */
    vm->clip_ammo = hta_anim_find(&vm->graph, "ammunition");

    setup_flash(vm, c, bitmaps, weap);
    setup_counter(vm, c, bitmaps, weap);

    if (vm->mesh.vertex_count == 0) {
        if (err) snprintf(err, errlen, "first-person meshes are empty");
        free_partial(vm);
        return false;
    }
    vm->posed = (hta_vertex *)malloc((size_t)vm->mesh.vertex_count * sizeof(hta_vertex));
    if (!vm->posed) { free_partial(vm); if (err) snprintf(err, errlen, "oom"); return false; }
    memcpy(vm->posed, vm->mesh.vertices, (size_t)vm->mesh.vertex_count * sizeof(hta_vertex));

    for (int i = 0; i < HTA_VM_STATE_COUNT; i++) {
        vm->clip[i] = -1;
        for (int k = 0; k < 4 && CLIP_NAMES[i][k] && vm->clip[i] < 0; k++)
            vm->clip[i] = hta_anim_find(&vm->graph, CLIP_NAMES[i][k]);
    }
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

/* The needler's needles are posed by an overlay clip rather than by whatever
 * the gun is doing, so they stay put through firing, reloading and the melee
 * swing. Only the nodes the overlay actually keyframes are taken -- the rest
 * of a sampled overlay is its own default pose, which would flatten the
 * arms. */
void hta_viewmodel_apply_ammo(const hta_viewmodel *vm, hta_transform *local)
{
    if (!vm || !local || vm->clip_ammo < 0) return;

    const hta_animation *a = &vm->graph.anims[vm->clip_ammo];
    float last = (float)(a->frame_count > 0 ? a->frame_count - 1 : 0);

    hta_transform over[HTA_ANIM_MAX_NODES], full[HTA_ANIM_MAX_NODES];
    if (!hta_anim_sample(&vm->graph, (uint32_t)vm->clip_ammo, vm->ammo_frame, over))
        return;
    if (!hta_anim_sample(&vm->graph, (uint32_t)vm->clip_ammo, last, full))
        return;

    /* The clip is a DELTA away from a full magazine, and its full end is
     * its LAST frame.
     *
     * The idle already holds the complete needle rack -- that is what the
     * gun looks like loaded -- so a full magazine has to leave the base
     * pose exactly alone. Referencing the delta to the clip's last frame
     * makes that true by construction: at a full magazine this is the
     * identity. Referencing it to frame 0 instead (the spent end) threw the
     * needles out into a splayed fan as the magazine emptied.
     *
     * Only the nodes the clip keyframes are touched; the rest of a sampled
     * overlay is its own default pose, which would flatten the arms. */
    for (uint32_t i = 0; i < vm->graph.node_count; i++) {
        if (!hta_anim_animates(&vm->graph, (uint32_t)vm->clip_ammo, i)) continue;
        hta_transform inv, delta, posed;
        hta_xf_inverse(&inv, &full[i]);
        hta_xf_mul(&delta, &over[i], &inv);
        hta_xf_mul(&posed, &delta, &local[i]);
        local[i] = posed;
    }
}

void hta_viewmodel_set_ammo(hta_viewmodel *vm, float fraction)
{
    if (!vm || vm->clip_ammo < 0) return;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    const hta_animation *a = &vm->graph.anims[vm->clip_ammo];
    float last = (float)(a->frame_count > 0 ? a->frame_count - 1 : 0);
    /* The LAST frame is a full magazine and frame 0 an empty one -- the
     * opposite of the obvious reading, and the reason the needles ran
     * backwards at first.
     *
     * What misleads: at the full end all sixteen needle bones share one
     * transform, which looks like "collapsed to a point" until you
     * remember each needle is skinned against its OWN bind pose. Identical
     * node transforms therefore mean every needle sits at rest, i.e. the
     * complete rack. It is the spent end that gives them separate,
     * displaced transforms. */
    vm->ammo_frame = fraction * last;
}

void hta_viewmodel_set_counter(hta_viewmodel *vm, uint32_t value)
{
    if (!vm || !vm->have_counter) return;
    vm->counter_value = value;
}

/* Points the readout quads at their digit in the atlas. */
static void pose_counter(hta_viewmodel *vm)
{
    if (!vm->have_counter) return;
    uint32_t value = vm->counter_value;
    uint32_t scale = 1;
    for (uint32_t d = 1; d < vm->counter_digits; d++) scale *= 10u;

    for (uint32_t d = 0; d < vm->counter_digits; d++) {
        uint32_t digit = (value / scale) % 10u;
        scale /= 10u;
        float span = 1.0f / (float)vm->counter_frames;
        for (uint32_t k = 0; k < 4; k++) {
            uint32_t v = vm->counter_vertex[d][k];
            if (v >= vm->mesh.vertex_count) continue;
            /* The model's U runs across one frame; the atlas lays the ten
             * frames side by side, so squeeze into this digit's slot. */
            vm->posed[v].uv[0] = ((float)digit + vm->counter_uv[d][k][0]) * span;
            vm->posed[v].uv[1] = vm->counter_uv[d][k][1];
        }
    }
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
    pose_counter(vm);
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

    hta_viewmodel_apply_ammo(vm, local);

    hta_anim_world(&vm->graph, local, world);
    hta_viewmodel_pose(vm, world);
}
