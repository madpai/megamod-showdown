#ifndef HTA_NET_PROTOCOL_H
#define HTA_NET_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_NET_MAGIC 0x31415448u /* "HTA1" on the wire */
/* v10: HELLO carries the content fingerprint (app/compat.h).
 * v11 (X8): WORLD_STATE carries replicated world state by replication index
 * (movers' spatial state, relays' logical flags) instead of a 6-bit entity
 * index, and the FX world sound names a 16-bit runtime object. v10 and v11
 * refuse each other (docs/WORLD_STATE.md "Protocol v11"). */
#define HTA_NET_VERSION 14u
#define HTA_NET_HEADER 20u
#define HTA_NET_MAX_PACKET 1200u
#define HTA_NET_MAX_PLAYERS 8u
#define HTA_NET_PLAYER_BYTES 35u
#define HTA_NET_MAX_ENTITIES 16u
#define HTA_NET_ENTITY_NAME 12u
#define HTA_NET_ENTITY_BYTES 68u
#define HTA_NET_WORLD_HEADER 86u
#define HTA_NET_CONTROL_BYTES 39u
#define HTA_NET_KILL_BYTES 128u
#define HTA_NET_FX_BYTES 29u    /* v11: the entity is 16 bits */
#define HTA_NET_PROJECTILE_BYTES 30u
#define HTA_NET_MAX_PROJECTILES 32u
/* Projectile pools: the match's own first, then the host's first-person
 * weapon and its grenades. */
#define HTA_NET_MAX_POOLS 16u
#define HTA_NET_POOL_HOST_WEAPON 12u
#define HTA_NET_POOL_HOST_GRENADES 13u
/* Roster entries an FX may name: carried weapons and vehicle triggers. */
#define HTA_NET_MAX_WEAPONS 64u
#define HTA_NET_MAX_VEHICLES 32u
#define HTA_NET_VEHICLE_SEATS 6u
#define HTA_NET_VEHICLE_BYTES 36u

typedef enum {
    HTA_NET_HELLO = 1, HTA_NET_WELCOME, HTA_NET_DISCONNECT,
    HTA_NET_PING, HTA_NET_PONG, HTA_NET_INPUT, HTA_NET_SNAPSHOT,
    HTA_NET_EVENT,
    /* Anyone may ask a server what it is; the answer needs no session.
     * This is how a LAN lobby finds games without typing an address. */
    HTA_NET_DISCOVER, HTA_NET_INFO, HTA_NET_WORLD, HTA_NET_CONTROL,
    HTA_NET_KILL, HTA_NET_ACK, HTA_NET_FX, HTA_NET_PROJECTILES,
    HTA_NET_REJECT, HTA_NET_VEHICLES, HTA_NET_DROPS,
    /* v4: the rules -- mode, team scores, the flags, vehicle hulls. */
    HTA_NET_GAME,
    /* The world's replicated state, host -> clients: movers and (v11)
     * relays. X1 added it within v10; X8 changed its layout, which is why
     * v11 exists (docs/WORLD_STATE.md). */
    HTA_NET_WORLD_STATE, HTA_NET_RPG
} hta_net_type;

typedef struct {
    uint8_t type;
    uint32_t sequence, tick;
    const uint8_t *payload;
    uint16_t length;
} hta_net_packet;

typedef struct {
    uint8_t id, weapon, flags;
    float pos[3], velocity[3], yaw, pitch;
} hta_net_player;

enum { HTA_NET_GROUNDED = 1, HTA_NET_CROUCH = 2,
       HTA_NET_FIRE = 4, HTA_NET_MELEE = 8, HTA_NET_GRENADE = 16 };
enum { HTA_NET_EVENT_FIRE = 1, HTA_NET_EVENT_MELEE = 2,
       HTA_NET_EVENT_GRENADE = 3, HTA_NET_EVENT_WEAPON = 4 };

typedef struct {
    uint8_t actor, kind, weapon;
    uint32_t event_id;
} hta_net_event;

