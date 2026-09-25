/* MEGAMOD SERVER: a dedicated, headless LAN / Tailscale / internet host.
 *
 *   megamod-server --config server.cfg [--check] [--seconds S]
 *
 * Reads a server.cfg (src/app/server_config.h; docs/DEDICATED_SERVER.md has
 * every key), lists this machine's interfaces and binds exactly where the
 * config says -- every interface, the LAN address, the Tailscale address,
 * or one IP -- answers LAN discovery with its name and map, and writes a
 * JSON status file for scripts and agents.
 *
 * `sim = scripted` serves megamod-fakehost's scripted match, to test
 * clients, binding and reachability now. `sim = match` (the real game, run
 * headless) needs the match simulation moved out of platform_android.c
 * first -- the plan is docs/DEDICATED_SERVER.md -- and says so until then.
 *
 * --check validates the config and the bind address and prints what the
 * server would do, without opening anything. */
#define _POSIX_C_SOURCE 200809L
#include "app/scripted_match.h"
#include "app/server_config.h"
#include "net/session.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t g_stop;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static uint32_t list_ifaces(hta_iface *out, uint32_t cap)
{
    struct ifaddrs *all = NULL, *a;
    uint32_t n = 0;
    if (getifaddrs(&all) != 0) return 0;
    for (a = all; a && n < cap; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        snprintf(out[n].name, sizeof(out[n].name), "%s", a->ifa_name);
        out[n].ip = ntohl(((const struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr);
        n++;
    }
    freeifaddrs(all);
    return n;
}

static void write_status(const hta_server_config *c, const hta_net_server *s, const char *bound,
                         double uptime, const char *map)
{
    if (!c->status_file[0]) return;
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", c->status_file);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "{\"name\":\"%s\",\"protocol\":%u,\"bind\":\"%s\",\"port\":%u,\"map\":\"%s\","
               "\"sim\":\"%s\",\"uptime_s\":%.0f,\"players\":%u,\"max_players\":%u,"
               "\"packets_in\":%llu,\"packets_out\":%llu,\"invalid\":%llu,\"peers\":[",
            c->name, HTA_NET_VERSION, bound[0] ? bound : "all", c->port, map,
            c->sim == HTA_SERVER_SIM_SCRIPTED ? "scripted" : "match", uptime,
            hta_net_server_count(s), c->max_players,
            (unsigned long long)s->stats.packets_in, (unsigned long long)s->stats.packets_out,
            (unsigned long long)s->stats.invalid);
    bool first = true;
    for (unsigned i = 0; i < HTA_NET_MAX_PLAYERS; i++) {
        const hta_net_peer *p = &s->peers[i];
        if (!p->active) continue;
        char ip[16] = "";
        uint16_t port = 0;
        hta_udp_addr_ip(&p->addr, ip, &port);
        fprintf(f, "%s{\"id\":%u,\"address\":\"%s:%u\"}", first ? "" : ",", p->player.id, ip, port);
        first = false;
    }
    fprintf(f, "]}\n");
    fclose(f);
    rename(tmp, c->status_file);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    bool check = false;
    double seconds = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else { fprintf(stderr, "usage: %s --config server.cfg [--check] [--seconds S]\n", argv[0]); return 2; }
    }
    static hta_server_config c;
    hta_server_config_defaults(&c);
    if (path) {
        FILE *f = fopen(path, "rb");
        if (!f) { perror(path); return 1; }
        static char text[65536];
        size_t n = fread(text, 1, sizeof(text) - 1, f);
        fclose(f);
        hta_server_config_parse(&c, text, n);
    }
    if (c.warnings[0]) fprintf(stderr, "warnings:\n%s", c.warnings);
    if (c.error_count) { fprintf(stderr, "errors in %s:\n%s", path ? path : "config", c.errors); return 1; }

    hta_iface ifs[32];
    uint32_t nif = list_ifaces(ifs, 32);
    char bound[16], why[200] = "";
    if (!hta_server_bind_resolve(&c, ifs, nif, bound, why, sizeof(why))) {
        fprintf(stderr, "bind: %s\ninterfaces here:\n", why);
        for (uint32_t i = 0; i < nif; i++)
            fprintf(stderr, "  %-12s %u.%u.%u.%u\n", ifs[i].name, ifs[i].ip >> 24, (ifs[i].ip >> 16) & 255u,
                    (ifs[i].ip >> 8) & 255u, ifs[i].ip & 255u);
        return 1;
    }
    char cfg[4096];
    hta_server_config_format(&c, cfg, sizeof(cfg));
    if (c.sim == HTA_SERVER_SIM_MATCH && !check) {
        fprintf(stderr, "megamod-server: `sim = match` needs the match simulation moved out of the Android app "
                        "first (docs/DEDICATED_SERVER.md, stages S1-S3).\n"
                        "Use `sim = scripted` meanwhile to test clients, binding and reachability.\n");
        return 2;
    }

    printf("megamod-server: protocol v%u, listening on %s:%u\n", HTA_NET_VERSION, bound[0] ? bound : "0.0.0.0 (all)", c.port);
    if (check) { printf("%s", cfg); return 0; }

    static hta_net_server s;
    if (!hta_net_server_open_bind(&s, bound[0] ? bound : NULL, c.port)) {
        fprintf(stderr, "cannot listen on %s:%u (in use, or not this machine's address)\n", bound[0] ? bound : "*", c.port);
        return 1;
    }
    snprintf(s.info.name, sizeof(s.info.name), "%.23s", c.name);
    s.info.max_players = c.max_players;
    snprintf(s.info.map, sizeof(s.info.map), "%.23s", strcmp(c.maps[0], "bloodgulch") ? c.maps[0] : "");
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    static hta_scripted_match m;
    double t0 = now_s(), next_status = t0, step = 1.0 / (double)c.tick_rate;
    hta_scripted_match_init(&m, "server", (unsigned)(c.bots < 8 ? c.bots : 8), 40, t0);
    printf("megamod-server: '%s', scripted match, %d bots, up to %u players\n", c.name, c.bots, c.max_players);
    fflush(stdout);
    while (!g_stop) {
        double now = now_s();
        if (seconds > 0 && now - t0 > seconds) break;
        hta_net_server_pump(&s, now);
        hta_scripted_match_tick(&m, &s, now);
        if (now >= next_status) { write_status(&c, &s, bound, now - t0, c.maps[0]); next_status = now + 5.0; }
        double left = step - (now_s() - now);
        if (left > 0) {
            struct timespec nap = { 0, (long)(left * 1e9) };
            nanosleep(&nap, NULL);
        }
    }
    write_status(&c, &s, bound, now_s() - t0, c.maps[0]);
    printf("megamod-server: stopped after %.0f s, %u players connected at the end\n", now_s() - t0, hta_net_server_count(&s));
    hta_net_server_close(&s);
    return 0;
}
