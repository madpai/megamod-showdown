/* World entity DEFINITIONS: the immutable, compiled half of a world's
 * generic behaviour (X1, docs/WORLD_ENTITIES.md). Read from an OALMAP v3
 * manifest's "world_entities" section, or built by a test in C.
 *
 * Five generic kinds, a closed vocabulary of events and inputs, and links
 * between placements. Nothing here is Source's or Halo's: an importer
 * translates its own classes into these (Open Asset Lab), the runtime
 * (engine/world_entities.h) implements them.
 *
 *   kind          emits      accepts
 *   interactable  used       -
 *   relay         fired      activate
 *   mover         -          open close toggle
 *   trigger       entered    -
 *   teleport      -          teleport (moves the player who began the chain)
 *   prop          -          -  (X5: a placed model; solid as its bounds)
 *
 * A placement's authored ID is `namespace:entity/name` (the content-ID
 * grammar, Open Asset Lab docs/CONTENT_IDS.md). It is load-time only: links
 * are resolved to entity indices here, once, and the runtime turns those
 * into generation-checked handles. Nothing compares IDs during play.
 *
 * Mover definitions (X2, schema 2): a mover's shared, immutable behaviour
 * -- the size of its box, how far and which way it slides, how fast -- is
 * a `hta_wmover_def` with its own ID `namespace:mover/name`, and any
 * number of placed movers reference one. A placement keeps only where it
 * stands (`pos`, the closed box's centre) and its links; the reference is
 * resolved to a definition index here, once. Schema 1 (X1) movers carry
 * their parameters inline; the parser gives each an unnamed definition of
 * its own, so the runtime has one path.
 *
 * Scripts (X3, schema 3): host-side Lua gameplay scripts are content here
 * too -- `namespace:script/name`, the API they were written for
 * (`megamod.v1`), the callbacks they declare and their source text, kept
 * in a bounded pool. An interactable may name a script (its `on_used`
 * runs instead of nothing when used; its links still fire), and the world
 * may name one `ability_script` (its `on_ability` answers a player's
 * ability press). References resolve to indices here, once. What a script
 * may do is script/script.h's business; this file only holds the text.
 *
 * Resource identity (X4, asset/resource.h): every ID above is parsed by
 * the one grammar, and every reference (link target, definition, script,
 * ability_script) is resolved through one typed resolver, once, into the
 * index the runtime keeps. A world that declares a package (asset/
 * package.h) may also import scripts from the library packages it
 * requires: they are copied into this table at load, marked with their
 * provider, and run exactly as the world's own.
 *
 * Asset references (X5, schema 4, asset/asset_res.h): a PROP places a
 * model (`namespace:model/name`) where it stands, drawn with the model's
 * materials and solid as the model's bounds; a mover definition may name
 * the sound (`namespace:sound/name`) it makes when it starts to move. Both
 * are imported from library packages and resolved here, once, to asset
 * table indices.
 *
 * Prefab instances (X6, schema 5, asset/prefab.h): a world may place
 * instances of prefabs it imports from libraries ("prefab_instances"). Each
 * EXPANDS here, once, into ordinary placed entities -- after the world's
 * own, instances in canonical instance-ID order, each one's children in
 * canonical local-ID order -- named <world ns>:entity/<instance>__<child>,
 * their parameters transformed to world space, their references already
 * resolved from the prefab's own package. Nothing after this file knows an
 * entity came from a prefab; `instance` below is kept for introspection.
 *
 * Portable C11. The definitions are bounded by the limits below, which
 * Open Asset Lab's validator shares (assetlab/world.py); parsing allocates
 * only transiently (a package set). */
#ifndef HTA_WORLD_DEF_H
#define HTA_WORLD_DEF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_WDEF_SCHEMA 5u            /* newest understood; 1 (X1) .. 4 (X5) still load */
#define HTA_WDEF_MAX_MOVER_DEFS 64u   /* one per mover at most (schema 1) */
#define HTA_WDEF_NO_DEF 0xFFFFu
#define HTA_WDEF_MAX_SCRIPTS 16u
#define HTA_WDEF_SCRIPT_POOL (64u * 1024u)  /* all scripts' source, bytes */
#define HTA_WDEF_SCRIPT_MAX_BYTES (32u * 1024u)  /* one script's source */
#define HTA_WDEF_SCRIPT_API "megamod.v1"
/* Callbacks a script may declare (script/script.h implements them). */
enum { HTA_WCB_ON_USED = 1u, HTA_WCB_ON_ABILITY = 2u };
#define HTA_WDEF_MAX_ENTITIES 64u
#define HTA_WDEF_MAX_LINKS_PER 8u
#define HTA_WDEF_MAX_LINKS 256u
#define HTA_WDEF_MAX_CHAIN 16u        /* links from a root event to its last consequence */
#define HTA_WDEF_ID_MAX 96u
#define HTA_WDEF_MAX_REACH 4.0f       /* wu */
#define HTA_WDEF_MAX_MOVE 64.0f       /* wu */
#define HTA_WDEF_MAX_SPEED 64.0f      /* wu/s */
#define HTA_WDEF_WORLD_LIMIT 4096.0f  /* |coordinate| */