/* What a server says about itself in answer to DISCOVER. */
#define HTA_NET_NAME 24u
/* v5: the world being played -- "" for Blood Gulch, else an imported
 * map's name ([a-z0-9_-]), so a joiner loads the same one. */
#define HTA_NET_MAP 24u
#define HTA_NET_INFO_BYTES (8u + HTA_NET_NAME + HTA_NET_MAP)
typedef struct {
    uint32_t nonce;           /* echoes the DISCOVER's */
    uint8_t players, max_players;
    uint8_t score_limit;      /* kills to win */
    uint8_t time_limit;       /* minutes, 0 for none */
    char name[HTA_NET_NAME];  /* printable ASCII, NUL-terminated */
    char map[HTA_NET_MAP];    /* [a-z0-9_-], NUL-terminated; "" Blood Gulch */
} hta_net_info;

/* The host's match state. Slots are stable for a round, including dead
 * players. Every number that affects combat or the scoreboard comes from
 * the host; clients never send these fields. */
enum { HTA_NET_ENTITY_NONE, HTA_NET_ENTITY_PLAYER, HTA_NET_ENTITY_BOT };
enum { HTA_NET_ENTITY_ALIVE=1, HTA_NET_ENTITY_GROUNDED=2,
       HTA_NET_ENTITY_CROUCH=4, HTA_NET_ENTITY_FIRE=8,
       HTA_NET_ENTITY_MELEE=16, HTA_NET_ENTITY_GRENADE=32,
       HTA_NET_ENTITY_BLUE=64, /* v4: on the blue team (team games) */
       HTA_NET_ENTITY_CLASS_REJECT=128 /* v7: unique hero was claimed */ };
typedef struct {
    uint8_t id, kind, flags, weapon, peer_id; /* peer_id 0 for bots */
    float pos[3], velocity[2], yaw, pitch, health, shield;
    int16_t score, kills, deaths;
    char name[HTA_NET_ENTITY_NAME];
    uint8_t carry[2], slot, grenades, powerup; /* carry 255 means empty */
    uint16_t ammo_loaded, ammo_reserve; /* held weapon */
    /* v6: the imported body it wears: 0 the cyborg, else the match's
     * character index + 1. Rides in the kind byte's upper six bits. */
    uint8_t character;
} hta_net_entity;
typedef struct {
    float time;
    uint16_t round;
    uint8_t count, bot_count, over, winner; /* winner 255 means none */
    uint8_t score_limit, time_limit, respawn_time;
    uint8_t item_count, item_present[8], item_choice[64];
    hta_net_entity entities[HTA_NET_MAX_ENTITIES];
} hta_net_world;

/* A player's requested controls, sampled repeatedly. The host applies
 * movement and fire; counters make one-shot actions survive packet loss. */
/* v6 HTA_NET_READY: the player has chosen a class and may be spawned. */
enum { HTA_NET_JUMP=1, HTA_NET_TRIGGER=2, HTA_NET_DUCK=4, HTA_NET_ALT=8, HTA_NET_READY=16, HTA_NET_FLY=32 };
/* v10: CONTENT -- the imported characters/weapons differ (app/compat.h). */
/* v11: VERSION -- the peers speak different protocols. A v11+ host answers
 * a foreign HELLO with it (payload: nonce, reason, its version u16), which a
 * newer client can read by peeking at the header; an older client cannot
 * read anything newer and simply gets no answer. A client also sets it
 * itself when any answer arrives in another version (hta_net_peek). */
enum { HTA_NET_REJECT_FULL=1, HTA_NET_REJECT_MAP=2, HTA_NET_REJECT_CONTENT=3, HTA_NET_REJECT_VERSION=4, HTA_NET_REJECT_IDENTITY=5 };
/* A Trial item spawn's weighted choices (asset/items.h HTA_ITEM_MAX_CHOICES):
 * WORLD's item_choice indexes them, so it is checked against this. */
