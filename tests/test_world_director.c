/* The world-effects director: game events in, debris and effects out. No
 * game data: a zeroed game with one unit is enough to stand in. */
#include "game/world_fx.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static hta_vertex V[6];
static uint32_t I[6];

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    /* A floor. */
    const float q[6][3] = { {-40,-40,0}, {40,-40,0}, {40,40,0}, {-40,-40,0}, {40,40,0}, {-40,40,0} };
    for (int i = 0; i < 6; i++) { memset(&V[i], 0, sizeof(V[i])); memcpy(V[i].pos, q[i], 12); I[i] = (uint32_t)i; }
    hta_bsp_mesh m;
    memset(&m, 0, sizeof(m));
    m.vertices = V; m.vertex_count = 6; m.indices = I; m.index_count = 6;
    m.bounds_min[0] = m.bounds_min[1] = -40; m.bounds_max[0] = m.bounds_max[1] = 40; m.bounds_max[2] = 1;
    hta_collision col;
    assert(hta_collision_build(&col, &m));

    hta_gfx_settings s;
    hta_gfx_settings_preset(&s, HTA_QUALITY_HIGH);
    hta_world_fx w;
    assert(hta_wfx_init(&w, &col, &s));
    assert(w.rigid.cap == s.max_debris && w.gib_level == 2);

    static hta_game g;
    memset(&g, 0, sizeof(g));
    g.unit_count = 1;
    g.units[0].kind = HTA_UNIT_BOT;
    g.units[0].body.eye_height = 0.62f;
    g.units[0].vitals.max_health = 75; g.units[0].vitals.max_shield = 75;

    /* A rifle death leaves a corpse: nothing here. */
    hta_game_event e;
    memset(&e, 0, sizeof(e));
    e.kind = HTA_EV_KILL; e.a = 0; e.b = -1;
    e.pos[2] = 0.35f;
    hta_wfx_game_event(&w, &e, &g);
    assert(hta_rigid_active(&w.rigid) == 0);
    /* A rocket death the game marked as gibbed: chunks and blood. */
    g.units[0].gibbed = true;
    e.amount = 1.5f; e.dir[0] = -1.0f; e.dir[2] = 0.1f;
    hta_wfx_game_event(&w, &e, &g);
    uint32_t chunks = hta_rigid_active(&w.rigid);
    assert(chunks >= 10 && hta_fx_live(&w.fx) > 10 && w.gibbed == 1);

    /* A grenade detonation on the ground: clods, dust, and the chunks
     * already lying there are thrown again. */
    hta_camera cam;
    hta_camera_init(&cam);
    cam.pos[2] = 1.0f;
    for (int i = 0; i < 300; i++) hta_wfx_update(&w, 1.0f / 60.0f, &cam);
    uint32_t asleep = 0;
    for (uint32_t i = 0; i < w.rigid.cap; i++) asleep += w.rigid.bodies[i].active && w.rigid.bodies[i].asleep;
    assert(asleep > 0);
    hta_game_event d;
    memset(&d, 0, sizeof(d));
    d.kind = HTA_EV_DETONATE; d.pool = -1; d.dir[2] = 1.0f;
    hta_wfx_game_event(&w, &d, NULL);
    uint32_t awake = 0;
    for (uint32_t i = 0; i < w.rigid.cap; i++) awake += w.rigid.bodies[i].active && !w.rigid.bodies[i].asleep;
    assert(awake > 3);
    /* A wreck sheds panels. */
    uint32_t before = hta_rigid_active(&w.rigid);
    memset(&d, 0, sizeof(d));
    d.kind = HTA_EV_WRECK; d.pos[0] = 10;
    hta_wfx_game_event(&w, &d, &g);
    assert(hta_rigid_active(&w.rigid) > before);

    /* Gore off: nothing, even for a gibbed unit. */
    hta_gfx_settings off = s;
    off.gib_level = 0;
    hta_wfx_settings(&w, &off);
    hta_wfx_reset(&w);
    hta_wfx_game_event(&w, &e, &g);
    assert(hta_rigid_active(&w.rigid) == 0);

    /* Weather: the setting wins over the map, AUTO follows the map. */
    const char *cfg = "preset = high\nweather = snow\n";
    assert(hta_wfx_parse_weather(cfg, strlen(cfg)) == HTA_WEATHER_SNOW);
    assert(hta_wfx_parse_weather("gib_level = 2\n", 14) == HTA_WFX_WEATHER_AUTO);
    assert(hta_wfx_parse_weather("weather = auto\n", 15) == HTA_WFX_WEATHER_AUTO);
    hta_wfx_set_map_weather(&w, HTA_WEATHER_RAIN, 0.5f);
    assert(w.weather.kind == HTA_WEATHER_RAIN);
    hta_wfx_choose_weather(&w, HTA_WEATHER_STORM);
    assert(w.weather.kind == HTA_WEATHER_STORM);
    hta_wfx_choose_weather(&w, HTA_WFX_WEATHER_AUTO);
    assert(w.weather.kind == HTA_WEATHER_RAIN && fabsf(w.weather.intensity - 0.5f) < 1e-6f);

    /* The frame's look: weather pulls fog in even on a preset with it
     * off, and a lightning flash brightens. */
    hta_gfx_settings nofog = s, look;
    nofog.fog = false;
    hta_wfx_choose_weather(&w, HTA_WEATHER_STORM);
    for (int i = 0; i < 60; i++) hta_wfx_update(&w, 1.0f / 60.0f, &cam);
    float amb[3] = { 0.3f, 0.3f, 0.3f }, lit[3] = { 0.8f, 0.8f, 0.8f };
    w.weather.flash = 1.0f;
    hta_wfx_frame_look(&w, &nofog, &look, amb, lit);
    assert(look.fog && look.fog_density > nofog.fog_density);
    assert(look.exposure > nofog.exposure && amb[0] > 0.3f && lit[0] > 0.8f);
    /* The frame look never changes what would rebuild the renderer. */
    assert(look.msaa == nofog.msaa && look.post == nofog.post && look.render_scale == nofog.render_scale);

    hta_wfx_free(&w);
    hta_collision_free(&col);
    puts("world director OK");
    return 0;
}
