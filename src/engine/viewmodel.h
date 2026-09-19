/* Animated first-person viewmodel, driven entirely by Trial tags.
 *
 * Halo builds the first-person view from two meshes sharing one skeleton:
 *   - the hands  (`matg` -> first person interface -> first person hands)
 *   - the weapon (`weap+0x45C`, the gun alone -- it has no arms)
 * and animates both with the weapon's `antr` (`weap+0x46C`), whose node list
 * is the union of the two. `frame gun` hangs off `frame r wriste`, so the gun
 * is in the right hand because the skeleton says so, not because of an offset.
 *
 * Vertices are skinned on the CPU: ~3.2k verts for the AR, two bone influences
 * each, once per frame. That keeps the Vulkan side to a vertex-buffer write.
 */
#ifndef HTA_VIEWMODEL_H
#define HTA_VIEWMODEL_H

#include "../asset/anim.h"
#include "../asset/model.h"
#include "../asset/weapon.h"

typedef enum {
    HTA_VM_READY = 0,   /* raise on spawn, then fall through to idle */
    HTA_VM_IDLE,
    HTA_VM_FIRE,
    HTA_VM_RELOAD,
    HTA_VM_MELEE,
    HTA_VM_STATE_COUNT
} hta_vm_state;

typedef struct {
    hta_anim_graph   graph;
    hta_bsp_mesh     mesh;      /* bind pose: uploaded once, owns the textures */
    hta_skin_vertex *skin;      /* mesh.vertex_count entries */
    hta_vertex      *posed;     /* mesh.vertex_count, rewritten every frame */
    uint32_t         hands_verts, gun_verts;

    hta_transform    rest_inv[HTA_ANIM_MAX_NODES];
    uint8_t          have_rest[HTA_ANIM_MAX_NODES];

    /* animation slots, -1 when the graph has no such clip */
    int32_t          clip[HTA_VM_STATE_COUNT];
    hta_vm_state     state;
    float            frame;       /* in animation frames (30 Hz) */
    bool             loaded;

    /* set for one update when a clip's tagged sound frame is crossed.
     * `snd!` decoding needs sounds.map, which SetupActivity does not pick yet;
     * until then this is the hook, not a played sound. */
    uint32_t         sound_cue;   /* snd! tag id, 0 = none */
} hta_viewmodel;

/* Builds hands + gun against the weapon's animation graph. Returns false and
 * leaves `vm` zeroed if the weapon has no FP animations (vehicle turrets). */
bool hta_viewmodel_load(hta_viewmodel *vm, const hta_cache *c,
                        const hta_resource_map *bitmaps,
                        const hta_weapon_def *weap, char *err, size_t errlen);
void hta_viewmodel_free(hta_viewmodel *vm);

/* Advances playback and re-skins `posed`. */
void hta_viewmodel_update(hta_viewmodel *vm, float dt);
/* Skins `posed` from graph-space node transforms. hta_viewmodel_update calls
 * this; htaview drives it directly to step a clip frame by frame. */
void hta_viewmodel_pose(hta_viewmodel *vm, const hta_transform *world);

/* Restarts a clip. Ignored if the graph has no clip for that state. */
void hta_viewmodel_play(hta_viewmodel *vm, hta_vm_state s);

#endif
