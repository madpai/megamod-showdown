#ifndef HTA_NET_SESSION_H
#define HTA_NET_SESSION_H
#include "protocol.h"
#include "udp.h"

typedef struct {
    uint64_t packets_in, packets_out, bytes_in, bytes_out;
    uint64_t invalid, dropped, snapshots_in, snapshots_out, events_in, events_out;
    uint64_t worlds_in, worlds_out;
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
    bool has_control;
    double last_control_at;
    hta_net_pending_kill pending_kills[16];
} hta_net_peer;

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
    uint32_t map_crc;
    /* What DISCOVER is told. max_players also caps who HELLO lets in;
     * hta_net_server_open sets it to HTA_NET_MAX_PLAYERS. */
    hta_net_info info;
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
    bool connected;
    uint8_t reject_reason;
    uint32_t map_crc;
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
} hta_net_client;

bool hta_net_server_open(hta_net_server *s, uint16_t port);
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
bool hta_net_server_game(hta_net_server *s, const hta_net_game *game);

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
