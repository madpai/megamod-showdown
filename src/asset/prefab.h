/* Prefabs (X6, docs/PREFABS.md): reusable, declarative compositions of
 * existing resources and world-entity kinds, provided by LIBRARY packages
 * and placed by worlds as instances that EXPAND, at load, into ordinary
 * placed entities. A prefab is an authoring and world-construction
 * abstraction, not a second runtime: after expansion nothing in play,
 * networking, Lua or collision knows an entity came from one.
 *
 * A library declares its prefabs in its manifest's "prefabs" member
 * (canonical JSON, keys sorted, prefabs in canonical ID order, children in
 * canonical local-ID order):
 *
 *   "prefabs": {"schema": 1, "prefabs": [
 *     {"id": "x6:prefab/security_door", "children": [
 *       {"id": "button", "kind": "interactable", "links": [{"event": "used", "input": "toggle", "target": "door"}],
 *        "position": [-0.16, -0.7, 0.9], "reach": 1.2, "script": "x6:script/security_door_log"},
 *       {"id": "door", "kind": "mover", "links": [], "model": "x6shared:model/door_panel", "move": [0, 1.3, 0],
 *        "position": [0, 0, 0.6], "size": [0.1, 1.2, 1.2], "sound": "x6shared:sound/door_hiss", "speed": 1.2},
 *       {"id": "frame_left", "kind": "prop", "links": [], "model": "x6shared:model/frame_post", "position": [0, -0.7, 0.8]}]}]}
 *
 * CHILD KINDS are the world's own (world_def.h): interactable, relay,
 * mover, trigger, teleport, prop -- with the same parameters, in PREFAB
 * SPACE (+z up, x forward, the instance's origin at 0). A mover carries its
 * size, move and speed inline (it becomes its own mover definition when
 * expanded) and may name a MODEL that draws it. Links name SIBLINGS by
 * local ID; nothing in a prefab can name an entity outside it.
 *
 * REFERENCES (model, sound, script) are typed resource references resolved
 * through hta_res_resolve from the PROVIDER'S point of view -- its own
 * resources and what it imports -- once, when the package set loads
 * (package.c). A world that places the prefab imports only the prefab.
 *
 * NESTING is not supported in schema 1: a child of kind "prefab", or any
 * child naming a prefab, is refused. There is no inheritance and no
 * override: an instance is the prefab plus an identity and a transform.
 *
 * IDENTITY. A prefab is a RESOURCE: namespace:prefab/name. An instance is a
 * PLACEMENT in one world, named by an instance ID; a child by its local ID.
 * Both are LOCAL IDs: [a-z][a-z0-9]*(_[a-z0-9]+)* -- lowercase segments with
 * no "__" and no trailing "_" -- at most HTA_PREFAB_LOCAL_MAX bytes. A child
 * of an instance becomes the ordinary placed entity
 *
 *     <world namespace>:entity/<instance>__<child>      x6:entity/north_door__button
 *
 * which is injective (neither part may hold "__", so the split is unique),
 * within the grammar's 48-byte name (23 + 2 + 23), and cannot collide with
 * an authored entity: a schema 5 world's own placed IDs may not hold "__".
 *
 * Portable C11; bounded; allocation only for a library's compiled table. */
#ifndef HTA_PREFAB_H
#define HTA_PREFAB_H

#include "resource.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_PREFAB_SCHEMA          1u
#define HTA_PREFAB_MAX_PER_LIBRARY 32u
#define HTA_PREFAB_MAX_CHILDREN    16u
#define HTA_PREFAB_MAX_LINKS       64u      /* one prefab, all its children */
#define HTA_PREFAB_MAX_LINKS_PER   8u       /* one child (the world's per-entity limit) */
#define HTA_PREFAB_LOCAL_MAX       23u      /* bytes in a child's local ID, and an instance ID */
#define HTA_PREFAB_MAX_INSTANCES   32u      /* one world */
#define HTA_PREFAB_MAX_LOCAL       256.0f   /* |a child's position| in prefab space, wu */
#define HTA_PREFAB_SCALE_MIN       0.25f    /* an instance's uniform scale */
#define HTA_PREFAB_SCALE_MAX       4.0f
#define HTA_PREFAB_MAX_YAW         360.0f   /* |yaw_degrees| */
#define HTA_PREFAB_SEP             "__"

