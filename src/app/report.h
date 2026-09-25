/* Diagnostics for agents: what a device measured, as one JSON document.
 *
 * The platform keeps an hta_frame_stats per session (one add per frame),
 * and when the player asks for a report -- or after a crash -- builds the
 * native half of it with hta_json and the helpers here. The Android side
 * adds the device, memory, thermal state and log lines, and posts it to the
 * owner's sideload server (scripts/serve_poc.py /report), where an agent
 * reads it (.claude/skills/read-report).
 *
 * Portable, allocation-free after init: safe to run every frame. */
#ifndef HTA_APP_REPORT_H
#define HTA_APP_REPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Frame times in 0.5 ms bins up to 100 ms, then one bin for "longer". */
#define HTA_FRAME_BINS 201u
#define HTA_FRAME_BIN_MS 0.5f

typedef struct {
    uint32_t bins[HTA_FRAME_BINS];
    uint32_t count;
    double   sum_ms, max_ms;
    uint32_t hitches;           /* frames over 50 ms */
} hta_frame_hist;

typedef struct {
    hta_frame_hist session;     /* since the match started */
    hta_frame_hist minute;      /* the current minute ... */
    hta_frame_hist last_minute; /* ... and the one before it, whole */
    double   minute_ms;         /* how far into the current minute */
} hta_frame_stats;

void  hta_frame_stats_reset(hta_frame_stats *s);
void  hta_frame_stats_add(hta_frame_stats *s, float frame_ms);
/* The frame time `pct` percent of frames were at or under (0..100). */
float hta_frame_hist_percentile(const hta_frame_hist *h, float pct);

/* A bounded JSON writer: never overflows `buf`; `ok` goes false (and the
 * output is cut at a valid point) when it would. Strings are escaped. */
typedef struct {
    char   *buf;
    size_t  cap, len;
    bool    ok;
    bool    first[16];          /* per nesting level: no comma yet */
    char    closer[16];         /* per nesting level: '}' or ']' */
    int     depth;
} hta_json;

void hta_json_init(hta_json *j, char *buf, size_t cap);
void hta_json_object(hta_json *j, const char *key);      /* key NULL at top level / in arrays */
void hta_json_array(hta_json *j, const char *key);
void hta_json_end_object(hta_json *j);
void hta_json_end_array(hta_json *j);
void hta_json_str(hta_json *j, const char *key, const char *v);
void hta_json_num(hta_json *j, const char *key, double v);
void hta_json_int(hta_json *j, const char *key, long long v);
void hta_json_bool(hta_json *j, const char *key, bool v);
/* A frame histogram as { count, mean, p50, p90, p95, p99, max, hitches,
 * fps_mean, over_16ms, over_33ms } -- the numbers an agent compares. */
void hta_json_frames(hta_json *j, const char *key, const hta_frame_hist *h);
/* Closes every open level; returns the text length. The text is valid
 * JSON even when it was cut short -- `ok` says whether everything fit. */
size_t hta_json_finish(hta_json *j);

#endif