#define HTA_NET_MAX_ITEM_CHOICES 8u
typedef struct {
    uint8_t id, flags, weapon_slot;
    float forward, right, yaw, pitch;
    uint16_t melee_count, grenade_count, reload_count, pickup_count;
    uint16_t action_count;    /* get in or out of a vehicle */
    /* v6: the player's custom class (roster indices, 255 none) and body
     * (0 the cyborg, else character index + 1). */
    uint8_t loadout[2], character;
    uint8_t team; /* 0 unchosen, 1 crimson, 2 azure */
    uint16_t ability_count;
    uint16_t rpg_serial;
    uint8_t rpg_action,rpg_item;
} hta_net_control;

/* v14: private authoritative character state; catalogue indices require
 * identical world/content fingerprints. No player identifiers exposed. */
#define HTA_NET_RPG_BYTES 264u
typedef struct {
    uint32_t wave,queued,prestige,upgrade[5],quantity[32];
    uint64_t gold,experience,skill[8];
    uint16_t serial;
    uint8_t phase,result,equipment[4];
    float mana,stamina,timer,max_health;
} hta_net_rpg;
bool hta_net_rpg_pack(uint8_t *dst,size_t cap,const hta_net_rpg *r);
bool hta_net_rpg_unpack(const uint8_t *src,size_t len,hta_net_rpg *r);

/* Every vehicle the host runs, whole, at snapshot rate. Positions are
 * hundredths of a world unit and angles ten-thousandths of a radian on the
 * wire, which keeps 32 of them under the packet cap. */
enum { HTA_NET_VEHICLE_ACTIVE=1, HTA_NET_VEHICLE_GROUNDED=2, HTA_NET_VEHICLE_DRIVEN=4 };
typedef struct {
    uint8_t index, flags;
    float pos[3];
    float yaw, pitch, roll;
    float aim_yaw, aim_pitch;
    float steering, wheel_spin, barrel_spin;
    float speed;                          /* signed, along the hull */
    uint8_t occupant[HTA_NET_VEHICLE_SEATS]; /* unit id, 255 empty */
    float travel[4];                      /* the first four wheels' travel */
} hta_net_vehicle;
typedef struct {
    uint8_t count;
    hta_net_vehicle cars[HTA_NET_MAX_VEHICLES];
} hta_net_vehicles;

/* v9: a kill says whether the body came apart, how hard (the game's
 * blast fraction) and from where, so every screen gibs the same corpse. */
enum { HTA_NET_KILL_GIBBED = 1 };
typedef struct {
    uint32_t id;
    uint8_t victim, killer; /* killer 255 means environment/suicide */
    char text[96];
    uint8_t flags;          /* HTA_NET_KILL_GIBBED */
    float amount;           /* 0..4 on the wire in 1/60ths */
    float pos[3], from[3];  /* where it died; the blast's centre */
} hta_net_kill;

/* HTA_NET_FX_WRECK: a vehicle blew up; `weapon` is the car.
 * HTA_NET_FX_WORLD_SOUND (X7): an event binding's play_sound on the host --
 * `entity` the world entity it sounds at, the world's sound asset index in
 * `weapon` (low byte) and `material` (high byte), `pos` where. Presentation
 * only: a joiner plays it from its own copy of the world (the same bytes:
 * the world key). The FX packet and its codec are unchanged; the host sends
 * this kind only in worlds with bindings, which only an X7 engine loads, and
 * an older decoder would refuse this kind as malformed -- but no older build
can load such a world, so none is ever in that match (the X1 WORLD_STATE
rule, docs/WORLD_ENTITIES.md "Why not v11"). */
enum { HTA_NET_FX_FIRE=1, HTA_NET_FX_IMPACT, HTA_NET_FX_DETONATE, HTA_NET_FX_WRECK, HTA_NET_FX_WORLD_SOUND, HTA_NET_FX_SPELL };
#define HTA_NET_FX_MAX_WORLD_ENTITIES 1024u /* HTA_WDEF_MAX_ENTITIES (v11: a runtime object index, 16 bits) */
#define HTA_NET_FX_MAX_WORLD_SOUNDS 2048u   /* HTA_RES_MAX */
typedef struct {
    uint8_t kind;
    uint16_t entity;                        /* a unit (255 none), or (world sound) a runtime object */
    uint8_t weapon, material;               /* weapon: roster or pool index */
    float pos[3], dir[3];
} hta_net_fx;
typedef struct {
    uint8_t pool, slot;
    float pos[3], dir[3], speed;
} hta_net_projectile;
typedef struct {
    uint8_t count;
    hta_net_projectile live[HTA_NET_MAX_PROJECTILES];
} hta_net_projectiles;

