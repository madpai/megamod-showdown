/* Model animations (`antr`) from Trial tags.
 *
 * A first-person weapon graph is the *merged* skeleton for two meshes: the
 * hands (`matg` -> first person interface -> first person hands) and the
 * weapon's own FP model (`weap+0x45C`). Blood Gulch's assault rifle graph has
 * 42 nodes = 37 hand nodes + 5 gun nodes, and `frame gun` is parented to
 * `frame r wriste`. That parenting is what puts the gun in the right hand --
 * there is no hold offset to guess, and the tag's own "first person offset"
 * (trigger+136) is (0,0,0).
 *
 * Frame encoding: three 64-bit flag sets say, per node, whether rotation /
 * transform / scale vary per frame. Varying components are packed per frame in
 * node order (rotation 8B, transform 12B, scale 4B); the rest are stored once
 * in default data, in the same order. So
 *     default_size + frame_size == node_count * 24
 * holds for every uncompressed animation, which is the check that catches a
 * mis-ordered flag field.
 */
#ifndef HTA_ANIM_H
#define HTA_ANIM_H

#include "cache.h"

#define HTA_TAG_ANTR HTA_FOURCC('a','n','t','r')

/* Flag data is two u32s, so Halo itself cannot animate more than 64 nodes. */
#define HTA_ANIM_MAX_NODES 64
#define HTA_ANIM_MAX_SOUNDS 16

/* ---- ModelAnimations ---- */
#define HTA_ANTR_SOUND_REFS   0x054u
#define HTA_ANTR_NODES        0x068u
#define HTA_ANTR_ANIMATIONS   0x074u
#define HTA_ANTR_SNDREF_SIZE  20u
#define HTA_ANTR_NODE_SIZE    64u
#define HTA_ANTR_NODE_PARENT  36u
#define HTA_ANTR_ANIM_SIZE    180u

/* ---- ModelAnimationsAnimation ---- */
#define HTA_ANIM_TYPE         0x20u
#define HTA_ANIM_FRAME_COUNT  0x22u
#define HTA_ANIM_FRAME_SIZE   0x24u
#define HTA_ANIM_NODE_COUNT   0x2Cu
#define HTA_ANIM_LOOP_FRAME   0x2Eu
#define HTA_ANIM_KEY_FRAME    0x34u
#define HTA_ANIM_NEXT         0x38u
#define HTA_ANIM_FLAGS        0x3Au
#define HTA_ANIM_SOUND        0x3Cu
#define HTA_ANIM_SOUND_FRAME  0x3Eu
/* Transform flags come BEFORE rotation flags. Swapping the two still parses --
 * it just silently trades 8-byte and 12-byte reads -- so the size invariant
 * above is the only thing that catches it. */
#define HTA_ANIM_TRANS_FLAGS  0x5Cu
#define HTA_ANIM_ROT_FLAGS    0x6Cu
#define HTA_ANIM_SCALE_FLAGS  0x7Cu
#define HTA_ANIM_DEFAULT_DATA 0x8Cu  /* TagDataOffset */
#define HTA_ANIM_FRAME_DATA   0xA0u  /* TagDataOffset */

#define HTA_ANIM_FLAG_COMPRESSED 0x1u

#define HTA_ANIM_ROT_SIZE    8u   /* 4 x int16, /32767 */
#define HTA_ANIM_TRANS_SIZE  12u  /* 3 x float */
#define HTA_ANIM_SCALE_SIZE  4u   /* float */
#define HTA_ANIM_NODE_STRIDE (HTA_ANIM_ROT_SIZE + HTA_ANIM_TRANS_SIZE + HTA_ANIM_SCALE_SIZE)

/* Halo ticks at 30 Hz and animation frames are ticks. */
#define HTA_ANIM_FPS 30.0f

/* Rotation, translation, uniform scale. Composes without matrices, which is
 * how Halo stores it and keeps the skinning path allocation-free. */
typedef struct {
    float q[4];  /* x y z w */
    float t[3];
    float s;
} hta_transform;

typedef struct {
    char    name[32];
    int16_t parent;   /* < 0, or == own index, means root */
} hta_anim_node;

typedef struct {
    char     name[32];
    uint16_t type;         /* 0 base, 1 overlay, 2 replacement */
    uint16_t frame_count;
    uint16_t frame_size;   /* bytes per frame */
    uint16_t node_count;
    int16_t  loop_frame, key_frame, next_anim;
    int16_t  sound_index, sound_frame;
    uint16_t flags;
    uint32_t rot_flags[2], trans_flags[2], scale_flags[2];
    uint32_t default_off, default_size;  /* file offsets; 0 = absent */
    uint32_t frame_off, frame_bytes;
} hta_animation;

typedef struct {
    const hta_cache *cache;
    uint32_t         tag_id;
    char             path[96];
    hta_anim_node    nodes[HTA_ANIM_MAX_NODES];
    uint32_t         node_count;
    hta_animation   *anims;
    uint32_t         anim_count;
    uint32_t         sound_ids[HTA_ANIM_MAX_SOUNDS];
    uint32_t         sound_count;
} hta_anim_graph;

bool hta_anim_load(hta_anim_graph *g, const hta_cache *c, uint32_t antr_tag_id,
                   char *err, size_t errlen);
void hta_anim_free(hta_anim_graph *g);

/* Case-insensitive substring match on the animation name; -1 if absent. */
int32_t hta_anim_find(const hta_anim_graph *g, const char *needle);
/* Exact node-name match; -1 if absent. */
int32_t hta_anim_node_index(const hta_anim_graph *g, const char *name);

/* Does this animation keyframe that node?
 *
 * Halo stores one bit per node per channel, and an OVERLAY animation only
 * writes the handful of nodes it moves -- the needler's `first-person
 * ammunition` keyframes its sixteen needle bones and nothing else. Every
 * other node in a sampled overlay carries the animation's own default, not
 * the pose you want to keep, so a caller composing an overlay onto a base
 * clip must take only the nodes this returns true for. */
bool hta_anim_animates(const hta_anim_graph *g, uint32_t anim, uint32_t node);

/* Parent-relative pose of every node at `frame` (fractional frames are
 * interpolated). `out` needs g->node_count entries. Refuses compressed
 * animations -- the Trial's FP graphs have none. */
bool hta_anim_sample(const hta_anim_graph *g, uint32_t anim, float frame,
                     hta_transform *out);

/* Parent-relative -> graph space. `local` and `out` both hold node_count. */
void hta_anim_world(const hta_anim_graph *g, const hta_transform *local,
                    hta_transform *out);

/* ---- transform algebra ---- */
void hta_xf_identity(hta_transform *x);
void hta_xf_mul(hta_transform *o, const hta_transform *a, const hta_transform *b); /* o = a . b */
void hta_xf_inverse(hta_transform *o, const hta_transform *a);
void hta_xf_point(float o[3], const hta_transform *x, const float v[3]);
void hta_xf_vector(float o[3], const hta_transform *x, const float v[3]);
void hta_xf_lerp(hta_transform *o, const hta_transform *a, const hta_transform *b, float t);

#endif