/* A link between siblings: `target` is a local child index. */
typedef struct { uint8_t event, input; uint16_t target; } hta_prefab_link;

typedef struct {
    char     id[HTA_PREFAB_LOCAL_MAX + 1];
    uint8_t  kind;                 /* hta_wdef_kind */
    uint8_t  link_count;
    uint16_t first_link;
    float    pos[3];               /* prefab space */
    float    yaw_deg;              /* mover, prop: its own rotation; teleport: facing on arrival */
    float    reach;                /* interactable */
    float    size[3], move[3], speed;   /* mover */
    float    min[3], max[3];       /* trigger, prefab space */
    /* Typed references, as written, then (package.c) resolved: */
    char     model_ref[HTA_RID_MAX + 1], sound_ref[HTA_RID_MAX + 1], script_ref[HTA_RID_MAX + 1];
    uint16_t model, sound;         /* the set's combined asset index + 1; 0 none */
    uint8_t  script_dep;           /* the set's dependency index + 1 providing the script; 0 none */
    uint16_t script;               /* its index in that library's scripts */
} hta_prefab_child;

typedef struct {
    char             id[HTA_RID_MAX + 1];   /* namespace:prefab/name */
    hta_prefab_child child[HTA_PREFAB_MAX_CHILDREN];
    uint32_t         child_count;
    hta_prefab_link  link[HTA_PREFAB_MAX_LINKS];
    uint32_t         link_count;
} hta_prefab;

/* One library's compiled prefabs (hta_pkg_dep.prefabs). */
typedef struct hta_prefab_table {
    hta_prefab *prefab;            /* owned */
    uint32_t    count;
} hta_prefab_table;

/* The "prefabs" member of a library manifest, strictly: schema, prefab IDs
 * (valid, not in a reserved namespace, canonical order, each once), each
 * child's local ID (canonical order, each once), kind (nesting refused),
 * fields (exactly the kind's), numbers (finite, bounded), links (siblings
 * only, the source emits the event, the target accepts the input, no
 * self-link, no cycle, chains within the world's limit). References stay
 * unresolved (package.c). No member: true with an empty table. `pkg`
 * names the package in messages. */
bool hta_prefab_parse(const uint8_t *manifest, size_t len, const char *pkg, hta_prefab_table *out, bool *declared,
                      char *err, size_t errlen);
void hta_prefab_table_free(hta_prefab_table *t);
/* Local index of prefab `id`, or -1. */
int32_t hta_prefab_find(const hta_prefab_table *t, const char *id);

/* A local ID (a child's, `what` "local child id"; an instance's, "instance
 * id"): true, or false with why ("has '__' (reserved: instance__child)"). */
bool hta_prefab_local_valid(const char *id, const char *what, char *why, size_t n);

/* ---- transforms ------------------------------------------------------------ */

/* A placement transform: translation, rotation about +z (yaw, degrees,
 * counter-clockwise seen from above: +x turns toward +y), uniform scale.
 * Applied to a point p in the placed thing's space:
 *     world = pos + scale * Rz(yaw) * p
 * A child's final transform under an instance I:
 *     pos   = I.pos + I.scale * Rz(I.yaw) * child.pos
 *     yaw   = I.yaw + child.yaw
 *     scale = I.scale
 * Right-handed, +z up (the engine's world). */
typedef struct { float pos[3]; float yaw_deg; float scale; } hta_xform;

/* cos and sin of `deg` degrees: exact (0, 1, -1) at every multiple of 90,
 * so axis-aligned placements are bit-exact on every platform; otherwise
 * computed in double from the canonical value and rounded to float. */
void hta_yaw_cossin(double deg, float *c, float *s);
/* Is `deg` a whole multiple of 90 (a rotation that keeps boxes axis-aligned)? */
bool hta_yaw_axis_aligned(double deg);
/* world = pos + scale * R * p. */
void hta_xform_point(const hta_xform *x, float c, float s, const float p[3], float out[3]);
/* The vector part: scale * R * v. */
void hta_xform_vector(float scale, float c, float s, const float v[3], float out[3]);

#endif