/* All integers and IEEE-754 floats are encoded little-endian; no C struct
 * layout crosses the wire. A decoder rejects nonfinite floats and extra data. */
bool hta_net_pack(uint8_t *dst, size_t cap, uint8_t type, uint32_t seq,
                  uint32_t tick, const uint8_t *payload, uint16_t len,
                  size_t *written);
bool hta_net_unpack(const uint8_t *src, size_t len, hta_net_packet *out);
bool hta_net_player_pack(uint8_t *dst, size_t cap, const hta_net_player *p);
bool hta_net_player_unpack(const uint8_t *src, size_t len, hta_net_player *p);
bool hta_net_event_pack(uint8_t *dst, size_t cap, const hta_net_event *e);
bool hta_net_event_unpack(const uint8_t *src, size_t len, hta_net_event *e);
bool hta_net_info_pack(uint8_t *dst, size_t cap, const hta_net_info *i);
bool hta_net_info_unpack(const uint8_t *src, size_t len, hta_net_info *i);
bool hta_net_world_pack(uint8_t *dst, size_t cap, const hta_net_world *w, size_t *written);
bool hta_net_world_unpack(const uint8_t *src, size_t len, hta_net_world *w);
bool hta_net_control_pack(uint8_t *dst, size_t cap, const hta_net_control *c);
bool hta_net_control_unpack(const uint8_t *src, size_t len, hta_net_control *c);
bool hta_net_kill_pack(uint8_t *dst, size_t cap, const hta_net_kill *k);
bool hta_net_kill_unpack(const uint8_t *src, size_t len, hta_net_kill *k);
bool hta_net_fx_pack(uint8_t *dst, size_t cap, const hta_net_fx *fx);
bool hta_net_fx_unpack(const uint8_t *src, size_t len, hta_net_fx *fx);
bool hta_net_projectiles_pack(uint8_t *dst, size_t cap, const hta_net_projectiles *p,
                              size_t *written);
bool hta_net_projectiles_unpack(const uint8_t *src, size_t len, hta_net_projectiles *p);
/* Weapons lying on the ground: which, where, which way. */
#define HTA_NET_MAX_DROPS 32u
#define HTA_NET_DROP_BYTES 9u
typedef struct {
    uint8_t count;
    struct { uint8_t weapon; float pos[3], yaw; } drop[HTA_NET_MAX_DROPS];
} hta_net_drops;
bool hta_net_drops_pack(uint8_t *dst, size_t cap, const hta_net_drops *d, size_t *written);
bool hta_net_drops_unpack(const uint8_t *src, size_t len, hta_net_drops *d);

/* The game's rules and state beside WORLD, which is full: the mode, both
 * teams' scores, each flag's state, carrier and place, and each vehicle's
 * hull (0..255 of full; 0 for a wreck). */
/* v9: and which of the map's destructible props are broken, one bit
 * each, in the order both sides loaded them from the same map. */
