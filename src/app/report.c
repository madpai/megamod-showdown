#include "report.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- frame times ----------------------------------------------------------- */

static void hist_add(hta_frame_hist *h, float ms)
{
    if (!(ms >= 0.0f)) return;                          /* NaN or negative */
    uint32_t b = (uint32_t)(ms / HTA_FRAME_BIN_MS);
    if (b >= HTA_FRAME_BINS) b = HTA_FRAME_BINS - 1u;
    h->bins[b]++;
    h->count++;
    h->sum_ms += ms;
    if (ms > h->max_ms) h->max_ms = ms;
    if (ms > 50.0f) h->hitches++;
}

void hta_frame_stats_reset(hta_frame_stats *s)
{
    if (s) memset(s, 0, sizeof(*s));
}

void hta_frame_stats_add(hta_frame_stats *s, float frame_ms)
{
    if (!s) return;
    hist_add(&s->session, frame_ms);
    hist_add(&s->minute, frame_ms);
    s->minute_ms += frame_ms > 0.0f ? frame_ms : 0.0f;
    if (s->minute_ms >= 60000.0) {
        s->last_minute = s->minute;
        memset(&s->minute, 0, sizeof(s->minute));
        s->minute_ms = 0.0;
    }
}

float hta_frame_hist_percentile(const hta_frame_hist *h, float pct)
{
    if (!h || !h->count) return 0.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    uint64_t want = (uint64_t)ceil((double)h->count * pct / 100.0);
    if (want < 1) want = 1;
    uint64_t seen = 0;
    for (uint32_t b = 0; b < HTA_FRAME_BINS; b++) {
        seen += h->bins[b];
        if (seen >= want)
            /* The bin's upper edge: "at or under this"; the last bin is the max. */
            return b == HTA_FRAME_BINS - 1u ? (float)h->max_ms : (float)(b + 1u) * HTA_FRAME_BIN_MS;
    }
    return (float)h->max_ms;
}

/* ---- JSON ------------------------------------------------------------------ */

/* Room kept for closing every level, so a cut document still parses. */
#define JSON_RESERVE 20u

void hta_json_init(hta_json *j, char *buf, size_t cap)
{
    memset(j, 0, sizeof(*j));
    j->buf = buf; j->cap = cap; j->ok = cap > JSON_RESERVE + 2u;
    if (j->ok) { buf[0] = '{'; buf[1] = 0; j->len = 1; }
    j->first[0] = true;
}

static bool room(hta_json *j, size_t n)
{
    return j->ok && j->len + n + JSON_RESERVE < j->cap;
}

static void raw(hta_json *j, const char *s, size_t n)
{
    memcpy(j->buf + j->len, s, n);
    j->len += n;
    j->buf[j->len] = 0;
}

/* The size a string takes quoted and escaped. */
static size_t esc_len(const char *s)
{
    size_t n = 2;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        n += (*p == '"' || *p == '\\') ? 2 : *p < 0x20 ? 6 : 1;
    return n;
}

static void esc(hta_json *j, const char *s)
{
    raw(j, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char tmp[8];
        if (*p == '"' || *p == '\\') { tmp[0] = '\\'; tmp[1] = (char)*p; raw(j, tmp, 2); }
        else if (*p < 0x20) { snprintf(tmp, sizeof(tmp), "\\u%04x", *p); raw(j, tmp, 6); }
        else raw(j, (const char *)p, 1);
    }
    raw(j, "\"", 1);
}

/* Comma and key before a value; false (and nothing written) if the whole
 * item, `value_len` included, would not fit. */
static bool begin(hta_json *j, const char *key, size_t value_len)
{
    size_t need = (j->first[j->depth] ? 0 : 1) + (key ? esc_len(key) + 1 : 0) + value_len;
    if (!room(j, need)) { j->ok = false; return false; }
    if (!j->first[j->depth]) raw(j, ",", 1);
    j->first[j->depth] = false;
    if (key) { esc(j, key); raw(j, ":", 1); }
    return true;
}

static void open_level(hta_json *j, const char *key, char c)
{
    if (j->depth >= 15 || !begin(j, key, 1)) { j->ok = false; return; }
    raw(j, &c, 1);
    j->depth++;
    j->first[j->depth] = true;
    j->closer[j->depth] = c == '{' ? '}' : ']';
}

void hta_json_object(hta_json *j, const char *key) { open_level(j, key, '{'); }
void hta_json_array(hta_json *j, const char *key) { open_level(j, key, '['); }

static void close_level(hta_json *j, char c)
{
    if (j->depth <= 0) return;
    c = j->closer[j->depth];            /* whatever was opened, closed right */
    /* The reserve guarantees a closer always fits. */
    raw(j, &c, 1);
    j->depth--;
}

void hta_json_end_object(hta_json *j) { close_level(j, '}'); }
void hta_json_end_array(hta_json *j) { close_level(j, ']'); }

void hta_json_str(hta_json *j, const char *key, const char *v)
{
    if (!v) v = "";
    if (begin(j, key, esc_len(v))) esc(j, v);
}

void hta_json_num(hta_json *j, const char *key, double v)
{
    char tmp[40];
    int n = isfinite(v) ? snprintf(tmp, sizeof(tmp), "%.6g", v) : snprintf(tmp, sizeof(tmp), "null");
    if (n > 0 && begin(j, key, (size_t)n)) raw(j, tmp, (size_t)n);
}

void hta_json_int(hta_json *j, const char *key, long long v)
{
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%lld", v);
    if (n > 0 && begin(j, key, (size_t)n)) raw(j, tmp, (size_t)n);
}

void hta_json_bool(hta_json *j, const char *key, bool v)
{
    if (begin(j, key, v ? 4 : 5)) raw(j, v ? "true" : "false", v ? 4 : 5);
}

void hta_json_frames(hta_json *j, const char *key, const hta_frame_hist *h)
{
    hta_json_object(j, key);
    hta_json_int(j, "count", h->count);
    double mean = h->count ? h->sum_ms / h->count : 0.0;
    hta_json_num(j, "mean_ms", mean);
    hta_json_num(j, "fps_mean", mean > 0.0 ? 1000.0 / mean : 0.0);
    hta_json_num(j, "p50_ms", hta_frame_hist_percentile(h, 50));
    hta_json_num(j, "p90_ms", hta_frame_hist_percentile(h, 90));
    hta_json_num(j, "p95_ms", hta_frame_hist_percentile(h, 95));
    hta_json_num(j, "p99_ms", hta_frame_hist_percentile(h, 99));
    hta_json_num(j, "max_ms", h->max_ms);
    uint32_t over16 = 0, over33 = 0;
    for (uint32_t b = 0; b < HTA_FRAME_BINS; b++) {
        float lo = (float)b * HTA_FRAME_BIN_MS;
        if (lo >= 16.7f) over16 += h->bins[b];
        if (lo >= 33.3f) over33 += h->bins[b];
    }
    hta_json_int(j, "over_16ms", over16);
    hta_json_int(j, "over_33ms", over33);
    hta_json_int(j, "hitches_over_50ms", h->hitches);
    hta_json_end_object(j);
}

size_t hta_json_finish(hta_json *j)
{
    if (!j->buf || j->cap <= JSON_RESERVE + 2u) return 0;
    while (j->depth > 0) close_level(j, '}');
    raw(j, "}", 1);
    return j->len;
}
