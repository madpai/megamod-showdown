#include "gfx/gfx_settings.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void presets_are_ordered(void)
{
    hta_gfx_settings p[5];
    for (int q = 0; q < 5; q++) {
        hta_gfx_settings_preset(&p[q], (hta_quality)q);
        assert(p[q].preset == (hta_quality)q);
        /* A preset is already in range. */
        hta_gfx_settings c = p[q];
        assert(!hta_gfx_settings_clamp(&c));
    }
    /* Higher presets never do less work. */
    for (int q = 1; q < 5; q++) {
        assert(p[q].render_scale >= p[q-1].render_scale);
        assert(p[q].msaa >= p[q-1].msaa);
        assert(p[q].anisotropy >= p[q-1].anisotropy);
        assert(p[q].max_debris >= p[q-1].max_debris);
        assert(p[q].particle_density >= p[q-1].particle_density);
    }
    /* Potato is cheap: no post, but it still needs the upscale. */
    assert(!p[0].post && p[0].render_scale == 0.5f && !hta_gfx_settings_direct(&p[0]));
    /* A plain 100% no-post setup draws straight to the screen. */
    hta_gfx_settings d = p[1];
    d.render_scale = 1.0f; d.msaa = 1; d.post = false;
    assert(hta_gfx_settings_direct(&d));
    assert(!hta_gfx_settings_direct(&p[4]));
}

static void clamp_fixes_garbage(void)
{
    hta_gfx_settings s;
    hta_gfx_settings_preset(&s, HTA_QUALITY_HIGH);
    s.render_scale = 9.0f; s.msaa = 6; s.anisotropy = 0; s.exposure = NAN;
    s.tonemap = (hta_tonemap)77; s.gib_level = 9; s.fov_degrees = 10.0f;
    assert(hta_gfx_settings_clamp(&s));
    assert(s.render_scale == 2.0f && s.msaa == 4 && s.anisotropy == 1);
    assert(s.exposure == 1.0f && s.tonemap == HTA_TONEMAP_NONE);
    assert(s.gib_level == 2 && s.fov_degrees == 50.0f);
    s.msaa = 0; hta_gfx_settings_clamp(&s); assert(s.msaa == 1);
    s.msaa = 64; hta_gfx_settings_clamp(&s); assert(s.msaa == 8);
}

static void round_trip(void)
{
    hta_gfx_settings a, b;
    hta_gfx_settings_preset(&a, HTA_QUALITY_ULTRA);
    a.window_mode = HTA_WINDOW_BORDERLESS; a.fps_cap = 144;
    char buf[8192];
    size_t n = hta_gfx_settings_format(&a, buf, sizeof buf);
    assert(n > 200 && n < sizeof buf && strlen(buf) == n);
    hta_gfx_settings_preset(&b, HTA_QUALITY_POTATO);
    assert(hta_gfx_settings_parse(&b, buf, n) > 30);
    assert(b.preset == HTA_QUALITY_ULTRA);      /* display fields do not make it custom */
    assert(b.render_scale == a.render_scale && b.msaa == a.msaa);
    assert(b.tonemap == HTA_TONEMAP_ACES && b.window_mode == HTA_WINDOW_BORDERLESS);
    assert(b.fps_cap == 144);
    /* A quality field that differs from the preset comes back CUSTOM. */
    a.fog_color[0] = 0.25f;
    n = hta_gfx_settings_format(&a, buf, sizeof buf);
    hta_gfx_settings_preset(&b, HTA_QUALITY_POTATO);
    hta_gfx_settings_parse(&b, buf, n);
    assert(b.preset == HTA_QUALITY_CUSTOM && fabsf(b.fog_color[0] - 0.25f) < 1e-6f && b.msaa == 4);
    a.fog_color[0] = 0.62f;
    /* Too small a buffer is refused, not truncated. */
    assert(hta_gfx_settings_format(&a, buf, 64) == 0);

    /* Through a file, atomically. */
    char path[] = "/tmp/hta-gfx-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    assert(hta_gfx_settings_save(&a, path));
    hta_gfx_settings c; hta_gfx_settings_preset(&c, HTA_QUALITY_LOW);
    assert(hta_gfx_settings_load(&c, path));
    assert(c.msaa == 4 && c.preset == HTA_QUALITY_ULTRA);
    unlink(path);
    assert(!hta_gfx_settings_load(&c, "/nonexistent/hta.cfg"));
}

