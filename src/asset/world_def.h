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
 *
 * A placement's authored ID is `namespace:entity/name` (the content-ID
 * grammar, Open Asset Lab docs/CONTENT_IDS.md). It is load-time only: links
 * are resolved to entity indices here, once, and the runtime turns those
 * into generation-checked handles. Nothing compares IDs during play.
 *
 * Portable C11, no allocation: everything is bounded by the limits below,
 * which Open Asset Lab's validator shares (assetlab/world.py). */
#ifndef HTA_WORLD_DEF_H
#define HTA_WORLD_DEF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_WDEF_SCHEMA 1u
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
    HTA_WDEF_TRIGGER, HTA_WDEF_TELEPORT, HTA_WDEF_KIND_COUNT
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

typedef struct {
    char     id[HTA_WDEF_ID_MAX + 1];
    uint8_t  kind;            /* hta_wdef_kind */
    uint8_t  link_count;
    uint16_t first_link;
    float    pos[3];          /* interactable: where it is used; teleport: destination */
    float    reach;           /* interactable: from the user's eye, wu */
    float    yaw;             /* teleport: facing on arrival, radians */
    float    min[3], max[3];  /* trigger: its volume; mover: its box when closed */
    float    move[3];         /* mover: offset when fully open */
    float    speed;           /* mover: wu/s */
} hta_wdef;

typedef struct {
    hta_wdef      entity[HTA_WDEF_MAX_ENTITIES];
    uint32_t      count;
    hta_wdef_link link[HTA_WDEF_MAX_LINKS];
    uint32_t      link_count;
} hta_world_defs;

/* The "world_entities" section of an OALMAP manifest (canonical JSON). The
 * manifest is walked key by key; any other key is skipped whole. No section:
 * true with count 0. A malformed, over-limit or invalid section (see
 * hta_world_defs_check): false, with `err` naming the placement. */
bool hta_world_defs_parse(const uint8_t *manifest, size_t len, hta_world_defs *out,
                          char *err, size_t errlen);

/* Every rule Open Asset Lab applies, again: IDs well-formed and unique,
 * links resolved, events the source emits, inputs the target accepts, no
 * self-link or cycle, chains within HTA_WDEF_MAX_CHAIN, finite bounded
 * parameters, no destination inside a trigger. */
bool hta_world_defs_check(const hta_world_defs *d, char *err, size_t errlen);

/* Load-time lookup (never during play): the entity with this ID, or -1. */
int32_t hta_world_defs_find(const hta_world_defs *d, const char *id);

bool hta_wdef_emits(uint8_t kind, uint8_t event);
bool hta_wdef_accepts(uint8_t kind, uint8_t input);
const char *hta_wdef_kind_name(uint8_t kind);
const char *hta_wdef_event_name(uint8_t event);
const char *hta_wdef_input_name(uint8_t input);

#endif
