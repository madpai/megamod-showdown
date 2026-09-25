#define _POSIX_C_SOURCE 200809L
#include "agent.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ------------------------------------------------------------- parsing */

static const char *ws(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++; return p; }

/* A JSON string into out (escapes \" \\ \/ \n \t kept simple); p at the
 * opening quote. Returns past the closing quote, or NULL. */
static const char *str(const char *p, char *out, size_t cap)
{
    if (*p != '"') return NULL;
    size_t n = 0;
    for (p++; *p && *p != '"'; p++) {
        char c = *p;
        if (c == '\\') {
            p++;
            c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
            if (!c) return NULL;
        }
        if (n + 1 < cap) out[n++] = c;
    }
    if (*p != '"') return NULL;
    if (cap) out[n] = 0;
    return p + 1;
}

static const char *num(const char *p, double *v)
{
    char *end;
    *v = strtod(p, &end);
    return end == p ? NULL : end;
}

/* Truncating copy (long values are cut, not refused). */
static void copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool fail(char *why, size_t len, const char *msg)
{
    if (why && len) snprintf(why, len, "%s", msg);
    return false;
}

bool hta_agent_parse(const char *line, hta_agent_cmd *out, char *why, size_t whylen)
{
    memset(out, 0, sizeof(*out));
    const char *p = ws(line);
    if (*p != '{') return fail(why, whylen, "not a JSON object");
    p = ws(p + 1);
    while (*p && *p != '}') {
        char key[32];
        if (!(p = str(p, key, sizeof key))) return fail(why, whylen, "bad key");
        p = ws(p);
        if (*p != ':') return fail(why, whylen, "expected ':'");
        p = ws(p + 1);
        if (*p == '"') {
            char v[200];
            if (!(p = str(p, v, sizeof v))) return fail(why, whylen, "bad string");
            if (!strcmp(key, "cmd")) copy(out->cmd, sizeof out->cmd, v);
            else if (!strcmp(key, "path")) copy(out->path, sizeof out->path, v);
            else if (!strcmp(key, "key")) copy(out->key, sizeof out->key, v);
            else if (!strcmp(key, "value")) copy(out->value, sizeof out->value, v);
        } else if (!strncmp(p, "true", 4) || !strncmp(p, "false", 5)) {
            bool b = *p == 't';
            p += b ? 4 : 5;
            if (!strcmp(key, "fire")) out->fire = b;
            else if (!strcmp(key, "alt")) out->alt = b;
            else if (!strcmp(key, "jump")) out->jump = b;
            else if (!strcmp(key, "crouch")) out->crouch = b;
            else if (!strcmp(key, "grenade")) out->grenade = b;
            else if (!strcmp(key, "use")) out->use = b;
        } else if (*p == '[') {
            int n = 0;
            p = ws(p + 1);
            while (*p && *p != ']') {
                double v;
                if (!(p = num(p, &v))) return fail(why, whylen, "bad array");
                if (n < 3 && !strcmp(key, "pos")) out->pos[n] = (float)v;
                n++;
                p = ws(p);
                if (*p == ',') p = ws(p + 1);
            }
            if (*p != ']') return fail(why, whylen, "unterminated array");
            p++;
            if (!strcmp(key, "pos")) {
                if (n != 3) return fail(why, whylen, "pos needs 3 numbers");
                out->has_pos = true;
            }
        } else {
            double v;
            if (!(p = num(p, &v))) return fail(why, whylen, "bad value");
            if (!strcmp(key, "forward")) out->forward = (float)v;
            else if (!strcmp(key, "right")) out->right = (float)v;
            else if (!strcmp(key, "yaw")) { out->yaw = (float)v; out->has_yaw = true; }
            else if (!strcmp(key, "pitch")) { out->pitch = (float)v; out->has_pitch = true; }
            else if (!strcmp(key, "frames")) out->frames = v < 0 ? 0 : v > 1e6 ? 1000000 : (int)v;
            else if (!strcmp(key, "value")) snprintf(out->value, sizeof out->value, "%g", v);
        }
        p = ws(p);
        if (*p == ',') p = ws(p + 1);
        else if (*p != '}') return fail(why, whylen, "expected ',' or '}'");
    }
    if (*p != '}') return fail(why, whylen, "unterminated object");
    if (!out->cmd[0]) return fail(why, whylen, "no \"cmd\"");
    if (out->forward < -1 || out->forward > 1 || out->right < -1 || out->right > 1)
        return fail(why, whylen, "forward/right must be -1..1");
    return true;
}

