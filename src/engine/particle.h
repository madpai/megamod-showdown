/* Particles: the smoke, sparks and fire an effect throws out.
 *
 * Halo's `effe` events carry a list of particles beside their parts, each
 * naming a `part` tag, how many to spawn, how fast to throw them and inside
 * what cone. A rocket's detonation is three such entries; the flamethrower's
 * jet is one. Nothing here invents a number.
 *
 * Geometry works the way the projectiles do: ONE mesh, built when the
 * effect is loaded, holding a fixed slot per particle. A slot is a camera
 * facing quad re-posed on the CPU each frame, and a dead one collapses to a
 * point so the index buffer never changes.
 *
 * Slots are partitioned by TYPE rather than pooled, so each type's quads are
 * contiguous and can share one submesh -- one draw call per particle type
 * instead of one per particle.
 */
#ifndef HTA_PARTICLE_H
#define HTA_PARTICLE_H

#include "../asset/bsp.h"
#include "../asset/cache.h"
#include "../asset/bitmap.h"
#include "../asset/effect.h"
#include "camera.h"

#define HTA_PART_TYPES     6u
#define HTA_PART_PER_TYPE 24u
#define HTA_PART_MAX      (HTA_PART_TYPES * HTA_PART_PER_TYPE)

typedef struct {
    float pos[3], vel[3];
    float age, life;
    float radius0, radius1;   /* Halo grows a particle over its life */
    float fade_in, fade_out;  /* seconds */
    bool  alive;
} hta_particle;

typedef struct {
    uint32_t bitmap_id;
    uint32_t tex;             /* index into mesh.textures */
    uint8_t  blend;           /* HTA_FX_BLEND_* */
    /* A particle bitmap is usually a sheet of variants; Halo picks one per
     * particle, which is why no two puffs of smoke look alike. */
    hta_bitmap_sprite sprite[8];
    uint32_t sprite_count;
} hta_particle_type;

typedef struct {
    hta_particle       live[HTA_PART_MAX];
    hta_particle_type  type[HTA_PART_TYPES];
    uint32_t           type_count;

    /* What each type spawns, straight from the effect. */
    int16_t  count_min[HTA_PART_TYPES], count_max[HTA_PART_TYPES];
    float    speed_min[HTA_PART_TYPES], speed_max[HTA_PART_TYPES];
    float    spread[HTA_PART_TYPES];
    float    radius_min[HTA_PART_TYPES], radius_max[HTA_PART_TYPES];
    float    life[HTA_PART_TYPES];
    float    fade_in[HTA_PART_TYPES], fade_out[HTA_PART_TYPES];

    hta_bsp_mesh mesh;
    uint32_t     rng;
    bool         loaded;
} hta_particles;

void hta_particles_init(hta_particles *p);
void hta_particles_free(hta_particles *p);

/* Build the types an effect spawns. False (and everything cleared) when the
 * effect has no particles we can draw, which is not an error. */
bool hta_particles_load(hta_particles *p, const hta_cache *c,
                        const hta_resource_map *bitmaps,
                        uint32_t effect_tag_id, char *err, size_t errlen);

/* Throw one effect's worth of particles from a point, biased along `dir`
 * (a surface normal for an impact). Ignored when nothing is loaded. */
void hta_particles_burst(hta_particles *p, const float origin[3],
                         const float dir[3]);

/* Fly, age out, and re-face the camera. The caller re-uploads the vertices. */
void hta_particles_update(hta_particles *p, const hta_camera *cam, float dt);

uint32_t hta_particles_count(const hta_particles *p);

#endif
