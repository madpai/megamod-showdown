/* The dedicated server's configuration (docs/DEDICATED_SERVER.md): a
 * key=value file like video.cfg, parsed and checked here, portably, so the
 * server binary, a future menu and tests all agree on what it means.
 *
 *   name = Friday LAN
 *   bind = tailscale          # all | lan | tailscale | an IPv4 address
 *   port = 32270
 *   maps = bloodgulch, de_dust2, cs_office
 *   mode = team               # slayer | team | ctf
 *   bots = 4
 *
 * Unknown keys are warnings (a newer config on an older server still runs);
 * values out of range or unparseable are errors. */
#ifndef HTA_APP_SERVER_CONFIG_H
#define HTA_APP_SERVER_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTA_SERVER_MAX_MAPS 16u
#define HTA_SERVER_DEFAULT_PORT 32270u

typedef enum { HTA_BIND_ALL = 0, HTA_BIND_LAN, HTA_BIND_TAILSCALE, HTA_BIND_ADDRESS } hta_bind_kind;
typedef enum { HTA_SERVER_SIM_MATCH = 0, HTA_SERVER_SIM_SCRIPTED } hta_server_sim;

typedef struct {
    char     name[24];                 /* the protocol's INFO carries 23 characters */
    char     motd[96];
    hta_bind_kind bind;
    char     bind_ip[16];              /* HTA_BIND_ADDRESS */
    uint16_t port;
    uint8_t  max_players;              /* 2..8 */
    char     password[32];             /* needs protocol v10: parsed, not enforced yet */
    char     maps[HTA_SERVER_MAX_MAPS][48];   /* rotation; "bloodgulch" is the Trial's */
    uint32_t map_count;
    char     map_dir[256];             /* where <map>.oalmap packages live */
    char     trial_dir[256];           /* the owner's Trial maps */
    uint8_t  mode;                     /* hta_game_mode: 0 slayer, 1 team slayer, 2 ctf */
    int      score_limit;              /* 0: the mode's default */
    int      time_limit_min;           /* 0: none */
    int      respawn_s;
    int      bots, bot_skill;          /* 0..15, 0..3 */
    int      spawn_protect_s;
    bool     classes, duplicate_heroes;
    int      tick_rate;                /* simulation steps per second, 20..120 */
    char     status_file[256];         /* JSON status written every few seconds; "" none */
    hta_server_sim sim;                /* SCRIPTED: megamod-fakehost's match, to test networking */
    /* What parsing found. */
    char     warnings[512];
    char     errors[512];
    int      error_count;
} hta_server_config;

void hta_server_config_defaults(hta_server_config *c);
/* Parses `text` over the defaults already in `c`. Returns the error count
 * (also in c->error_count; the messages in c->errors). */
int  hta_server_config_parse(hta_server_config *c, const char *text, size_t len);
/* The config as a file again (key = value, every key): what --check prints. */
size_t hta_server_config_format(const hta_server_config *c, char *out, size_t cap);

/* Choosing the address to listen on, from the machine's interfaces (the
 * server lists them; this decides, so it can be tested with made-up ones).
 * `ip` is host-order IPv4. */
typedef struct { char name[32]; uint32_t ip; } hta_iface;
/* Writes the dotted address to bind ("" for all interfaces). False, with
 * the reason in `why`, when the kind asked for has no address here. */
bool hta_server_bind_resolve(const hta_server_config *c, const hta_iface *ifaces, uint32_t count,
                             char out[16], char *why, size_t whylen);

#endif
