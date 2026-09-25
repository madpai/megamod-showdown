/* The agent harness's portable half: the request parser (what it takes,
 * what it refuses), the event log (one valid JSON object per line), and
 * the control channel end to end over localhost TCP. */
#define _GNU_SOURCE
#include "app/agent.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int client(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons(port); sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0);
    return fd;
}

static bool wait_cmd(hta_agent *a, hta_agent_cmd *c)
{
    for (int i = 0; i < 200; i++) {
        if (hta_agent_poll(a, c)) return true;
        usleep(1000);
    }
    return false;
}

static void read_line(int fd, char *out, size_t cap)
{
    size_t n = 0;
    while (n + 1 < cap) {
        char ch;
        if (recv(fd, &ch, 1, 0) != 1) break;
        if (ch == '\n') break;
        out[n++] = ch;
    }
    out[n] = 0;
}

int main(void)
{
    hta_agent_cmd c;
    char why[96];

    /* Parsing: everything the commands use. */
    assert(hta_agent_parse("{\"cmd\":\"input\",\"forward\":1,\"right\":-0.5,\"yaw\":1.25,\"fire\":true,"
                           "\"jump\":false,\"frames\":30}", &c, why, sizeof why));
    assert(!strcmp(c.cmd, "input") && c.forward == 1.0f && c.right == -0.5f && c.has_yaw && c.yaw == 1.25f);
    assert(c.fire && !c.jump && c.frames == 30 && !c.has_pitch && !c.has_pos);
    assert(hta_agent_parse(" { \"cmd\" : \"teleport\" , \"pos\" : [ 10, -4.5, 1e0 ] } ", &c, why, sizeof why));
    assert(c.has_pos && c.pos[0] == 10.0f && c.pos[1] == -4.5f && c.pos[2] == 1.0f);
    assert(hta_agent_parse("{\"cmd\":\"shot\",\"path\":\"scratch/a \\\"b\\\".ppm\"}", &c, why, sizeof why));
    assert(!strcmp(c.path, "scratch/a \"b\".ppm"));
    assert(hta_agent_parse("{\"cmd\":\"set\",\"key\":\"gore\",\"value\":2}", &c, why, sizeof why));
    assert(!strcmp(c.key, "gore") && !strcmp(c.value, "2"));
    assert(hta_agent_parse("{\"cmd\":\"state\",\"future_field\":\"ignored\"}", &c, why, sizeof why));

    /* And refusing. */
    const char *bad[] = { "", "state", "{\"forward\":1}", "{\"cmd\":\"x\",", "{\"cmd\":\"x\" \"y\":1}",
                          "{\"cmd\":\"input\",\"forward\":7}", "{\"cmd\":\"t\",\"pos\":[1,2]}",
                          "{\"cmd\":\"t\",\"pos\":[1,2", "{\"cmd\":\"x\",\"v\":nope}", "[1]" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        why[0] = 0;
        assert(!hta_agent_parse(bad[i], &c, why, sizeof why));
        assert(why[0]);
    }

    /* Events: lines of JSON with t, frame, ev and the caller's fields. */
    static hta_agent a;
    hta_agent_init(&a);
    assert(!hta_agent_event(&a, "nothing"));        /* no log: harmless */
    hta_agent_event_end(&a);
    char path[] = "/tmp/hta_agent_events_XXXXXX";
    int tfd = mkstemp(path);
    assert(tfd >= 0);
    close(tfd);
    assert(hta_agent_open_events(&a, path));
    a.t = 12.3456; a.frame = 744;
    hta_json *j = hta_agent_event(&a, "kill");
    hta_json_str(j, "victim", "Dummy \"3\"");
    hta_json_bool(j, "gibbed", true);
    hta_agent_event_end(&a);
    hta_agent_event(&a, "match_end");
    hta_agent_event_end(&a);

    /* Control: a client connects, sends two requests and a bad one in
     * pieces; each comes out once, the bad one answered with an error. */
    assert(hta_agent_listen(&a, 0));
    uint16_t port = hta_agent_port(&a);
    assert(port);
    int fd = client(port);
    const char *msg = "{\"cmd\":\"state\"}\n{\"cmd\":\"step\",\"fra";
    assert(send(fd, msg, strlen(msg), 0) == (ssize_t)strlen(msg));
    assert(wait_cmd(&a, &c) && !strcmp(c.cmd, "state"));
    hta_agent_reply(&a, "{\"ok\":true,\"frame\":1}");
    char line[256];
    read_line(fd, line, sizeof line);
    assert(!strcmp(line, "{\"ok\":true,\"frame\":1}"));
    assert(!hta_agent_poll(&a, &c));                /* half a line: not yet */
    const char *rest = "mes\":60}\n\nnot json\n{\"cmd\":\"quit\"}\n";
    assert(send(fd, rest, strlen(rest), 0) == (ssize_t)strlen(rest));
    assert(wait_cmd(&a, &c) && !strcmp(c.cmd, "step") && c.frames == 60);
    assert(wait_cmd(&a, &c) && !strcmp(c.cmd, "quit"));
    read_line(fd, line, sizeof line);
    assert(strstr(line, "\"ok\":false") && strstr(line, "not a JSON object"));
    assert(a.commands == 3 && a.rejected == 1);

    /* The client leaves; a new one is taken. */
    close(fd);
    assert(!wait_cmd(&a, &c));
    fd = client(port);
    assert(send(fd, "{\"cmd\":\"state\"}\n", 16, 0) == 16);
    assert(wait_cmd(&a, &c) && !strcmp(c.cmd, "state"));
    close(fd);
    hta_agent_close(&a);

    FILE *f = fopen(path, "r");
    char l1[512], l2[512];
    assert(f && fgets(l1, sizeof l1, f) && fgets(l2, sizeof l2, f));
    fclose(f);
    unlink(path);
    fprintf(stderr, "%s%s", l1, l2);
    assert(!strcmp(l1, "{\"t\":12.345,\"frame\":744,\"ev\":\"kill\",\"victim\":\"Dummy \\\"3\\\"\",\"gibbed\":true}\n"));
    assert(strstr(l2, "\"ev\":\"match_end\""));
    printf("agent: ok\n");
    return 0;
}
