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

#define HTA_PART_TYPES    12u
#define HTA_PART_PER_TYPE 16u
#define HTA_PART_MAX      (HTA_PART_TYPES * HTA_PART_PER_TYPE)
#define HTA_PART_RECIPES   8u   /* a detonation plus one per map material */
#define HTA_PART_EMITS     8u   /* particle entries in one effect */

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

/* One particle entry of an effect: which type to throw, how many, how fast.
 * Kept apart from the type because several effects share the same art --
 * every impact in the Trial throws the same sparks and the same smoke, and
 * one texture serves them all. */
typedef struct {
    uint8_t  type;
    int16_t  count_min, count_max;
    float    speed_min, speed_max, spread;
    float    radius_min, radius_max;
    float    life, fade_in, fade_out;
} hta_particle_emit;

typedef struct {
    uint32_t          effect_id;
    hta_particle_emit emit[HTA_PART_EMITS];
    uint32_t          emit_count;
} hta_particle_recipe;

typedef struct {
    hta_particle        live[HTA_PART_MAX];
    hta_particle_type   type[HTA_PART_TYPES];
    uint32_t            type_count;
    hta_particle_recipe recipe[HTA_PART_RECIPES];
    uint32_t            recipe_count;

    hta_bsp_mesh mesh;
    uint32_t     rng;
    bool         loaded;
} hta_particles;

void hta_particles_init(hta_particles *p);
void hta_particles_free(hta_particles *p);

/* Two phases, because the mesh cannot be sized until every type is known
 * and re-interning a texture mid-game would move it under the buffer the
 * GPU is reading.
 *
 * Add each effect you will ever spawn, then build once. `hta_particles_add`
 * returns a recipe index to pass to burst, or HTA_PART_NO_RECIPE when the
 * effect has nothing drawable -- which is not an error. */
#define HTA_PART_NO_RECIPE 0xFFFFFFFFu
uint32_t hta_particles_add(hta_particles *p, const hta_cache *c,
                           const hta_resource_map *bitmaps,
                           uint32_t effect_tag_id);

/* The same, but only the particles attached to one marker. A weapon's
 * firing effect carries its muzzle flashes AND its ejected casing; the
 * flash is already drawn by the viewmodel, so the casing is taken alone by
 * asking for `primary ejection`. */
uint32_t hta_particles_add_marker(hta_particles *p, const hta_cache *c,
                                  const hta_resource_map *bitmaps,
                                  uint32_t effect_tag_id, const char *marker);
bool hta_particles_build(hta_particles *p, char *err, size_t errlen);

/* Throw one recipe's worth of particles from a point, biased along `dir`
 * (a surface normal for an impact). Ignored for an unknown recipe. */
void hta_particles_burst(hta_particles *p, uint32_t recipe,
                         const float origin[3], const float dir[3]);

/* Fly, age out, and re-face the camera. The caller re-uploads the vertices. */
void hta_particles_update(hta_particles *p, const hta_camera *cam, float dt);

uint32_t hta_particles_count(const hta_particles *p);

#endif
