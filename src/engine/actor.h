/* A skinned character standing in the WORLD, as opposed to the viewmodel's
 * pair of hands stapled to the camera.
 *
 * There has never been one of these: the only body in the game was the
 * player's own, and you never saw it. Dying is what needs it -- Halo cuts to
 * a third-person view of your corpse -- and it is the same machinery any
 * other player or bot would want, which is why it is its own module rather
 * than a branch inside the viewmodel.
 *
 * The heavy lifting is already shared: hta_model_append_skinned binds any
 * `mod2` to any animation graph by node name, and the skinning loop is the
 * viewmodel's. What is new here is that the result is placed somewhere in
 * the world, by a position and a facing, rather than in view space.
 */
#ifndef HTA_ACTOR_H
#define HTA_ACTOR_H

#include "../asset/anim.h"
#include "../asset/bsp.h"
#include "../asset/model.h"
#include "../asset/cache.h"

typedef struct {
    hta_bsp_mesh      mesh;        /* bind pose; owns its textures */
    hta_vertex       *posed;       /* what the renderer draws, world space */
    hta_skin_vertex  *skin;
    hta_anim_graph    graph;
    hta_transform     rest_inv[HTA_ANIM_MAX_NODES];
    uint8_t           have_rest[HTA_ANIM_MAX_NODES];

    int32_t  clip;                 /* the animation playing, -1 for none */
    float    frame;
    bool     hold_last;            /* stop on the final frame, do not loop */
    bool     finished;             /* it has reached that frame */

    /* An OVERLAY laid on top of the clip above, -1 for none.
     *
     * Halo's flinches are overlay animations: they keyframe the handful of
     * nodes that jerk and nothing else. Played as an ordinary clip the
     * untouched nodes take the animation's own defaults instead of the pose
     * you meant to keep, and the body turns inside out -- which is exactly
     * what "when he's getting hurt he flips upside down" was. */
    int32_t  overlay;
    float    overlay_frame;

    float    pos[3];
    float    yaw;                  /* radians about +Z */
    uint32_t model_id;             /* the body's own, for finding markers */

    bool     loaded;
} hta_actor;

/* Loads the biped's own model and animation graph. `bipd_tag_id` is the
 * biped, not the model: the model and the graph are both hung off it. */
bool hta_actor_load(hta_actor *a, const hta_cache *c,
                    const hta_resource_map *bitmaps, uint32_t bipd_tag_id,
                    char *err, size_t errlen);
void hta_actor_free(hta_actor *a);

/* Start an animation by substring, the way hta_anim_find matches. `hold`
 * stops it on the last frame instead of looping, which is what a death
 * wants. False if the graph has no such clip. */
bool hta_actor_play(hta_actor *a, const char *name, bool hold);

/* Lay an overlay over whatever is playing. Only the nodes the overlay
 * actually keyframes are taken, which is what `hta_anim_animates` is for.
 * It runs once and clears itself. */
bool hta_actor_play_overlay(hta_actor *a, const char *name);

/* Is one still running? */
bool hta_actor_overlaying(const hta_actor *a);

/* One of Halo's death animations, chosen at random. The cyborg names them
 * `h-kill ...` for a hard death and `s-kill ...` for a soft one, by the
 * direction the blow came from; without a direction to work with, any of
 * them reads as a body going down. */
bool hta_actor_play_death(hta_actor *a, uint32_t *rng);

/* Put something in its hand.
 *
 * Halo's third-person weapon is not skinned to the body -- it is a rigid
 * model riding a marker, and the cyborg has `right hand` on `bip01 r hand`
 * for exactly this. Without it the idle reads as a man standing with his
 * arms out: `stand rifle idle` poses the hands to HOLD a rifle, and a rifle
 * that is not there is the whole difference.
 *
 * The geometry is appended to the actor's own mesh, so it poses, uploads and
 * draws as one thing. Call after loading and before the first place. */
bool hta_actor_hold(hta_actor *a, const hta_cache *c,
                    const hta_resource_map *bitmaps, uint32_t model_tag_id,
                    const char *marker, char *err, size_t errlen);

/* Advance the clip. */
void hta_actor_update(hta_actor *a, float dt);

/* Put it at `pos` facing `yaw` and write world-space vertices into
 * `a->posed`. Call after update, before uploading. */
void hta_actor_place(hta_actor *a, const float pos[3], float yaw);

#endif
