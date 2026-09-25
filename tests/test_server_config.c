/* The dedicated server's config: parsing, checking, and choosing the
 * address to listen on from a machine's interfaces. */
#include "app/server_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static hta_iface IF(const char *name, unsigned a, unsigned b, unsigned c, unsigned d)
{
    hta_iface f;
    snprintf(f.name, sizeof(f.name), "%s", name);
    f.ip = (a << 24) | (b << 16) | (c << 8) | d;
    return f;
}

int main(void)
{
    hta_server_config c;
    hta_server_config_defaults(&c);
    assert(c.port == 32270 && c.max_players == 8 && c.map_count == 1 && !strcmp(c.maps[0], "bloodgulch"));

    const char *good =
        "# Friday night\n"
        "name = Friday LAN\n"
        "bind = tailscale\n"
        "port=32280\n"
        "maps = bloodgulch, de_dust2,cs_office\n"
        "mode = team   # red vs blue\n"
        "bots = 4\n"
        "bot_skill = aggressive\n"
        "classes = on\n"
        "tick_rate = 30\n"
        "password = hunter2\n"
        "colour = blue\n";
    assert(hta_server_config_parse(&c, good, strlen(good)) == 0);
    assert(!strcmp(c.name, "Friday LAN") && c.bind == HTA_BIND_TAILSCALE && c.port == 32280);
    assert(c.map_count == 3 && !strcmp(c.maps[2], "cs_office") && c.mode == 1 && c.bots == 4 && c.bot_skill == 2);
    assert(c.classes && c.tick_rate == 30 && !strcmp(c.password, "hunter2"));
    assert(strstr(c.warnings, "unknown key 'colour'") && strstr(c.warnings, "not enforced until protocol v10"));

    char out[2048];
    hta_server_config_format(&c, out, sizeof(out));
    assert(strstr(out, "maps = bloodgulch, de_dust2, cs_office") && strstr(out, "password = (set)") &&
           !strstr(out, "hunter2"));                                   /* never echoed */
    /* What it prints parses back to the same thing (password aside). */
    hta_server_config d;
    hta_server_config_defaults(&d);
    assert(hta_server_config_parse(&d, out, strlen(out)) == 0);
    assert(d.port == c.port && d.map_count == 3 && d.mode == c.mode && d.bot_skill == c.bot_skill && d.classes);

    /* Errors, each named with its line. */
    const char *bad =
        "port = 99999\n"
        "max_players = 12\n"
        "maps = Blood Gulch\n"
        "mode = deathmatch\n"
        "bind = 300.1.1.1\n"
        "just some words\n"
        "tick_rate = 5\n";
    hta_server_config_defaults(&c);
    int errs = hta_server_config_parse(&c, bad, strlen(bad));
    printf("errors (%d):\n%s", errs, c.errors);
    assert(errs == 7 && strstr(c.errors, "line 1: port") && strstr(c.errors, "line 6: expected key = value"));
    assert(c.port == 32270 && c.max_players == 8);                    /* bad values leave the defaults */

    /* Where to listen. A laptop: loopback, Wi-Fi, Docker, Tailscale. */
    hta_iface laptop[] = { IF("lo", 127, 0, 0, 1), IF("docker0", 172, 17, 0, 1), IF("wlan0", 192, 168, 1, 23),
                           IF("tailscale0", 100, 89, 1, 14) };
    char ip[16], why[160];
    hta_server_config_defaults(&c);
    assert(hta_server_bind_resolve(&c, laptop, 4, ip, why, sizeof(why)) && ip[0] == 0);          /* all */
    c.bind = HTA_BIND_TAILSCALE;
    assert(hta_server_bind_resolve(&c, laptop, 4, ip, why, sizeof(why)) && !strcmp(ip, "100.89.1.14"));
    c.bind = HTA_BIND_LAN;
    assert(hta_server_bind_resolve(&c, laptop, 4, ip, why, sizeof(why)) && !strcmp(ip, "192.168.1.23")); /* not docker's */
    c.bind = HTA_BIND_ADDRESS; snprintf(c.bind_ip, sizeof(c.bind_ip), "192.168.1.23");
    assert(hta_server_bind_resolve(&c, laptop, 4, ip, why, sizeof(why)) && !strcmp(ip, "192.168.1.23"));
    snprintf(c.bind_ip, sizeof(c.bind_ip), "10.0.0.9");
    assert(!hta_server_bind_resolve(&c, laptop, 4, ip, why, sizeof(why)) && strstr(why, "10.0.0.9"));
    /* No Tailscale on this box: say so, do not fall back to everything. */
    hta_iface desk[] = { IF("lo", 127, 0, 0, 1), IF("eth0", 10, 0, 0, 5) };
    c.bind = HTA_BIND_TAILSCALE;
    assert(!hta_server_bind_resolve(&c, desk, 2, ip, why, sizeof(why)) && strstr(why, "tailscaled"));
    c.bind = HTA_BIND_LAN;
    assert(hta_server_bind_resolve(&c, desk, 2, ip, why, sizeof(why)) && !strcmp(ip, "10.0.0.5"));
    puts("server config OK");
    return 0;
}
