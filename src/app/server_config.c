#include "server_config.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hta_server_config_defaults(hta_server_config *c)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->name, sizeof(c->name), "Megamod Server");
    c->bind = HTA_BIND_ALL;
    c->port = HTA_SERVER_DEFAULT_PORT;
    c->max_players = 8;
    snprintf(c->maps[0], sizeof(c->maps[0]), "bloodgulch");
    c->map_count = 1;
    c->mode = 0;
    c->respawn_s = 5;
    c->bots = 3;
    c->bot_skill = 1;
    c->spawn_protect_s = 3;
    c->tick_rate = 60;
    c->sim = HTA_SERVER_SIM_MATCH;
}

static void note(char *buf, size_t cap, const char *fmt, ...)
{
    size_t at = strlen(buf);
    if (at + 2 >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + at, cap - at, fmt, ap);
    va_end(ap);
    at = strlen(buf);
    if (at + 1 < cap) { buf[at] = '\n'; buf[at + 1] = 0; }
}

static bool parse_int(const char *v, int lo, int hi, int *out)
{
    char *end;
    long x = strtol(v, &end, 10);
    if (end == v || *end || x < lo || x > hi) return false;
    *out = (int)x;
    return true;
}

static bool parse_bool(const char *v, bool *out)
{
    if (!strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "true") || !strcmp(v, "1")) { *out = true; return true; }
    if (!strcmp(v, "off") || !strcmp(v, "no") || !strcmp(v, "false") || !strcmp(v, "0")) { *out = false; return true; }
    return false;
}

static bool dotted(const char *v, uint32_t *ip)
{
    unsigned a, b, c, d;
    char tail;
    if (sscanf(v, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    if (ip) *ip = (a << 24) | (b << 16) | (c << 8) | d;
    return true;
}

/* A map name: [a-z0-9_-], as the protocol's INFO carries it. */
static bool map_name_ok(const char *v)
{
    if (!*v || strlen(v) >= 48) return false;
    for (const char *p = v; *p; p++)
        if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '_' || *p == '-')) return false;
    return true;
}

static bool printable(const char *v)
{
    for (const unsigned char *p = (const unsigned char *)v; *p; p++)
        if (*p < 32 || *p > 126) return false;
    return true;
}

static void set_str(hta_server_config *c, char *dst, size_t cap, const char *key, const char *v)
{
    if (strlen(v) >= cap) { note(c->errors, sizeof(c->errors), "%s: too long (%zu characters at most)", key, cap - 1); c->error_count++; return; }
    if (!printable(v)) { note(c->errors, sizeof(c->errors), "%s: printable ASCII only", key); c->error_count++; return; }
    snprintf(dst, cap, "%s", v);
}

