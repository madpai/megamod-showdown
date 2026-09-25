/* MEGAMOD FAKEHOST: a scripted LAN host, for testing a joiner (the PC's
 * megamod-join, or a phone) with no phone and no game data. It speaks
 * protocol v9 as a phone host does: WORLD, GAME (scores, props), KILL
 * (gibbed), FX (detonations). Its match is a flat plain:
 *   - bots walk circles; every few seconds one is blown apart by a
 *     grenade and comes back three seconds later,
 *   - every joiner gets a unit it steers with its CONTROL packets
 *     (forward/right along its yaw at a run, on flat ground),
 *   - the first `--props` props of the joiner's map break in turn and
 *     come back (a joiner on an imported map sees its crates go).
 *
 *   megamod-fakehost [--port P] [--bots N] [--props N] [--seconds S]
 *
 * It accepts any map (map check 0): it has no world of its own. */
#define _POSIX_C_SOURCE 200809L
#include "app/scripted_match.h"
#include "net/session.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    unsigned port = 32270, bots = 3, props = 0;
    double seconds = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bots") && i + 1 < argc) bots = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--props") && i + 1 < argc) props = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else { fprintf(stderr, "usage: %s [--port P] [--bots N] [--props N] [--seconds S]\n", argv[0]); return 2; }
    }
    if (bots > 8) bots = 8;
    if (props > HTA_NET_MAX_PROPS) props = HTA_NET_MAX_PROPS;
    static hta_net_server s;
    if (!hta_net_server_open(&s, (uint16_t)port)) { fprintf(stderr, "cannot open UDP %u\n", port); return 1; }
    snprintf(s.info.name, sizeof(s.info.name), "Fakehost");
    printf("fakehost: v%u on UDP %u, %u bots, %u props\n", HTA_NET_VERSION, port, bots, props);
    fflush(stdout);

    static hta_scripted_match m;
    double t0 = now_s();
    hta_scripted_match_init(&m, "fakehost", bots, props, t0);
    for (;;) {
        double now = now_s();
        if (seconds > 0 && now - t0 > seconds) break;
        hta_net_server_pump(&s, now);
        hta_scripted_match_tick(&m, &s, now);
        struct timespec nap = { 0, 8 * 1000 * 1000 };
        nanosleep(&nap, NULL);
    }
    printf("fakehost: done, %u joiners seen steering for %u frames\n", hta_net_server_count(&s), m.controls);
    hta_net_server_close(&s);
    return 0;
}
