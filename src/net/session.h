#ifndef HTA_NET_SESSION_H
#define HTA_NET_SESSION_H
#include "protocol.h"
#include "udp.h"

typedef struct {
    uint64_t packets_in, packets_out, bytes_in, bytes_out;
    uint64_t invalid, dropped, snapshots_in, snapshots_out, events_in, events_out;
    uint64_t worlds_in, worlds_out;
    uint64_t limited;           /* server: dropped unread, over a source's rate */
    uint64_t refused;           /* server: HELLOs turned away (map, content, v11: version) */
    uint64_t world_states, world_state_bytes;   /* X8: WORLD_STATE sent (server) or applied (client), payload bytes */
    uint32_t world_state_max;                   /* X8: the largest WORLD_STATE payload */
    double ping_ms;
} hta_net_stats;

typedef struct {
    bool active;
    hta_net_kill kill;
    double last_sent;
} hta_net_pending_kill;

typedef struct {
    bool active;
    bool has_state;
    hta_udp_addr addr;
    uint32_t token, nonce, last_sequence;
    double last_seen;
    hta_net_player player;
    hta_net_control control;
    uint8_t identity[16];
    bool has_control;
    double last_control_at;
    hta_net_pending_kill pending_kills[16];
} hta_net_peer;

/* Per-source rate limiting (docs/DEDICATED_SERVER.md): a token bucket per
 * IPv4 address, checked before a packet is decoded, so a flood costs one
 * table lookup a packet. Per address, not per port, so one sender cannot
 * dodge it by spraying ports -- which means players behind one router
 * share a budget: the default fits all 8 at the ~50 packets/s a phone or
 * PC joiner sends, with room to spare. */
#define HTA_NET_RATE_SOURCES 32u
#define HTA_NET_RATE_PER_S 600.0f
#define HTA_NET_RATE_BURST 1200.0f
typedef struct { uint32_t ip; float tokens; double last; bool used; } hta_net_rate_source;

typedef struct {
    hta_udp udp;
    hta_net_peer peers[HTA_NET_MAX_PLAYERS];
    uint32_t sequence, tick;
    double last_snapshot;
    hta_net_stats stats;
    hta_net_event actions[32];
    unsigned action_count;
    uint32_t last_world_tick;
    uint32_t next_kill_id;
    uint32_t last_projectile_tick;
    uint32_t last_vehicle_tick;
    uint32_t last_drop_tick;
    uint32_t last_game_tick;
    uint32_t last_world_state_tick;
    /* X8: WORLD_STATE goes out when what it says changes -- a mover's phase,
     * a relay's flag: at once, then HTA_NET_WSTATE_REPEATS more times against
     * loss -- while anything moves every HTA_NET_WSTATE_MOVING_TICKS (clients
     * move movers themselves between), when a peer has just joined, and at
     * least every HTA_NET_WSTATE_KEYFRAME_TICKS. Each is complete. */
    hta_net_world_state ws_sent;
    bool     ws_have, ws_force;
    uint8_t  ws_repeat;
    uint32_t ws_last_tick;
    uint32_t map_crc;
    uint64_t content;          /* v10: the imported rosters' fingerprint; 0 none */
    uint8_t  last_refusal;     /* the last HELLO turned away: HTA_NET_REJECT_* */
    uint16_t last_refused_version;  /* v11: ... and, for VERSION, the joiner's protocol */
    /* What DISCOVER is told. max_players also caps who HELLO lets in;
     * hta_net_server_open sets it to HTA_NET_MAX_PLAYERS. */
    hta_net_info info;
    /* Rate limiting; hta_net_server_open sets the defaults, 0 turns it off. */
    float rate_per_s, rate_burst;
    hta_net_rate_source rate[HTA_NET_RATE_SOURCES];
} hta_net_server;

/* Looking for servers: DISCOVER out to one or more addresses (a broadcast
 * address finds everyone on the LAN), INFO back. */
typedef struct {
    hta_udp udp;
    uint32_t nonce;
} hta_net_scan;

