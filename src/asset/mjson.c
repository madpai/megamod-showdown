/* The bounded manifest JSON reader (mjson.h). Moved out of world_def.c in
 * X4 unchanged, so the package parser reads with the same rules. */
#include "mjson.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void hta_mj_ws(hta_mj *r) { while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r')) r->p++; }
bool hta_mj_eat(hta_mj *r, char c) { hta_mj_ws(r); if (r->p < r->end && *r->p == c) { r->p++; return true; } return false; }
bool hta_mj_peek(hta_mj *r, char c) { hta_mj_ws(r); return r->p < r->end && *r->p == c; }

static bool str_body(hta_mj *r, char *out, size_t cap, size_t *len);

bool hta_mj_str(hta_mj *r, char *out, size_t cap)
{
    size_t n = 0;
    bool ok = str_body(r, out, cap, &n);
    if (out && cap && !ok) out[n < cap ? n : cap - 1] = 0;   /* never left unterminated */
    return ok;
}

static bool str_body(hta_mj *r, char *out, size_t cap, size_t *len)
{
    size_t n = 0;
    if (!hta_mj_eat(r, '"')) return false;
    while (r->p < r->end) {
        uint8_t c = *r->p++;
        if (c == '"') { if (out) out[n] = 0; return true; }
        if (c < 0x20) return false;
        if (c == '\\') {
            if (r->p >= r->end) return false;
            uint8_t e = *r->p++;
            if (e == 'u') {
                unsigned v = 0;
                for (int k = 0; k < 4; k++) {
                    if (r->p >= r->end) return false;
                    uint8_t h = *r->p++;
                    v = v * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 :
                                  h >= 'A' && h <= 'F' ? h - 'A' + 10 : 99);
                    if (v > 0xFFFF) return false;
                }
                c = v >= 0x20 && v < 0x7F ? (uint8_t)v : 0;
            } else {
                const char *m = strchr("\"\\/bfnrt", e);
                if (!m || !e) return false;
                c = (uint8_t)"\"\\/\b\f\n\r\t"[m - "\"\\/bfnrt"];
            }
            if (out && (c < 0x20 || c >= 0x7F)) return false;
        } else if (out && c >= 0x7F) return false;
        if (out) { if (n + 1 >= cap) return false; out[n++] = (char)c; *len = n; }
    }
    return false;
}

bool hta_mj_num(hta_mj *r, double *out)
{
    hta_mj_ws(r);
    char buf[40]; size_t n = 0;
    while (r->p < r->end && n + 1 < sizeof(buf) && strchr("+-.0123456789eE", *r->p) && *r->p) buf[n++] = (char)*r->p++;
    if (!n || (r->p < r->end && strchr("+-.0123456789eE", *r->p) && *r->p)) return false;
    buf[n] = 0;
    char *e;
    double v = strtod(buf, &e);
    if (*e || !isfinite(v)) return false;
    *out = v;
    return true;
}

bool hta_mj_skip(hta_mj *r)
{
    int depth = 0;
    do {
        hta_mj_ws(r);
        if (r->p >= r->end) return false;
        uint8_t c = *r->p;
        if (c == '"') { if (!hta_mj_str(r, NULL, 0)) return false; }
        else if (c == '{' || c == '[') {
            if (++depth > HTA_MJ_MAX_DEPTH) return false;
            r->p++;
            if (hta_mj_eat(r, c == '{' ? '}' : ']')) { depth--; goto next; }
            if (c == '{') { if (!hta_mj_str(r, NULL, 0) || !hta_mj_eat(r, ':')) return false; }
            continue;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            double d;
            const uint8_t *at = r->p;
            if (!hta_mj_num(r, &d)) {
                if (!r->lenient) return false;
                r->p = at;
                size_t n = 0;
                while (r->p < r->end && n < 40 && strchr("+-.0123456789eE", *r->p) && *r->p) { r->p++; n++; }
                if (!n || n >= 40) return false;
            }
        }
        else if ((size_t)(r->end - r->p) >= 4 && !memcmp(r->p, "true", 4)) r->p += 4;
        else if ((size_t)(r->end - r->p) >= 5 && !memcmp(r->p, "false", 5)) r->p += 5;
        else if ((size_t)(r->end - r->p) >= 4 && !memcmp(r->p, "null", 4)) r->p += 4;
        else return false;
next:
        /* After a value: a comma continues the container, a close ends it. */
        while (depth > 0) {
            hta_mj_ws(r);
            if (r->p >= r->end) return false;
            if (*r->p == ',') {
                r->p++;
                /* In an object the next member starts with its key. The
                 * container kind is not tracked; a key is a string then ':'. */
                const uint8_t *save = r->p;
                if (hta_mj_peek(r, '"') && hta_mj_str(r, NULL, 0) && hta_mj_eat(r, ':')) break;
                r->p = save;
                break;
            }
            if (*r->p == '}' || *r->p == ']') { r->p++; depth--; continue; }
            return false;
        }
    } while (depth > 0);
    return true;
}
