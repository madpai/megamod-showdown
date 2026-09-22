/* Contrails: the streaks rounds leave in the air.
 *
 * Halo draws every bullet, bolt, shell and needle with a `cont` tag hung on
 * its projectile: the assault rifle's yellow tracer, the sniper's grey
 * vapour trail, a plasma bolt's blue glow, the Scorpion shell's smoke. A
 * contrail EMITS POINTS where its object is, at the tag's rate, and each
 * point walks through the tag's point STATES -- each a width and a colour
 * held for a duration and then blended into the next over a transition --
 * until it reaches the last and dies. The ribbon joins the points, newest
 * at the object, and always faces the viewer.
 *
 * Two ways to drive one:
 *   - feed: something that really flies (a projectile) says where it is each
 *     frame; stop feeding and the trail finishes fading where it ended.
 *   - tracer: a hitscan round, which has no flight of its own, gets one
 *     here: a head moving from the muzzle to where the round landed at the
 *     projectile's own speed.
 *
 * Geometry: one mesh, a fixed number of trails per contrail type, each a
 * fixed run of quads rewritten every frame; dead quads collapse. Colour
 * and fade ride per vertex (see hta_gfx_dynamic.vertex_color).
 */
#ifndef HTA_CONTRAIL_H
#define HTA_CONTRAIL_H

#include "../asset/bsp.h"
#include "../asset/cache.h"
#include "../asset/bitmap.h"
#include "camera.h"

#define HTA_CONT_TYPES      12u
#define HTA_CONT_TRAILS     12u   /* per type */
#define HTA_CONT_POINTS     12u   /* per trail, head included */
#define HTA_CONT_STATES      4u
#define HTA_CONT_NONE       0xFFFFFFFFu

typedef struct {
    float duration, transition;   /* seconds */
    float width;                  /* world units */
    float color[4];               /* r g b a */
} hta_contrail_state;

typedef struct {
    uint32_t tag_id;
    uint32_t tex;
    bool     additive;
    float    rate;                /* points a second */
    float    life;                /* how long a point lives, all states through */
    float    repeats_u, repeats_v;
    hta_bitmap_sprite sprite;
    hta_contrail_state state[HTA_CONT_STATES];
    uint32_t state_count;
} hta_contrail_type;

typedef struct {
    float pos[3];
    float age;
} hta_contrail_point;

typedef struct {
    bool     used;
    bool     fed;                 /* the head is still being driven */
    uint32_t key;                 /* what feeds it: caller's own id */
    float    last_feed_age;       /* restarts the trail when a slot is reused */
    float    head[3];
    bool     have_head;
    /* A tracer moves its own head. */
    bool     tracer;
    float    from[3], dir[3], length, speed, travelled;
    float    accum;               /* toward the next point */
    hta_contrail_point pt[HTA_CONT_POINTS];  /* newest first */
    uint32_t count;
} hta_contrail;

typedef struct {
    hta_contrail_type type[HTA_CONT_TYPES];
    uint32_t          type_count;
    hta_contrail      trail[HTA_CONT_TYPES][HTA_CONT_TRAILS];
    hta_bsp_mesh      mesh;
    bool              loaded;
} hta_contrails;

void hta_contrails_init(hta_contrails *c);
void hta_contrails_free(hta_contrails *c);

/* A projectile's own contrail, from its object attachments, or HTA_CONT_NONE.
 * Registers the type the first time. Call every add before build. */
uint32_t hta_contrails_for_projectile(hta_contrails *c, const hta_cache *cache,
                                      const hta_resource_map *bitmaps, uint32_t proj_tag);
uint32_t hta_contrails_add(hta_contrails *c, const hta_cache *cache,
                           const hta_resource_map *bitmaps, uint32_t cont_tag);
bool hta_contrails_build(hta_contrails *c, char *err, size_t errlen);

/* Something flying reports where it is this frame. `key` is the caller's
 * name for it; `age` its time in flight, so a reused slot starts afresh. */
void hta_contrails_feed(hta_contrails *c, uint32_t type, uint32_t key,
                        const float pos[3], float age);
/* A hitscan round's trail, muzzle to impact at `speed` wu/s. */
void hta_contrails_tracer(hta_contrails *c, uint32_t type, const float from[3],
                          const float to[3], float speed);

/* Age the points, move the tracers, stop the trails nothing fed, and pose
 * the ribbons toward the camera. The caller re-uploads the vertices. */
void hta_contrails_update(hta_contrails *c, const hta_camera *cam, float dt);

/* Live trails, for tests. */
uint32_t hta_contrails_live(const hta_contrails *c);

#endif