typedef enum {
    HTA_WDEF_NONE = 0, HTA_WDEF_INTERACTABLE, HTA_WDEF_RELAY, HTA_WDEF_MOVER,
    HTA_WDEF_TRIGGER, HTA_WDEF_TELEPORT, HTA_WDEF_PROP, HTA_WDEF_KIND_COUNT
} hta_wdef_kind;

/* What a source emits (a link's `event`). */
typedef enum {
    HTA_WEV_NONE = 0, HTA_WEV_USED, HTA_WEV_FIRED, HTA_WEV_ENTERED, HTA_WEV_COUNT
} hta_wdef_event;

/* What a target is told to do (a link's `input`). */
typedef enum {
    HTA_WIN_NONE = 0, HTA_WIN_ACTIVATE, HTA_WIN_OPEN, HTA_WIN_CLOSE, HTA_WIN_TOGGLE,
    HTA_WIN_TELEPORT, HTA_WIN_COUNT
} hta_wdef_input;

typedef struct {
    uint8_t  event, input;    /* hta_wdef_event, hta_wdef_input */
    uint16_t target;          /* entity index, resolved from its placed ID at load */
} hta_wdef_link;

/* A reusable mover: shared by every placement that names it. */
typedef struct {
    char     id[HTA_WDEF_ID_MAX + 1];  /* namespace:mover/name; "" for an X1 inline mover */
    float    size[3];         /* the box's extent (wu); centred on the placement */
    float    move[3];         /* offset when fully open */
    float    speed;           /* wu/s */
    uint16_t sound;           /* X5: the sound it makes starting to move: asset table index + 1; 0 none */
    bool     generated;       /* X6: a prefab mover child's own (unnamed); `move` already in world axes */
} hta_wmover_def;

/* A script: immutable content. Its source is `len` bytes at `at` in the
 * pool (not NUL-terminated there). */
typedef struct {
    char     id[HTA_WDEF_ID_MAX + 1];  /* namespace:script/name */
    uint32_t callbacks;       /* HTA_WCB_* it declares */
    uint32_t at, len;
    uint8_t  provider;        /* 0: the world's own; k: imported from the set's k-th dependency (X4) */
} hta_wscript_def;

typedef struct {
    char     id[HTA_WDEF_ID_MAX + 1];
    uint8_t  kind;            /* hta_wdef_kind */
    uint8_t  link_count;
    uint16_t first_link;
    uint16_t def;             /* mover: its definition's index; else HTA_WDEF_NO_DEF */
    uint16_t script;          /* interactable: its script's index + 1; 0 none */
    float    pos[3];          /* interactable: where it is used; teleport: destination;
                                 mover: its box's centre when closed */
    float    reach;           /* interactable: from the user's eye, wu */
    float    yaw;             /* teleport: facing on arrival, radians */
    float    min[3], max[3];  /* trigger: its volume; prop: its model's bounds where it stands
                                 (world axes; for a rotated prop, around its oriented box) */
    uint16_t model;           /* prop (X5): its model's asset table index + 1;
                                 mover (X6, prefab children): the model that draws it, 0 none */
    /* X6: a placement transform (asset/prefab.h) -- props and prefab
     * movers. `xform` false: identity, the X5 behaviour, bit for bit. */
    bool     xform;
    float    rot_c, rot_s;    /* cos, sin of its yaw (about +z) */
    float    scale;           /* uniform */
    float    box_c[3], box_h[3];   /* prop with xform: its oriented box -- world centre, half extents (scaled) */
    uint8_t  instance;        /* X6: its prefab instance's index + 1; 0: the world's own */
    uint8_t  child;           /* ... and its local child index there */
} hta_wdef;

/* X6: a prefab instance as the world placed it -- kept only to say what
 * expanded to what (megamod-resources, logs); play never reads it. */
typedef struct {
    char     id[24];                   /* instance ID (a local ID) */
    char     prefab[HTA_WDEF_ID_MAX + 1];
    uint8_t  provider;                 /* the set's dependency index + 1 providing the prefab */
    uint16_t first, count;             /* the entities it expanded to */
    float    pos[3], yaw_deg, scale;
} hta_wprefab_instance;
#define HTA_WDEF_MAX_INSTANCES 32u