/* --------------------------------------------------------------- events */

void hta_agent_init(hta_agent *a)
{
    memset(a, 0, sizeof(*a));
    a->listen_fd = a->client_fd = -1;
}

bool hta_agent_open_events(hta_agent *a, const char *path)
{
    a->events = !strcmp(path, "-") ? stdout : fopen(path, "w");
    return a->events != NULL;
}

hta_json *hta_agent_event(hta_agent *a, const char *ev)
{
    if (!a || !a->events) return NULL;
    hta_json_init(&a->json, a->line, sizeof a->line);
    hta_json_num(&a->json, "t", (double)((long long)(a->t * 1000.0)) / 1000.0);
    hta_json_int(&a->json, "frame", a->frame);
    hta_json_str(&a->json, "ev", ev);
    return &a->json;
}

void hta_agent_event_end(hta_agent *a)
{
    if (!a || !a->events) return;
    size_t n = hta_json_finish(&a->json);
    fwrite(a->line, 1, n, a->events);
    fputc('\n', a->events);
    fflush(a->events);
    a->events_written++;
}

/* -------------------------------------------------------------- control */

bool hta_agent_listen(hta_agent *a, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);          /* never anything else */
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) || listen(fd, 1)) { close(fd); return false; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    a->listen_fd = fd;
    return true;
}

uint16_t hta_agent_port(const hta_agent *a)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof sa;
    if (a->listen_fd < 0 || getsockname(a->listen_fd, (struct sockaddr *)&sa, &len)) return 0;
    return ntohs(sa.sin_port);
}

static void drop_client(hta_agent *a)
{
    if (a->client_fd >= 0) close(a->client_fd);
    a->client_fd = -1;
    a->in_len = 0;
}

void hta_agent_close(hta_agent *a)
{
    drop_client(a);
    if (a->listen_fd >= 0) close(a->listen_fd);
    a->listen_fd = -1;
    if (a->events && a->events != stdout) fclose(a->events);
    a->events = NULL;
}

void hta_agent_reply(hta_agent *a, const char *json)
{
    if (a->client_fd < 0) return;
    size_t n = strlen(json);
    const char *parts[2] = { json, "\n" };
    size_t lens[2] = { n, 1 };
    for (int k = 0; k < 2; k++) {
        size_t off = 0;
        while (off < lens[k]) {
            ssize_t w = send(a->client_fd, parts[k] + off, lens[k] - off, MSG_NOSIGNAL);
            if (w > 0) { off += (size_t)w; continue; }
            if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                struct pollfd pf = { a->client_fd, POLLOUT, 0 };
                if (poll(&pf, 1, 200) > 0) continue;
            }
            drop_client(a);
            return;
        }
    }
}

bool hta_agent_poll(hta_agent *a, hta_agent_cmd *cmd)
{
    if (a->listen_fd < 0) return false;
    if (a->client_fd < 0) {
        int fd = accept(a->listen_fd, NULL, NULL);
        if (fd < 0) return false;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        a->client_fd = fd;
        a->in_len = 0;
    }
    for (;;) {
        char *nl = memchr(a->in, '\n', a->in_len);
        if (nl) {
            *nl = 0;
            size_t used = (size_t)(nl - a->in) + 1;
            char why[96];
            bool ok = a->in[0] && hta_agent_parse(a->in, cmd, why, sizeof why);
            bool blank = !a->in[0];
            memmove(a->in, a->in + used, a->in_len - used);
            a->in_len -= used;
            if (blank) continue;
            if (ok) { a->commands++; return true; }
            a->rejected++;
            char r[192];
            hta_json j;
            hta_json_init(&j, r, sizeof r);
            hta_json_bool(&j, "ok", false);
            hta_json_str(&j, "error", why);
            hta_json_finish(&j);
            hta_agent_reply(a, r);
            continue;
        }
        if (a->in_len == sizeof a->in) {             /* a line longer than we take */
            hta_agent_reply(a, "{\"ok\":false,\"error\":\"line too long\"}");
            drop_client(a);
            return false;
        }
        ssize_t r = recv(a->client_fd, a->in + a->in_len, sizeof a->in - a->in_len, 0);
        if (r > 0) { a->in_len += (size_t)r; continue; }
        if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) drop_client(a);
        return false;
    }
}
