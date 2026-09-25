/* World effects the engine draws for itself: debris chunks, and sprites --
 * blood, dust, sparks, splashes, smoke, and flat splats on surfaces.
 * Portable; no renderer here, only meshes and vertices for it.
 *
 * Nothing here comes from game data. Textures are generated procedurally
 * into one atlas at start-up, so these effects work on any map, imported
 * or not, and ship with no assets.
 *
 * Two meshes, both "dynamic": topology fixed at creation, a fixed slot per
 * body or sprite, and a dead slot collapsed to a point so the index buffer
 * never changes (the particle system's arrangement).
 *   debris  -- one lit box per rigid body, textured by material
 *   sprites -- alpha and additive quads, coloured per vertex
 */
#ifndef HTA_FX_H
#define HTA_FX_H

#include <stdbool.h>
#include <stdint.h>
#include "../asset/bsp.h"
#include "camera.h"
#include "player.h"
#include "rigid.h"

/* Atlas tiles, 4 x 4 of 128 px. */
typedef enum {
    HTA_TILE_WOOD = 0, HTA_TILE_METAL, HTA_TILE_CONCRETE, HTA_TILE_GLASS,
    HTA_TILE_FLESH, HTA_TILE_DIRT, HTA_TILE_BLOB, HTA_TILE_STREAK,
    HTA_TILE_FLAKE, HTA_TILE_SPARK, HTA_TILE_RING, HTA_TILE_SMOKE,
    HTA_TILE_SPLAT, HTA_TILE_COUNT
} hta_fx_tile;

#define HTA_FX_ATLAS 512u

/* Fills `out` with the procedural atlas (malloc'd rgba; free it with
 * hta_fx_atlas_free). Deterministic. */
bool hta_fx_atlas(hta_bsp_texture *out);
void hta_fx_atlas_free(hta_bsp_texture *t);
/* The atlas tile a rigid material draws with. */
hta_fx_tile hta_fx_material_tile(hta_rigid_material m);

/* ---------------------------------------------------------------- sprites */

typedef enum {
    HTA_SPRITE_BILLBOARD = 0,  /* faces the camera */
    HTA_SPRITE_STREAK,         /* stretched along its velocity (rain, sparks) */
    HTA_SPRITE_FLAT            /* lies on a surface: a splat, a scorch */
} hta_sprite_mode;

typedef struct {
    float pos[3], vel[3];
    float normal[3];          /* FLAT only */
    float size0, size1;       /* radius at birth and at death */
    float color[4];           /* rgba, alpha multiplies the tile's */
    float life, age;
    float gravity;            /* multiples of world gravity, signed */
    float drag;               /* per second */
    float spin, angle;
    uint8_t tile;             /* hta_fx_tile */
    uint8_t mode;             /* hta_sprite_mode */
    bool  additive;
    bool  collide;            /* stops on the world; a blood drop leaves a splat */
    bool  active;
} hta_sprite;

/* Canned bursts. `dir` is the main throw direction (need not be unit). */
typedef enum {
    HTA_BURST_BLOOD = 0,      /* a spray of drops that splat where they land */
    HTA_BURST_GORE,           /* a gib: mist, chunks of spray, heavier */
    HTA_BURST_DUST,           /* a puff of dust off concrete and dirt */
    HTA_BURST_SPLINTERS,      /* wood breaking */
    HTA_BURST_SPARKS,         /* metal struck */
    HTA_BURST_GLASS,          /* glittering shards */
    HTA_BURST_SPLASH,         /* a raindrop hitting the ground */
    HTA_BURST_COUNT
} hta_burst;

typedef struct {
    /* debris */
    hta_bsp_mesh debris;          /* template for hta_gfx_mesh_upload_dynamic_world */
    hta_vertex  *debris_verts;    /* this frame's vertices, debris.vertex_count long */
    uint32_t     debris_slots;
    /* sprites: submesh 0 alpha, 1 additive */
    hta_bsp_mesh sprites;
    hta_vertex  *sprite_verts;
    hta_sprite  *pool;
    uint32_t     alpha_slots, add_slots;
    hta_bsp_texture atlas;        /* shared by both meshes */
    const hta_collision *world;
    float        gravity;         /* wu/s^2, as the rigid world's */
    float        density;         /* the particle_density setting, 0..1 */
    float        light;           /* scene brightness sprites are shaded by */
    uint32_t     rng;
    uint32_t     splats;          /* diagnostics: splats made */
} hta_fx;

bool hta_fx_init(hta_fx *fx, uint32_t debris_slots, uint32_t alpha_slots, uint32_t add_slots,
                 const hta_collision *world);
void hta_fx_free(hta_fx *fx);

/* One sprite; returns false when the pool is full (the oldest of its kind
 * is replaced unless `keep_old`). */
bool hta_fx_emit(hta_fx *fx, const hta_sprite *s);
/* A canned burst of `count` (scaled by density) at `pos`. */
void hta_fx_burst(hta_fx *fx, hta_burst kind, const float pos[3], const float dir[3], uint32_t count);
/* A flat splat lying on the surface at `pos` facing `normal`. */
void hta_fx_splat(hta_fx *fx, const float pos[3], const float normal[3], float size,
                  const float color[4], float life);

void hta_fx_update(hta_fx *fx, float dt);
uint32_t hta_fx_live(const hta_fx *fx);

/* Writes this frame's vertices. Debris from the rigid world (fading
 * bodies shrink), sprites facing `cam`. */
void hta_fx_build_debris(hta_fx *fx, const hta_rigid_world *w);
void hta_fx_build_sprites(hta_fx *fx, const hta_camera *cam);

#endif
