/* Asset-free process-level UDP smoke test and headless session runner. */
#define _POSIX_C_SOURCE 200809L
#include "net/session.h"
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t running=1;
static void stop(int sig) { (void)sig; running=0; }
static double now_seconds(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (double)t.tv_sec+t.tv_nsec*1e-9; }
static void nap(void)
{ struct timespec t={0,5000000}; nanosleep(&t,NULL); }

int main(int argc, char **argv)
{
    if (argc<2) {
        fprintf(stderr,"usage: htanet server [port] [seconds] | client <numeric IPv4> [port] [seconds] | race-client <IPv4> <port> <seconds> <map-crc-hex> <content-hex>\n");
        return 2;
    }
    signal(SIGINT,stop); signal(SIGTERM,stop);
    bool server=!strcmp(argv[1],"server");
    bool client=!strcmp(argv[1],"client");
    bool race_client=!strcmp(argv[1],"race-client");
    if (!server && !client && !race_client) return 2;
    if (race_client && argc!=7) return 2;
    int port_arg=server ? 2 : 3, duration_arg=server ? 3 : 4;
    unsigned port=(unsigned)atoi(argc>port_arg ? argv[port_arg] : "32270");
    if (port<1 || port>65535) return 2;
    double duration=atof(argc>duration_arg ? argv[duration_arg] : "10");
    if (duration<=0 || duration>86400) return 2;
    double start=now_seconds(), last=start;
    if (server) {
        hta_net_server s;
        if (!hta_net_server_open(&s,(uint16_t)port)) { perror("server bind"); return 1; }
        printf("server listening UDP %u\n",port); fflush(stdout);
        while (running && now_seconds()-start<duration) {
            double now=now_seconds(); hta_net_server_pump(&s,now);
            if (now-last>=1) {
                printf("server players=%u packets=%llu/%llu bytes=%llu/%llu invalid=%llu snapshots=%llu\n",
                       hta_net_server_count(&s),
                       (unsigned long long)s.stats.packets_in,(unsigned long long)s.stats.packets_out,
                       (unsigned long long)s.stats.bytes_in,(unsigned long long)s.stats.bytes_out,
                       (unsigned long long)s.stats.invalid,
                       (unsigned long long)s.stats.snapshots_out);
                fflush(stdout); last=now;
            }
            nap();
        }
        hta_net_server_close(&s);
    } else {
        if (argc<3) return 2;
        hta_net_client c;
        if (!hta_net_client_open(&c,argv[2],(uint16_t)port)) { perror("client open"); return 1; }
        if (race_client) {
            char *end=NULL;
            unsigned long map=strtoul(argv[5],&end,16);
            if (!end || *end || map>UINT32_MAX) return 2;
            unsigned long long content=strtoull(argv[6],&end,16);
            if (!end || *end) return 2;
            c.map_crc=(uint32_t)map;
            c.content=(uint64_t)content;
        }
        printf("client connecting %s:%u\n",argv[2],port); fflush(stdout);
        double last_state=0; unsigned event_id=0;
        bool saw_go=false, saw_racer=false, saw_motion=false;
        while (running && now_seconds()-start<duration) {
            double now=now_seconds(); hta_net_client_pump(&c,now);
            if (race_client && c.reject_reason) {
                printf("race-client refused reason=%u protocol=%u\n",c.reject_reason,c.peer_version);
                hta_net_client_close(&c); return 3;
            }
            if (race_client && c.have_game && c.have_world) {
                if (c.game.mode==4 && c.game.race_phase>=2) saw_go=true;
                for (unsigned i=0;i<8;i++) if (c.game.race[i].flags&1) saw_racer=true;
                for (unsigned i=0;i<c.world.count;i++) if (c.world.entities[i].peer_id==c.id &&
                    hypotf(c.world.entities[i].velocity[0],c.world.entities[i].velocity[1])>3)
                    saw_motion=true;
            }
            if (c.connected && now-last_state>=0.05) {
                if (race_client) {
                    hta_net_control ctl={0};
                    ctl.id=c.id; ctl.flags=HTA_NET_READY;
                    ctl.forward=1; ctl.loadout[0]=ctl.loadout[1]=255;
                    hta_net_client_control(&c,&ctl);
                    last_state=now;
                    continue;
                }
                float t=(float)(now-start);
                hta_net_player p={.id=c.id,.weapon=(uint8_t)((int)t%2),
                    .flags=(t-(int)t)<0.2f ? HTA_NET_CROUCH : HTA_NET_GROUNDED,
                    .pos={cosf(t+c.id)*3,sinf(t+c.id)*3,0},
                    .velocity={-sinf(t+c.id)*3,cosf(t+c.id)*3,0},.yaw=t};
                hta_net_client_state(&c,&p); last_state=now;
                if ((int)t>=(int)(event_id+1)) {
                    hta_net_event e={c.id,HTA_NET_EVENT_FIRE,p.weapon,++event_id};
                    hta_net_client_event(&c,&e);
                }
            }
            if (now-last>=1) {
                unsigned remote=0;
                for (unsigned i=0;i<HTA_NET_MAX_PLAYERS;i++)
                    if (c.present[i] && i+1!=c.id) remote++;
                printf("client id=%u connected=%d remote=%u ping_ms=%.2f packets=%llu/%llu bytes=%llu/%llu snapshots=%llu events=%llu invalid=%llu\n",
                       c.id,c.connected,remote,c.stats.ping_ms,
                       (unsigned long long)c.stats.packets_in,(unsigned long long)c.stats.packets_out,
                       (unsigned long long)c.stats.bytes_in,(unsigned long long)c.stats.bytes_out,
                       (unsigned long long)c.stats.snapshots_in,(unsigned long long)c.stats.events_in,
                       (unsigned long long)c.stats.invalid);
                fflush(stdout); last=now;
            }
            nap();
        }
        if (race_client) {
            printf("race-client connected=%d go=%d racer=%d motion=%d worlds=%llu bytes_in=%llu bytes_out=%llu invalid=%llu\n",
                   c.connected,saw_go,saw_racer,saw_motion,
                   (unsigned long long)c.stats.worlds_in,
                   (unsigned long long)c.stats.bytes_in,(unsigned long long)c.stats.bytes_out,
                   (unsigned long long)c.stats.invalid);
            int ok=c.connected && saw_go && saw_racer && saw_motion;
            hta_net_client_close(&c); return ok ? 0 : 1;
        }
        int ok=c.connected && c.stats.snapshots_in>0;
        hta_net_client_close(&c); return ok ? 0 : 1;
    }
    return 0;
}