#define HTA_NET_MAX_PROPS 512u
#define HTA_NET_RACE_BYTES (4u + 8u * 6u)
#define HTA_NET_GAME_BASE_BYTES (8u + 2u * 11u + HTA_NET_MAX_VEHICLES + 2u + HTA_NET_MAX_PROPS / 8u)
#define HTA_NET_GAME_BYTES (HTA_NET_GAME_BASE_BYTES + HTA_NET_RACE_BYTES)
enum { HTA_NET_FLAG_HOME = 0, HTA_NET_FLAG_CARRIED, HTA_NET_FLAG_DROPPED };
enum { HTA_NET_GAME_CLASSES = 1, HTA_NET_GAME_DUPLICATES = 2 };
typedef struct {
    uint8_t mode;              /* hta_game_mode */
    uint8_t score_limit;
    int16_t team_score[2];
    uint8_t winner_team;       /* 255: none or a draw */
    uint8_t options;           /* v6: HTA_NET_GAME_CLASSES */
    struct {
        uint8_t present, state, carrier;   /* carrier 255: nobody */
        float pos[3], yaw;
    } flag[2];
    uint8_t hull[HTA_NET_MAX_VEHICLES];
    uint16_t prop_count;       /* <= HTA_NET_MAX_PROPS */
    uint8_t prop_broken[HTA_NET_MAX_PROPS / 8u];
    /* v13: host-owned racing rules; inactive entries are zero. */
    uint8_t race_phase, race_countdown; /* countdown in tenths */
    uint16_t race_elapsed;              /* centiseconds */
    struct { uint8_t lap, next_gate, flags, finish_order, boost_tier, position; } race[8];
} hta_net_game;
bool hta_net_game_pack(uint8_t *dst, size_t cap, const hta_net_game *g);
bool hta_net_game_unpack(const uint8_t *src, size_t len, hta_net_game *g);

/* v11 WORLD_STATE (X8, docs/WORLD_STATE.md): the world's replicated state,
 * addressed by REPLICATION index -- never by what both sides already know
 * from the package (placements, definitions, bindings).
 *
 *   u8  format (1)   u16 spatial_total   u16 flag_total
 *   then sections, each a run of consecutive entries of one table:
 *     u8 1 (SPATIAL)  u16 first  u8 n (1..255)   n movers: u8 phase
 *                     (0 closed, 1 opening, 2 open, 3 closing), and for a
 *                     moving phase u16 progress in 1/65535ths (at rest it is
 *                     exactly 0 or 1 and not sent)
 *     u8 2 (FLAGS)    u16 first  u16 n (1..1024) ceil(n/8) bytes, bit k =
 *                     flag first+k active; unused high bits zero
 *
 * Totals are the sender's table sizes: a receiver whose world differs
 * refuses the message. Runs of one table ascend and never overlap (so no
 * entry appears twice). A message may carry any subset; the host sends all
 * of it every time (it fits: HTA_NET_WSTATE_FULL_MAX), so each is a complete
 * snapshot and a late joiner needs nothing earlier. Resulting state only --
 * clients never see the events. */
#define HTA_NET_WSTATE_FORMAT 1u
#define HTA_NET_WSTATE_MAX_SPATIAL 256u    /* HTA_WREP_MAX_SPATIAL */
#define HTA_NET_WSTATE_MAX_FLAGS 1024u     /* HTA_WREP_MAX_FLAGS */
#define HTA_NET_WSTATE_HEADER 5u
#define HTA_NET_WSTATE_SPATIAL_RUN 4u      /* kind, first, n */
#define HTA_NET_WSTATE_FLAG_RUN 5u         /* kind, first, n */
#define HTA_NET_WSTATE_MAX_RUN 255u
/* Every mover moving and every flag, in maximal runs: the largest complete snapshot. */
#define HTA_NET_WSTATE_FULL_MAX (HTA_NET_WSTATE_HEADER + \
    ((HTA_NET_WSTATE_MAX_SPATIAL + HTA_NET_WSTATE_MAX_RUN - 1u) / HTA_NET_WSTATE_MAX_RUN) * HTA_NET_WSTATE_SPATIAL_RUN + \
    HTA_NET_WSTATE_MAX_SPATIAL * 3u + HTA_NET_WSTATE_FLAG_RUN + HTA_NET_WSTATE_MAX_FLAGS / 8u)