static void parse_rules(void)
{
    hta_gfx_settings s;
    hta_gfx_settings_preset(&s, HTA_QUALITY_MEDIUM);
    /* The preset line applies first even when it comes last; the override
     * before it still wins, and makes the result custom. */
    const char *t = "# comment\n  msaa = 8 \n\n; also a comment\nbogus = 3\nbloom=off\nfog_color = 1, 0.5 ,0\npreset = high\n";
    int n = hta_gfx_settings_parse(&s, t, strlen(t));
    assert(n == 4);                              /* preset, msaa, bloom, fog_color */
    assert(s.msaa == 8 && !s.bloom && s.anisotropy == 8);  /* anisotropy from HIGH */
    assert(s.fog_color[1] == 0.5f && s.preset == HTA_QUALITY_CUSTOM);
    /* Bad values are skipped, not half-applied. */
    hta_gfx_settings_preset(&s, HTA_QUALITY_LOW);
    const char *bad = "msaa = lots\ntonemap = sepia\nvsync = maybe\nfog_color = 1, 2\n";
    assert(hta_gfx_settings_parse(&s, bad, strlen(bad)) == 0);
    assert(s.preset == HTA_QUALITY_LOW && s.msaa == 1);
    /* Case-insensitive names. */
    hta_quality q;
    assert(hta_quality_from_name("ULTRA", &q) && q == HTA_QUALITY_ULTRA);
    assert(!hta_quality_from_name("extreme", &q));
}

static void suggestions(void)
{
    assert(hta_gfx_settings_suggest("llvmpipe (LLVM 17.0.6, 256 bits)", false, 0, 16) == HTA_QUALITY_POTATO);
    assert(hta_gfx_settings_suggest("Adreno (TM) 740", true, 0, 8) == HTA_QUALITY_HIGH);
    assert(hta_gfx_settings_suggest("Adreno (TM) 618", true, 0, 8) == HTA_QUALITY_LOW);
    assert(hta_gfx_settings_suggest("Mali-G78 MP14", true, 0, 8) == HTA_QUALITY_MEDIUM);
    assert(hta_gfx_settings_suggest("Mali-G52 MC2", true, 0, 8) == HTA_QUALITY_LOW);
    assert(hta_gfx_settings_suggest("Mali-G715-Immortalis MC11", true, 0, 8) == HTA_QUALITY_HIGH);
    assert(hta_gfx_settings_suggest("Adreno (TM) 740", true, 0, 4) == HTA_QUALITY_POTATO);
    assert(hta_gfx_settings_suggest("NVIDIA GeForce RTX 3070", false, 8192, 16) == HTA_QUALITY_ULTRA);
    assert(hta_gfx_settings_suggest("AMD Radeon RX 570", false, 4096, 8) == HTA_QUALITY_HIGH);
    assert(hta_gfx_settings_suggest("Intel(R) UHD Graphics 620", false, 1024, 8) == HTA_QUALITY_LOW);
    assert(hta_gfx_settings_suggest(NULL, false, 0, 0) == HTA_QUALITY_MEDIUM);
}

static void dynamic_resolution(void)
{
    hta_gfx_settings s;
    hta_gfx_settings_preset(&s, HTA_QUALITY_LOW);   /* 0.75, target 30 fps, floor 0.5 */
    assert(s.dynamic_res);
    /* Within the band: hold. */
    assert(!hta_gfx_settings_adapt(&s, 30.0f, 0.75f));
    /* Over budget: step down, never below the floor. */
    int steps = 0;
    while (hta_gfx_settings_adapt(&s, 60.0f, 0.75f)) steps++;
    assert(steps == 5 && fabsf(s.render_scale - 0.5f) < 1e-5f);
    /* Well under: climb back, never above the ceiling. */
    steps = 0;
    while (hta_gfx_settings_adapt(&s, 10.0f, 0.75f)) steps++;
    assert(steps == 10 && fabsf(s.render_scale - 0.75f) < 1e-5f);
    /* Off means off. */
    s.dynamic_res = false;
    assert(!hta_gfx_settings_adapt(&s, 100.0f, 0.75f));
}

int main(void)
{
    presets_are_ordered();
    clamp_fixes_garbage();
    round_trip();
    parse_rules();
    suggestions();
    dynamic_resolution();
    puts("gfx settings OK");
    return 0;
}