typedef struct hta_world_defs {
    hta_wdef      entity[HTA_WDEF_MAX_ENTITIES];
    uint32_t      count;
    hta_wdef_link link[HTA_WDEF_MAX_LINKS];
    uint32_t      link_count;
    hta_wmover_def mover_def[HTA_WDEF_MAX_MOVER_DEFS];
    uint32_t      mover_def_count;
    uint32_t      schema;     /* the section's schema (1, 2 or 3) */
    hta_wscript_def script[HTA_WDEF_MAX_SCRIPTS];
    uint32_t      script_count;
    uint16_t      ability_script;   /* the on_ability script's index + 1; 0 none */
    uint32_t      pool_used;
    /* X5: how many models and sounds the world's asset table holds
     * (external_map.h), so a reference can be checked without it. */
    uint32_t      asset_models, asset_sounds;
    hta_wprefab_instance prefab_instance[HTA_WDEF_MAX_INSTANCES];
    uint32_t      prefab_instance_count;
    char          pool[HTA_WDEF_SCRIPT_POOL];
} hta_world_defs;

/* The "world_entities" section of an OALMAP manifest (canonical JSON). The
 * manifest is walked key by key; any other key is skipped whole. No section:
 * true with count 0. A malformed, over-limit or invalid section (see
 * hta_world_defs_check): false, with `err` naming the placement. */
bool hta_world_defs_parse(const uint8_t *manifest, size_t len, hta_world_defs *out,
                          char *err, size_t errlen);

struct hta_pkg_set_s;
/* The same, resolving against a loaded package set (asset/package.h): the
 * world's own package is `set->root` (its declared provides are checked
 * against what the section defines) and imported scripts come from its
 * dependencies. hta_world_defs_parse is this with a set holding only the
 * manifest's own declaration (a world that requires packages is refused
 * there: it needs a package source). */
bool hta_world_defs_parse_env(const uint8_t *manifest, size_t len, const struct hta_pkg_set_s *set,
                              hta_world_defs *out, char *err, size_t errlen);

/* A library package's "scripts" (X4): parsed and checked like a world's
 * (ID, API, callbacks, source limits) into `out` (no entities); any
 * namespace but a reserved one. */
bool hta_world_defs_parse_library(const uint8_t *manifest, size_t len, hta_world_defs *out,
                                  char *err, size_t errlen);

/* Every rule Open Asset Lab applies, again: IDs well-formed and unique,
 * links resolved, events the source emits, inputs the target accepts, no
 * self-link or cycle, chains within HTA_WDEF_MAX_CHAIN, finite bounded
 * parameters, no destination inside a trigger; every mover names a valid
 * definition, every definition is well-formed. */
bool hta_world_defs_check(const hta_world_defs *d, char *err, size_t errlen);

/* Load-time lookup (never during play): the entity with this ID, or -1. */
int32_t hta_world_defs_find(const hta_world_defs *d, const char *id);
/* Load-time lookup: the mover definition with this ID, or -1. */
int32_t hta_world_defs_find_mover(const hta_world_defs *d, const char *id);
/* Load-time lookup: the script with this ID, or -1. */
int32_t hta_world_defs_find_script(const hta_world_defs *d, const char *id);
const char *hta_wscript_callback_name(uint32_t cb);
/* The mover definition a placed mover uses (NULL for any other kind). */
const hta_wmover_def *hta_wdef_mover(const hta_world_defs *d, uint32_t entity);
/* A placed mover's box when closed. */
void hta_wdef_mover_box(const hta_world_defs *d, uint32_t entity, float min[3], float max[3]);

/* Each top-level member of a canonical JSON manifest, in order: its key and
 * the exact bytes of its value. False when the manifest is malformed. (The
 * world key, external_map.c, hashes the members the runtime plays by.) */
typedef void (*hta_manifest_member_fn)(void *ctx, const char *key, const uint8_t *value, size_t len);
bool hta_manifest_members(const uint8_t *manifest, size_t len, hta_manifest_member_fn fn, void *ctx);

bool hta_wdef_emits(uint8_t kind, uint8_t event);
bool hta_wdef_accepts(uint8_t kind, uint8_t input);
const char *hta_wdef_kind_name(uint8_t kind);
const char *hta_wdef_event_name(uint8_t event);
const char *hta_wdef_input_name(uint8_t input);
/* An input by its name ("open"...), HTA_WIN_NONE when there is none. */
uint8_t hta_wdef_input_from_name(const char *name);

#endif