enum { HTA_NET_WSTATE_SPATIAL = 1, HTA_NET_WSTATE_FLAGS = 2 };
/* When the host sends it (net/session.c), in 20 Hz server ticks: on a change,
 * then REPEATS more times; while only progress moves, every MOVING_TICKS;
 * idle, every KEYFRAME_TICKS; and at once for a new peer. */
#define HTA_NET_WSTATE_REPEATS 2u
#define HTA_NET_WSTATE_MOVING_TICKS 4u     /* 5 Hz */
#define HTA_NET_WSTATE_KEYFRAME_TICKS 20u  /* 1 s */
typedef struct {
    uint16_t spatial_total, flag_total;
    uint8_t  spatial_has[HTA_NET_WSTATE_MAX_SPATIAL / 8u];  /* entries carried */
    uint8_t  phase[HTA_NET_WSTATE_MAX_SPATIAL];
    uint16_t t[HTA_NET_WSTATE_MAX_SPATIAL];
    uint8_t  flag_has[HTA_NET_WSTATE_MAX_FLAGS / 8u];
    uint8_t  flag[HTA_NET_WSTATE_MAX_FLAGS / 8u];           /* bit k: flag k active */
} hta_net_world_state;
/* False (and `err`, when given, says why in words) for anything malformed,
 * over a limit, out of order or truncated; nothing is written then. */
bool hta_net_world_state_pack(uint8_t *dst, size_t cap, const hta_net_world_state *w, size_t *written);
/* The size of a complete snapshot of `spatial` movers (`moving` of them
 * moving) and `flags` relays, as hta_net_world_state_pack writes it. */
static inline uint32_t hta_net_world_state_bytes(uint32_t spatial, uint32_t moving, uint32_t flags)
{
    return HTA_NET_WSTATE_HEADER +
           (spatial ? (spatial + HTA_NET_WSTATE_MAX_RUN - 1u) / HTA_NET_WSTATE_MAX_RUN * HTA_NET_WSTATE_SPATIAL_RUN + spatial + 2u * moving : 0u) +
           (flags ? HTA_NET_WSTATE_FLAG_RUN + (flags + 7u) / 8u : 0u);
}
bool hta_net_world_state_unpack(const uint8_t *src, size_t len, hta_net_world_state *w, char *err, size_t errlen);
static inline bool hta_net_bit(const uint8_t *bits, uint32_t k) { return (bits[k >> 3] >> (k & 7u)) & 1u; }
static inline void hta_net_bit_set(uint8_t *bits, uint32_t k, bool on)
{ if (on) bits[k >> 3] |= (uint8_t)(1u << (k & 7u)); else bits[k >> 3] &= (uint8_t)~(1u << (k & 7u)); }

/* v11: any packet's version and type, if it carries this protocol's magic
 * (the header's first 7 bytes have kept one layout since v1). A peer uses
 * it to name the other side's version instead of just not answering. */
bool hta_net_peek(const uint8_t *src, size_t len, uint16_t *version, uint8_t *type);
/* v11: a DISCOVER in an OLDER protocol's header (DISCOVER and its 4-byte
 * nonce are the same in every version since v4): a host of that version
 * answers it, which is how a newer client learns it has found an older host. */
bool hta_net_pack_probe(uint8_t *dst, size_t cap, uint16_t version, uint32_t nonce, size_t *written);
#define HTA_NET_PROBE_OLDEST 8u

bool hta_net_vehicles_pack(uint8_t *dst, size_t cap, const hta_net_vehicles *v,
                           size_t *written);
bool hta_net_vehicles_unpack(const uint8_t *src, size_t len, hta_net_vehicles *v);
void hta_net_u32_write(uint8_t *p, uint32_t v);
uint32_t hta_net_u32_read(const uint8_t *p);

#endif
