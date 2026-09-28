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

#define HTA_WDEF_SCHEMA 7u            /* X9 adds authored atmosphere and lights */
#define HTA_WDEF_MAX_LIGHTS 32u
typedef struct {
    char id[24];                 /* local ID, canonical order */
    float position[3], color[3], direction[3];
    float intensity, range, inner_cos, outer_cos;
    uint16_t relay;             /* entity index + 1; 0: always enabled */
    bool spot;
} hta_wlight_def;
typedef struct {
    float ambient[3], clear[3], fog_color[3];
    float fog_density, fog_start;
} hta_wenvironment;
#define HTA_WDEF_MAX_MOVER_DEFS 256u  /* one per mover at most (schema 1, prefab movers): the movers' limit (X8) */
#define HTA_WDEF_NO_DEF 0xFFFFu
#define HTA_WDEF_MAX_SCRIPTS 16u
#define HTA_WDEF_SCRIPT_POOL (64u * 1024u)  /* all scripts' source, bytes */
#define HTA_WDEF_SCRIPT_MAX_BYTES (32u * 1024u)  /* one script's source */
#define HTA_WDEF_SCRIPT_API "megamod.v1"
/* Callbacks a script may declare (script/script.h implements them). */
enum { HTA_WCB_ON_USED = 1u, HTA_WCB_ON_ABILITY = 2u };
/* X8: runtime world objects -- every placed and expanded entity, whatever
 * its kind. 64 until X8, when WORLD_STATE named them by a 6-bit index; the
 * wire now names only replicated state (asset/world_repl.h), so this is a
 * memory and load-time bound, not a network one. Indices fit 16 bits. */
#define HTA_WDEF_MAX_ENTITIES 1024u
#define HTA_WDEF_MAX_LINKS_PER 8u
#define HTA_WDEF_MAX_LINKS 1024u
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

/* What a source emits. A link (X1) may listen to the first three, by their
 * X1 names (`used`, `fired`, `entered`); an event binding (X7) to all, by
 * the binding names (`used`, `activated`, `entered`, `deactivated`,
 * `opened`, `closed`) -- `fired` and `activated` are ONE engine event, a
 * relay's answer to `activate`. */
typedef enum {
    HTA_WEV_NONE = 0, HTA_WEV_USED, HTA_WEV_FIRED, HTA_WEV_ENTERED,
    HTA_WEV_LINK_COUNT,                       /* links end here */
    HTA_WEV_DEACTIVATED = HTA_WEV_LINK_COUNT, /* X7: a relay told to deactivate */
    HTA_WEV_OPENED,                           /* X7: a mover arrived open */
    HTA_WEV_CLOSED,                           /* X7: a mover arrived closed */
    HTA_WEV_COUNT
} hta_wdef_event;
#define HTA_WEV_ACTIVATED HTA_WEV_FIRED

/* What a target is told to do: a link's `input`, and (X7) the queue's
 * operation for an action. Links, Lua's world.send and prefab links name
 * only the first five (hta_wdef_input_from_name); `deactivate` is X7's, and
 * the last two are internal (an action's damage or sound, dispatched
 * through the same queue). */
