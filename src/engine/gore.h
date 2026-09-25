/* Gibs: a body blown apart. Portable; drives the rigid world and fx.
 *
 * A gib is a flesh chunk in the rigid world -- head, torso halves, limbs,
 * scraps -- thrown by the blast that killed it, with a spray of blood and
 * mist. At the higher gore level each chunk bleeds while it flies, dripping
 * a trail, and paints a splat wherever it strikes hard.
 *
 * Sizes are a Halo-scale person (standing eye 0.62 wu, so about 0.7 wu
 * tall); a caller with bigger bodies passes their height. */
#ifndef HTA_GORE_H
#define HTA_GORE_H

#include "fx.h"
#include "rigid.h"

#define HTA_GIB_USER 0x61000000u   /* rigid body `user` tag for a gib */

typedef struct {
    float pos[3];      /* the body's centre */
    float vel[3];      /* its velocity when it died */
    float from[3];     /* where the blast was (or the shot came from) */
    float strength;    /* 0..1: how hard; a rocket at point blank is 1 */
    float height;      /* body height in wu; 0 = 0.7 */
} hta_gib_desc;

/* Blows a body apart. `level` is the gib_level setting: 0 does nothing,
 * 1 chunks and a burst, 2 adds trails and impact splats. Returns chunks
 * spawned. */
uint32_t hta_gibs_spawn(hta_rigid_world *w, hta_fx *fx, const hta_gib_desc *d, uint32_t level);

/* Per frame after hta_rigid_step: trails and splats for flying gibs.
 * Level as above. */
void hta_gibs_update(hta_rigid_world *w, hta_fx *fx, float dt, uint32_t level);

/* Whether a death of this much blast damage (as a fraction of the body's
 * health) should gib, and how hard. Kept in one place so the host and a
 * joining phone agree without replicating debris. */
bool hta_gibs_should(float blast_fraction, float *strength);

#endif
