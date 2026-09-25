/* The agent harness (docs/DESKTOP_AGENT.md): what lets an agent run, read
 * and drive a desktop build without a person at it.
 *
 *  - An event log: one JSON object per line, as things happen
 *    ({"t":12.40,"frame":744,"ev":"kill",...}). The first thing to grep
 *    when a scripted run goes wrong.
 *  - A control channel: JSON lines over TCP, 127.0.0.1 only, one client at
 *    a time. {"cmd":"state"} in, one reply line out. The program decides
 *    what each command means; this parses and transports.
 *
 * Portable POSIX, no SDL: the sandbox, the joiner and (later) the full game
 * share it. Off unless asked for: nothing listens by default. */
#ifndef HTA_APP_AGENT_H
#define HTA_APP_AGENT_H

#include "report.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* One parsed request. Flat JSON: strings, numbers, true/false and one
 * array of up to 3 numbers ("pos"); anything else is refused. */
typedef struct {
    char  cmd[24];
    char  path[200];
    char  key[48], value[48];
    float forward, right, yaw, pitch;
    bool  has_yaw, has_pitch;
    bool  fire, alt, jump, crouch, grenade, use;
    int   frames;                     /* 0: not given */
    float pos[3];
    bool  has_pos;
} hta_agent_cmd;

/* Parses one line. False, with the reason in `why`, for anything that is
 * not a flat object with a "cmd". */
bool hta_agent_parse(const char *line, hta_agent_cmd *out, char *why, size_t whylen);

typedef struct {
    FILE    *events;
    double   t;                      /* the program's clock, set each frame */
    uint32_t frame;
    int      listen_fd, client_fd;
    char     in[4096];
    size_t   in_len;
    uint32_t events_written, commands, rejected;
    char     line[2048];              /* the event being built */
    hta_json json;
} hta_agent;

void hta_agent_init(hta_agent *a);
/* Event log to `path` ("-" for stdout). */
bool hta_agent_open_events(hta_agent *a, const char *path);
/* Control channel on 127.0.0.1:port (0: any free port; hta_agent_port says
 * which). Non-blocking. */
bool hta_agent_listen(hta_agent *a, uint16_t port);
uint16_t hta_agent_port(const hta_agent *a);
void hta_agent_close(hta_agent *a);

/* Events: begin returns the object with "t", "frame" and "ev" written;
 * add fields with hta_json_*; end writes the line. With no log open,
 * begin returns NULL and end does nothing -- callers need not check. */
hta_json *hta_agent_event(hta_agent *a, const char *ev);
void      hta_agent_event_end(hta_agent *a);

/* The next complete request, if one has arrived (accepts a waiting client
 * first). A malformed line is answered with an error here and skipped. */
bool hta_agent_poll(hta_agent *a, hta_agent_cmd *cmd);
/* One reply line (a JSON object, without the newline). Blocks briefly if
 * the client is slow; drops the client if it has gone. */
void hta_agent_reply(hta_agent *a, const char *json);

#endif