typedef enum {
    HTA_WIN_NONE = 0, HTA_WIN_ACTIVATE, HTA_WIN_OPEN, HTA_WIN_CLOSE, HTA_WIN_TOGGLE,
    HTA_WIN_TELEPORT,
    HTA_WIN_LINK_COUNT,                       /* links, Lua end here */
    HTA_WIN_DEACTIVATE = HTA_WIN_LINK_COUNT,
    HTA_WIN_DAMAGE, HTA_WIN_SOUND,
    HTA_WIN_USE,                              /* X7: use an interactable -- a player's press, without the reach test */
    HTA_WIN_COUNT
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
#define HTA_WDEF_MAX_INSTANCES 128u   /* X8: 32 before; `instance` (index + 1) is a byte */

/* ---- X7: declarative event bindings (docs/EVENT_BINDINGS.md) ----------------
 *
 * EVENT (a source entity's) -> CONDITIONS (all must hold; read-only engine
 * predicates) -> ACTIONS (requests of existing engine capabilities, in
 * order, through the same bounded queue links and Lua use). A world lists
 * them in world_entities.bindings (schema 6); a prefab in its own
 * "bindings" (prefab schema 2), naming its children, expanded per instance.
 * Every reference is resolved to an index here, once. */
#define HTA_WDEF_MAX_BINDINGS      1024u  /* one world, after expansion (X8: 128 before) */
#define HTA_WDEF_MAX_CONDS_PER     4u     /* one binding */
#define HTA_WDEF_MAX_ACTIONS_PER   8u     /* one binding */
#define HTA_WDEF_MAX_CONDS         1024u  /* one world (X8: 256 before) */
#define HTA_WDEF_MAX_ACTIONS       2048u  /* one world (X8: 512 before) */
#define HTA_WDEF_MAX_BINDINGS_PER_EVENT 16u   /* one source's one event */
#define HTA_WDEF_MAX_DAMAGE        500.0f /* one damage action (Lua's game.damage limit) */
#define HTA_WDEF_BINDING_ID_MAX    23u    /* a binding's local ID (the prefab local-ID grammar) */

typedef enum { HTA_WCOND_NONE = 0, HTA_WCOND_MOVER_STATE, HTA_WCOND_RELAY_STATE, HTA_WCOND_COUNT } hta_wcond_kind;
/* relay_state values */
enum { HTA_WRELAY_INACTIVE = 0, HTA_WRELAY_ACTIVE = 1 };

typedef enum {
    HTA_WACT_NONE = 0, HTA_WACT_OPEN, HTA_WACT_CLOSE, HTA_WACT_TOGGLE, HTA_WACT_ACTIVATE, HTA_WACT_DEACTIVATE,
    HTA_WACT_TELEPORT, HTA_WACT_DAMAGE, HTA_WACT_PLAY_SOUND, HTA_WACT_USE, HTA_WACT_COUNT
} hta_wact_op;

typedef struct {
    uint8_t  kind;            /* hta_wcond_kind */
    uint8_t  value;           /* mover_state: HTA_MOVER_* phase (0 closed .. 3 closing); relay_state: HTA_WRELAY_* */
    uint16_t entity;          /* the entity it reads */
} hta_wcond;

typedef struct {
    uint8_t  op;              /* hta_wact_op */
    uint8_t  input;           /* the queue operation it becomes (hta_wdef_input) */
    uint16_t target;          /* open .. teleport: the entity told; else HTA_WDEF_NO_DEF */
    uint16_t sound;           /* play_sound: asset table index + 1 */
    uint16_t at;              /* play_sound: where it sounds (an entity); else HTA_WDEF_NO_DEF */
    float    amount;          /* damage */
} hta_waction;

typedef struct {
    char     id[HTA_WDEF_BINDING_ID_MAX + 1];  /* local ID: unique in its world, or in its prefab */
    uint16_t source;          /* entity index */
    uint8_t  event;           /* hta_wdef_event */
    uint8_t  cond_count, action_count;
    uint16_t first_cond, first_action;
    uint8_t  instance;        /* 0: the world's own; k: prefab instance k - 1's */
} hta_wbinding;
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
    /* X7: event bindings -- the world's own (canonical ID order), then each
     * prefab instance's (instances by ID, bindings by local ID). */
    hta_wbinding  binding[HTA_WDEF_MAX_BINDINGS];
    uint32_t      binding_count;
    hta_wcond     cond[HTA_WDEF_MAX_CONDS];
    uint32_t      cond_count;
    hta_waction   action[HTA_WDEF_MAX_ACTIONS];
    uint32_t      action_count;
    bool          has_environment;
    hta_wenvironment environment;
    hta_wlight_def light[HTA_WDEF_MAX_LIGHTS];
    uint32_t      light_count;
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

/* Links (X1): what a kind emits and accepts, by the X1 vocabulary. */
bool hta_wdef_emits(uint8_t kind, uint8_t event);
bool hta_wdef_accepts(uint8_t kind, uint8_t input);
const char *hta_wdef_kind_name(uint8_t kind);
const char *hta_wdef_event_name(uint8_t event);      /* the link name: used, fired, entered */
const char *hta_wdef_input_name(uint8_t input);
/* A LINK input by its name ("open"...): the X1 five only (links, prefab
 * links, Lua's world.send); HTA_WIN_NONE otherwise. */
uint8_t hta_wdef_input_from_name(const char *name);

/* ---- X7 vocabulary: one table each, read by the parser, the checks, the
 * runtime and megamod-resources (so the contract cannot drift). ---------- */
typedef enum { HTA_WACTOR_ALWAYS = 0, HTA_WACTOR_CHAIN, HTA_WACTOR_NEVER } hta_wactor_rule;
typedef struct {
    const char *name;         /* binding name */
    uint8_t     event;        /* hta_wdef_event */
    uint32_t    sources;      /* bit per hta_wdef_kind that emits it */
    uint8_t     actor;        /* hta_wactor_rule: does the event carry a player */
    const char *when;         /* the engine transition, in words */
} hta_wevent_info;
typedef struct {
    const char *name;
    uint32_t    kinds;        /* bit per hta_wdef_kind it reads */
    const char *const *values;/* the `is` vocabulary, index = value */
    uint8_t     value_count;
    const char *doc;
} hta_wcond_info;
enum { HTA_WARG_TARGET = 1u, HTA_WARG_AMOUNT = 2u, HTA_WARG_SOUND = 4u, HTA_WARG_AT = 8u };
typedef struct {
    const char *name;
    uint8_t     input;        /* the queue operation */
    uint32_t    targets;      /* bit per kind `target` may name; 0: no target */
    uint32_t    needs, takes; /* HTA_WARG_* */
    bool        needs_actor;  /* acts on the event's actor */
    const char *path;         /* the engine path it reuses, in words */
} hta_waction_info;
const hta_wevent_info *hta_wevent_get(uint8_t event);          /* by hta_wdef_event */
const hta_wevent_info *hta_wevent_by_name(const char *name);   /* binding names; NULL unknown */
const hta_wcond_info *hta_wcond_get(uint8_t kind);
const hta_waction_info *hta_waction_get(uint8_t op);
uint8_t hta_waction_from_name(const char *name);
uint8_t hta_wcond_from_name(const char *name);
/* A kind's capabilities (its affordances), from the same tables: does it
 * afford action `op` as a target. What a future agent may discover. */
bool hta_wdef_affords(uint8_t kind, uint8_t op);
/* Does a kind emit this event to a binding / sit where a sound can play. */
bool hta_wbind_emits(uint8_t kind, uint8_t event);
bool hta_wdef_positioned(uint8_t kind);
/* One binding's rules against kinds (the world's or a prefab's): the source
 * emits the event, each condition reads a kind it applies to, each action's
 * target is a kind that affords it, a sound's `at` has a position, an actor
 * action is not bound to an event that never carries one. `name(ctx, i)`
 * names entity i in the message ("x7:entity/north_crate", "child 'door'"). */
typedef const char *(*hta_wbind_name_fn)(const void *ctx, uint16_t entity);
typedef uint8_t (*hta_wbind_kind_fn)(const void *ctx, uint16_t entity);
/* One binding object as written (a world's or a prefab's): names checked
 * against the vocabulary, the fields each action takes and needs, counts;
 * references kept as text for the caller to resolve (entity IDs in a world,
 * local child IDs in a prefab). `who` prefixes messages ("binding" or
 * "prefab x7:prefab/door binding"). */
typedef struct {
    char        id[64];
    uint8_t     event;
    char        source[HTA_WDEF_ID_MAX + 1];
    uint32_t    cond_count, action_count;
    hta_wcond   cond[HTA_WDEF_MAX_CONDS_PER];
    char        cond_entity[HTA_WDEF_MAX_CONDS_PER][HTA_WDEF_ID_MAX + 1];
    hta_waction action[HTA_WDEF_MAX_ACTIONS_PER];
    char        target[HTA_WDEF_MAX_ACTIONS_PER][HTA_WDEF_ID_MAX + 1];
    char        at[HTA_WDEF_MAX_ACTIONS_PER][HTA_WDEF_ID_MAX + 1];
    char        sound[HTA_WDEF_MAX_ACTIONS_PER][HTA_WDEF_ID_MAX + 1];
} hta_wbind_text;
struct hta_mj_s;
bool hta_wbind_parse_text(struct hta_mj_s *r, hta_wbind_text *t, const char *who, char *err, size_t errlen);

bool hta_wbind_check_one(const char *who, uint16_t source, uint8_t event, const hta_wcond *cond, uint32_t cond_count,
                         const hta_waction *act, uint32_t act_count, uint32_t entity_count, hta_wbind_kind_fn kind,
                         hta_wbind_name_fn name, const void *ctx, uint32_t sounds, char *err, size_t errlen);

#endif
