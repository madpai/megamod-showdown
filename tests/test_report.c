/* The diagnostics report's portable half: frame-time statistics and the
 * bounded JSON writer (valid JSON always, even when cut short). */
#include "app/report.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* A minimal JSON syntax check: balanced, quoted, no trailing commas. */
static bool valid_json(const char *s)
{
    char stack[64];
    int d = 0;
    bool in_str = false, esc = false;
    char prev = 0;
    for (; *s; s++) {
        char c = *s;
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            else if ((unsigned char)c < 0x20) return false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '{' || c == '[') { if (d >= 63) return false; stack[d++] = c; }
        else if (c == '}' || c == ']') {
            if (!d || stack[d - 1] != (c == '}' ? '{' : '[')) return false;
            if (prev == ',') return false;
            d--;
        }
        if (!(c == ' ' || c == '\n')) prev = c;
    }
    return d == 0 && !in_str;
}

int main(void)
{
    /* Frame stats: a steady 60 fps with a few hitches. */
    static hta_frame_stats fs;
    hta_frame_stats_reset(&fs);
    for (int i = 0; i < 3000; i++) hta_frame_stats_add(&fs, 16.6f);
    for (int i = 0; i < 30; i++) hta_frame_stats_add(&fs, 70.0f);
    hta_frame_stats_add(&fs, 400.0f);
    /* The hitch log keeps the last 8 of the 31, with when they began:
     * 3000 frames of 16.6 ms, then 30 of 70 ms, then the 400 ms one. */
    assert(fs.hitch_next == 31u);
    assert(fs.frame == 3031u);
    {
        uint32_t last = (fs.hitch_next - 1u) % HTA_FRAME_HITCH_LOG;
        assert(fs.hitch_log[last].ms == 400.0f && fs.hitch_log[last].frame == 3030u);
        assert(fabsf(fs.hitch_log[last].at_s - (3000.0f * 0.0166f + 30.0f * 0.070f)) < 0.01f);
        char hb[1024];
        hta_json hj;
        hta_json_init(&hj, hb, sizeof(hb));
        hta_json_object(&hj, NULL);
        hta_json_hitches(&hj, "hitches", &fs);
        size_t hn = hta_json_finish(&hj);
        assert(hj.ok && hn > 0);
        assert(strstr(hb, "\"frame\":3023") && strstr(hb, "\"ms\":400"));  /* oldest kept, newest */
        assert(!strstr(hb, "\"frame\":3022}"));
    }
    hta_frame_stats_add(&fs, NAN);                   /* ignored */
    assert(fs.session.count == 3031 && fs.session.hitches == 31);
    assert(fabsf(hta_frame_hist_percentile(&fs.session, 50) - 17.0f) < 0.01f);
    assert(hta_frame_hist_percentile(&fs.session, 99) > 60.0f);
    assert(hta_frame_hist_percentile(&fs.session, 100) == 400.0f);
    /* The minute rolls over into last_minute. */
    assert(fs.last_minute.count == 0);
    for (int i = 0; i < 4000; i++) hta_frame_stats_add(&fs, 16.6f);
    assert(fs.last_minute.count > 3000 && fs.minute.count < fs.session.count);

    /* JSON: nesting, escaping, numbers. */
    char buf[4096];
    hta_json j;
    hta_json_init(&j, buf, sizeof(buf));
    hta_json_str(&j, "build", "abc\"def\\\n\x01");
    hta_json_int(&j, "protocol", 9);
    hta_json_num(&j, "nan", NAN);
    hta_json_bool(&j, "ok", true);
    hta_json_object(&j, "video");
    hta_json_str(&j, "preset", "high");
    hta_json_array(&j, "list");
    hta_json_int(&j, NULL, 1); hta_json_int(&j, NULL, 2);
    hta_json_end_array(&j);
    hta_json_end_object(&j);
    hta_json_frames(&j, "frames", &fs.session);
    size_t n = hta_json_finish(&j);
    printf("%s\n", buf);
    assert(n == strlen(buf) && j.ok && valid_json(buf));
    assert(strstr(buf, "\"build\":\"abc\\\"def\\\\\\u000a\\u0001\"") && strstr(buf, "\"nan\":null"));
    assert(strstr(buf, "\"list\":[1,2]") && strstr(buf, "\"p99_ms\":"));

    /* Too small: cut short, still valid, and it says so. Also with an
     * array left open by the caller. */
    for (size_t cap = 24; cap < 400; cap += 7) {
        char small[400];
        hta_json_init(&j, small, cap);
        hta_json_str(&j, "a", "a fairly long string that will not always fit");
        hta_json_array(&j, "arr");
        for (int i = 0; i < 40; i++) hta_json_str(&j, NULL, "item");
        hta_json_object(&j, NULL);
        hta_json_frames(&j, "f", &fs.session);
        size_t m = hta_json_finish(&j);
        assert(m < cap && valid_json(small));
    }
    puts("report OK");
    return 0;
}
