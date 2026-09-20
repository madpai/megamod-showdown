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
#include "../asset/bitmap.h"

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

    /* Muzzle flash. Four vertices and a submesh appended to the viewmodel's
     * own mesh, so it is skinned, posed and drawn in view space with
     * everything else -- no second pass, no world-space transform to get
     * wrong. Collapsed to zero area when not firing.
     *
     * Everything about it is the weapon's own tag: which marker it hangs
     * from, its bitmap, its radius and how long it lasts. */
    bool             have_flash;
    int32_t          flash_node;      /* index into graph.nodes */
    float            flash_offset[3]; /* marker translation, node-local */
    float            flash_radius;
    float            flash_life;      /* seconds, from the particle tag */
    float            flash_timer;     /* counts down while visible */
    uint32_t         flash_first_vertex;
    /* The flash bitmap is a sprite sheet of nine variants; Halo picks one
     * per shot. These are the ones on sheet 0, which is the texture we
     * interned. */
    hta_bitmap_sprite flash_sprite[12];
    uint32_t          flash_sprite_count;
    uint32_t          flash_pick;
    uint32_t          flash_rng;

    /* The readout on the gun itself. Halo's assault rifle carries its own
     * round counter: two quads on `frame display`, each a
     * shader_transparent_chicago whose "numeric counter limit" is the
     * weapon's magazine size, textured from a TEN-FRAME bitmap -- one image
     * per digit, not a sprite sheet. The ten frames are decoded into a
     * single wide atlas so a digit is chosen by shifting U. */
    /* The needler wears its magazine: sixteen needle bones that fold away as
     * it empties. Halo drives them with an OVERLAY clip, `first-person
     * ammunition`, 21 frames for a 20-round magazine -- frame 0 full, frame
     * 20 empty -- composed on top of whatever the weapon is doing. It is the
     * only Trial weapon with one. */
    int32_t           clip_ammo;      /* -1 when the weapon has no such clip */
    float             ammo_frame;

    bool              have_counter;
    uint32_t          counter_submesh[2];   /* [0] most significant */
    uint32_t          counter_vertex[2][4];   /* the quad's four corners */
    float             counter_uv[2][4][2];  /* the model's own UVs, per quad */
    uint32_t          counter_digits;       /* how many quads we found */
    uint32_t          counter_frames;       /* frames in the atlas */
    uint32_t          counter_value;
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

/* Lights the muzzle flash for its tagged lifespan. Harmless if the weapon
 * has no first-person flash particle. */
void hta_viewmodel_flash(hta_viewmodel *vm);

/* Compose the ammunition overlay onto a sampled base pose, in place. Called
 * by hta_viewmodel_update; exposed so a tool that drives the graph itself
 * previews exactly what the game draws. Harmless without an overlay. */
void hta_viewmodel_apply_ammo(const hta_viewmodel *vm, hta_transform *local);

/* How full the magazine is, 0..1, for weapons that show it on the model.
 * Harmless on a weapon without an ammunition overlay. */
void hta_viewmodel_set_ammo(hta_viewmodel *vm, float fraction);

/* What the gun's own readout shows. Harmless if it has no readout. */
void hta_viewmodel_set_counter(hta_viewmodel *vm, uint32_t value);

#endif