static void apply(hta_server_config *c, const char *k, const char *v, int line)
{
    int n;
    bool b;
#define BAD(...) do { note(c->errors, sizeof(c->errors), "line %d: %s: " __VA_ARGS__); c->error_count++; return; } while (0)
    if (!strcmp(k, "name")) set_str(c, c->name, sizeof(c->name), k, v);
    else if (!strcmp(k, "motd")) set_str(c, c->motd, sizeof(c->motd), k, v);
    else if (!strcmp(k, "password")) set_str(c, c->password, sizeof(c->password), k, v);
    else if (!strcmp(k, "map_dir")) set_str(c, c->map_dir, sizeof(c->map_dir), k, v);
    else if (!strcmp(k, "trial_dir")) set_str(c, c->trial_dir, sizeof(c->trial_dir), k, v);
    else if (!strcmp(k, "status_file")) set_str(c, c->status_file, sizeof(c->status_file), k, v);
    else if (!strcmp(k, "bind")) {
        if (!strcmp(v, "all") || !strcmp(v, "0.0.0.0")) c->bind = HTA_BIND_ALL;
        else if (!strcmp(v, "lan")) c->bind = HTA_BIND_LAN;
        else if (!strcmp(v, "tailscale")) c->bind = HTA_BIND_TAILSCALE;
        else if (dotted(v, NULL)) { c->bind = HTA_BIND_ADDRESS; snprintf(c->bind_ip, sizeof(c->bind_ip), "%s", v); }
        else BAD("%s", line, k, "all, lan, tailscale or an IPv4 address");
    } else if (!strcmp(k, "port")) {
        if (!parse_int(v, 1, 65535, &n)) BAD("%s", line, k, "1..65535");
        c->port = (uint16_t)n;
    } else if (!strcmp(k, "max_players")) {
        if (!parse_int(v, 2, 8, &n)) BAD("%s", line, k, "2..8");
        c->max_players = (uint8_t)n;
    } else if (!strcmp(k, "maps")) {
        uint32_t count = 0;
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", v);
        for (char *tok = strtok(tmp, ", \t"); tok; tok = strtok(NULL, ", \t")) {
            if (!map_name_ok(tok)) BAD("%s '%s'", line, k, "map names are lowercase letters, digits, _ and -", tok);
            if (count >= HTA_SERVER_MAX_MAPS) BAD("%s", line, k, "at most 16 maps");
            snprintf(c->maps[count++], sizeof(c->maps[0]), "%s", tok);
        }
        if (!count) BAD("%s", line, k, "at least one map");
        c->map_count = count;
    } else if (!strcmp(k, "mode")) {
        if (!strcmp(v, "slayer")) c->mode = 0;
        else if (!strcmp(v, "team") || !strcmp(v, "team_slayer")) c->mode = 1;
        else if (!strcmp(v, "ctf")) c->mode = 2;
        else BAD("%s", line, k, "slayer, team or ctf");
    } else if (!strcmp(k, "score_limit")) {
        if (!parse_int(v, 0, 255, &n)) BAD("%s", line, k, "0..255 (0: the mode's default)");
        c->score_limit = n;
    } else if (!strcmp(k, "time_limit")) {
        if (!parse_int(v, 0, 120, &n)) BAD("%s", line, k, "0..120 minutes");
        c->time_limit_min = n;
    } else if (!strcmp(k, "respawn")) {
        if (!parse_int(v, 1, 60, &n)) BAD("%s", line, k, "1..60 seconds");
        c->respawn_s = n;
    } else if (!strcmp(k, "bots")) {
        if (!parse_int(v, 0, 15, &n)) BAD("%s", line, k, "0..15");
        c->bots = n;
    } else if (!strcmp(k, "bot_skill")) {
        static const char *names[] = { "casual", "standard", "aggressive", "brutal" };
        int found = -1;
        for (int i = 0; i < 4; i++) if (!strcmp(v, names[i])) found = i;
        if (found < 0 && !parse_int(v, 0, 3, &found)) BAD("%s", line, k, "casual, standard, aggressive, brutal or 0..3");
        c->bot_skill = found;
    } else if (!strcmp(k, "spawn_protect")) {
        if (!parse_int(v, 0, 10, &n)) BAD("%s", line, k, "0..10 seconds");
        c->spawn_protect_s = n;
    } else if (!strcmp(k, "classes")) {
        if (!parse_bool(v, &b)) BAD("%s", line, k, "on or off");
        c->classes = b;
    } else if (!strcmp(k, "duplicate_heroes")) {
        if (!parse_bool(v, &b)) BAD("%s", line, k, "on or off");
        c->duplicate_heroes = b;
    } else if (!strcmp(k, "tick_rate")) {
        if (!parse_int(v, 20, 120, &n)) BAD("%s", line, k, "20..120");
        c->tick_rate = n;
    } else if (!strcmp(k, "sim")) {
        if (!strcmp(v, "match")) c->sim = HTA_SERVER_SIM_MATCH;
        else if (!strcmp(v, "scripted")) c->sim = HTA_SERVER_SIM_SCRIPTED;
        else BAD("%s", line, k, "match or scripted");
    } else {
        note(c->warnings, sizeof(c->warnings), "line %d: unknown key '%s' (ignored)", line, k);
    }
#undef BAD
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

int hta_server_config_parse(hta_server_config *c, const char *text, size_t len)
{
    char line[1200];
    size_t at = 0;
    int number = 0;
    while (at < len) {
        size_t end = at;
        while (end < len && text[end] != '\n') end++;
        size_t n = end - at < sizeof(line) - 1 ? end - at : sizeof(line) - 1;
        memcpy(line, text + at, n);
        line[n] = 0;
        at = end + 1;
        number++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *s = trim(line);
        if (!*s) continue;
        char *eq = strchr(s, '=');
        if (!eq) {
            note(c->errors, sizeof(c->errors), "line %d: expected key = value", number);
            c->error_count++;
            continue;
        }
        *eq = 0;
        apply(c, trim(s), trim(eq + 1), number);
    }
    if (c->password[0])
        note(c->warnings, sizeof(c->warnings), "password is set but not enforced until protocol v10 (docs/DEDICATED_SERVER.md)");
    if (c->bind == HTA_BIND_ALL)
        note(c->warnings, sizeof(c->warnings), "bind = all listens on every interface; with a port forward that is the whole internet");
    return c->error_count;
}

size_t hta_server_config_format(const hta_server_config *c, char *out, size_t cap)
{
    static const char *binds[] = { "all", "lan", "tailscale", "" };
    static const char *modes[] = { "slayer", "team", "ctf" };
    static const char *skills[] = { "casual", "standard", "aggressive", "brutal" };
    char maps[HTA_SERVER_MAX_MAPS * 50] = "";
    for (uint32_t i = 0; i < c->map_count; i++) {
        if (i) strcat(maps, ", ");
        strcat(maps, c->maps[i]);
    }
    int n = snprintf(out, cap,
        "name = %s\nmotd = %s\nbind = %s\nport = %u\nmax_players = %u\npassword = %s\n"
        "maps = %s\nmap_dir = %s\ntrial_dir = %s\nmode = %s\nscore_limit = %d\ntime_limit = %d\n"
        "respawn = %d\nbots = %d\nbot_skill = %s\nspawn_protect = %d\nclasses = %s\n"
        "duplicate_heroes = %s\ntick_rate = %d\nstatus_file = %s\nsim = %s\n",
        c->name, c->motd, c->bind == HTA_BIND_ADDRESS ? c->bind_ip : binds[c->bind], c->port,
        c->max_players, c->password[0] ? "(set)" : "", maps, c->map_dir, c->trial_dir,
        modes[c->mode < 3 ? c->mode : 0], c->score_limit, c->time_limit_min, c->respawn_s, c->bots,
        skills[c->bot_skill & 3], c->spawn_protect_s, c->classes ? "on" : "off",
        c->duplicate_heroes ? "on" : "off", c->tick_rate, c->status_file,
        c->sim == HTA_SERVER_SIM_SCRIPTED ? "scripted" : "match");
    return n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1;
}

static bool is_private(uint32_t ip)
{
    return (ip >> 24) == 10 || (ip >> 20) == (172u << 4 | 1u) || (ip >> 16) == (192u << 8 | 168u);
}
static bool is_cgnat(uint32_t ip) { return (ip >> 22) == (100u << 2 | 1u); }   /* 100.64.0.0/10: Tailscale */
static bool virtual_iface(const char *n)
{
    return !strncmp(n, "docker", 6) || !strncmp(n, "br-", 3) || !strncmp(n, "veth", 4) ||
           !strncmp(n, "virbr", 5) || !strncmp(n, "lo", 2);
}

static void fmt_ip(uint32_t ip, char out[16])
{
    snprintf(out, 16, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u);
}

bool hta_server_bind_resolve(const hta_server_config *c, const hta_iface *ifs, uint32_t n,
                             char out[16], char *why, size_t whylen)
{
    out[0] = 0;
    switch (c->bind) {
    case HTA_BIND_ALL:
        return true;
    case HTA_BIND_ADDRESS: {
        uint32_t want = 0;
        dotted(c->bind_ip, &want);
        for (uint32_t i = 0; i < n; i++)
            if (ifs[i].ip == want) { fmt_ip(want, out); return true; }
        snprintf(why, whylen, "no interface has %s", c->bind_ip);
        return false;
    }
    case HTA_BIND_TAILSCALE:
        /* Tailscale's own interface first, then any address in its range. */
        for (int pass = 0; pass < 2; pass++)
            for (uint32_t i = 0; i < n; i++)
                if (is_cgnat(ifs[i].ip) && (pass || !strncmp(ifs[i].name, "tailscale", 9))) {
                    fmt_ip(ifs[i].ip, out);
                    return true;
                }
        snprintf(why, whylen, "no Tailscale address (100.64.0.0/10) here: is tailscaled up?");
        return false;
    case HTA_BIND_LAN:
        for (uint32_t i = 0; i < n; i++)
            if (is_private(ifs[i].ip) && !virtual_iface(ifs[i].name)) { fmt_ip(ifs[i].ip, out); return true; }
        snprintf(why, whylen, "no private LAN address (10/8, 172.16/12, 192.168/16) on a real interface");
        return false;
    }
    return false;
}