typedef struct {
    hta_udp udp;
    hta_udp_addr server;
    uint32_t sequence, nonce, token;
    uint32_t last_snapshot_tick;
    uint8_t id;
    uint8_t identity[16];
    hta_net_rpg rpg;
    bool have_rpg;
    uint32_t last_rpg_tick;
    bool connected;
    uint8_t reject_reason;
    uint16_t peer_version;     /* v11: with REJECT_VERSION, the host's protocol (0 unknown) */
    double last_probe;         /* v11: when older-protocol probes went out */
    uint32_t map_crc;
    uint64_t content;          /* v10: sent in HELLO (app/compat.h) */
    double last_hello, last_ping, ping_sent, last_receive;
    uint32_t ping_nonce;
    hta_net_player players[HTA_NET_MAX_PLAYERS];
    bool present[HTA_NET_MAX_PLAYERS];
    hta_net_event events[32];
    unsigned event_count;
    hta_net_stats stats;
    hta_net_world world;
    bool have_world;
    uint32_t last_world_tick;
    hta_net_kill kills[32];
    unsigned kill_count;
    uint32_t seen_kills[64];
    unsigned seen_kill_cursor;
    hta_net_fx fx[64];
    unsigned fx_count;
    hta_net_projectiles projectiles;
    bool have_projectiles;
    uint32_t last_projectile_tick;
    hta_net_vehicles vehicles;
    bool have_vehicles;
    uint32_t last_vehicle_tick;
    hta_net_drops drops;
    bool have_drops;
    uint32_t last_drop_tick;
    hta_net_game game;
    bool have_game;
    uint32_t last_game_tick;
    hta_net_world_state world_state;
    bool have_world_state;
    uint32_t last_world_state_tick;
    char world_state_error[160];   /* X8: why the last WORLD_STATE was refused ("" none) */
} hta_net_client;


bool hta_net_server_open(hta_net_server *s, uint16_t port);
/* Listening on one address only (NULL: all), e.g. the Tailscale address. */
bool hta_net_server_open_bind(hta_net_server *s, const char *ip, uint16_t port);
void hta_net_server_close(hta_net_server *s);
/* Nonblocking bounded drain; now is monotonic seconds supplied by caller. */
void hta_net_server_pump(hta_net_server *s, double now);
unsigned hta_net_server_count(const hta_net_server *s);
bool hta_net_server_world(hta_net_server *s, const hta_net_world *world);
bool hta_net_server_pop_action(hta_net_server *s, hta_net_event *action);
bool hta_net_server_kill(hta_net_server *s, const hta_net_kill *kill, double now);
bool hta_net_server_fx(hta_net_server *s, const hta_net_fx *fx);
bool hta_net_server_projectiles(hta_net_server *s, const hta_net_projectiles *projectiles);
bool hta_net_server_vehicles(hta_net_server *s, const hta_net_vehicles *vehicles);
bool hta_net_server_drops(hta_net_server *s, const hta_net_drops *drops);
bool hta_net_server_rpg(hta_net_server *s,unsigned peer,const hta_net_rpg *rpg);
bool hta_net_server_game(hta_net_server *s, const hta_net_game *game);
/* X8: the world's complete replicated state now (every entry carried); the
 * server decides whether it goes out this tick (see ws_* above). True when sent. */
bool hta_net_server_world_state(hta_net_server *s, const hta_net_world_state *ws);

bool hta_net_scan_open(hta_net_scan *s);
void hta_net_scan_close(hta_net_scan *s);
bool hta_net_scan_send(hta_net_scan *s, const char *ip, uint16_t port);
/* One answer, if one has arrived: the server's info and where it is. */
bool hta_net_scan_recv(hta_net_scan *s, hta_net_info *info, char ip[16], uint16_t *port);

bool hta_net_client_open(hta_net_client *c, const char *ip, uint16_t port);
void hta_net_client_close(hta_net_client *c);
void hta_net_client_pump(hta_net_client *c, double now);
bool hta_net_client_state(hta_net_client *c, const hta_net_player *p);
bool hta_net_client_control(hta_net_client *c, const hta_net_control *control);
bool hta_net_client_event(hta_net_client *c, const hta_net_event *e);
/* First queued event is removed; snapshots remain in players[]. */
bool hta_net_client_pop_event(hta_net_client *c, hta_net_event *e);
bool hta_net_client_pop_kill(hta_net_client *c, hta_net_kill *kill);
bool hta_net_client_pop_fx(hta_net_client *c, hta_net_fx *fx);
#endif
