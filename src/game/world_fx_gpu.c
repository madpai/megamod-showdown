#include "world_fx_gpu.h"
#include <string.h>

bool hta_wfx_gpu_upload(hta_world_fx *w, hta_gfx *g)
{
    if (!w || !w->ready || !g) return false;
    char err[256];
    hta_wfx_gpu_free(w, g);
    /* Debris is lit world geometry and wants mipmaps; sprites and rain
     * are small and move, and do not. */
    w->gpu_debris  = hta_gfx_mesh_upload_dynamic_world(g, &w->fx.debris, err, sizeof(err));
    w->gpu_sprites = hta_gfx_mesh_upload_dynamic(g, &w->fx.sprites, err, sizeof(err));
    w->gpu_weather = hta_gfx_mesh_upload_dynamic(g, &w->weather.mesh, err, sizeof(err));
    w->weather_hw = 0;
    return w->gpu_debris && w->gpu_sprites && w->gpu_weather;
}

void hta_wfx_gpu_free(hta_world_fx *w, hta_gfx *g)
{
    if (!w) return;
    if (g) {
        if (w->gpu_debris)  hta_gfx_mesh_free(g, w->gpu_debris);
        if (w->gpu_sprites) hta_gfx_mesh_free(g, w->gpu_sprites);
        if (w->gpu_weather) hta_gfx_mesh_free(g, w->gpu_weather);
    }
    w->gpu_debris = w->gpu_sprites = w->gpu_weather = NULL;
}

uint32_t hta_wfx_gpu_draw(hta_world_fx *w, const hta_camera *cam, hta_gfx_dynamic *dyn,
                          uint32_t count, uint32_t max)
{
    if (!w || !w->ready || !cam || !dyn) return count;
    /* Nothing alive, nothing drawn: no vertex copies at all. */
    if (w->gpu_debris && count < max && hta_rigid_active(&w->rigid)) {
        hta_fx_build_debris(&w->fx, &w->rigid);
        memset(&dyn[count], 0, sizeof(dyn[count]));
        dyn[count].mesh = w->gpu_debris;
        dyn[count].vertices = w->fx.debris_verts;
        dyn[count].vertex_count = w->fx.debris.vertex_count;
        dyn[count].lit = true;
        count++;
    }
    if (w->gpu_sprites && count < max && hta_fx_live(&w->fx)) {
        hta_fx_build_sprites(&w->fx, cam);
        memset(&dyn[count], 0, sizeof(dyn[count]));
        dyn[count].mesh = w->gpu_sprites;
        dyn[count].vertices = w->fx.sprite_verts;
        dyn[count].vertex_count = w->fx.sprites.vertex_count;
        dyn[count].vertex_color = true;
        count++;
    }
    if (w->gpu_weather && count < max && w->weather.live) {
        hta_weather_build(&w->weather, cam);
        memset(&dyn[count], 0, sizeof(dyn[count]));
        dyn[count].mesh = w->gpu_weather;
        /* Copy only as far as drops have ever reached since the upload:
         * a light shower should not stream a full storm's 480 KB a frame,
         * and slots past the mark were zero when uploaded. */
        if (w->weather.live > w->weather_hw) w->weather_hw = w->weather.live;
        dyn[count].vertices = w->weather.verts;
        dyn[count].vertex_count = w->weather_hw * 4u;
        dyn[count].vertex_color = true;
        count++;
    }
    return count;
}

void hta_wfx_gpu_frame(hta_world_fx *w, hta_gfx *g, const hta_gfx_settings *s, float dt)
{
    if (!w || !g || !s || dt <= 0.0f) return;
    float ms = dt * 1000.0f;
    /* Half a second of smoothing: one hitch does not drop the resolution. */
    w->frame_ms = w->frame_ms > 0.0f ? w->frame_ms + (ms - w->frame_ms) * (dt / 0.5f < 1.0f ? dt / 0.5f : 1.0f) : ms;
    if (!s->dynamic_res || !hta_gfx_is_composed(g)) return;
    w->dynres_timer += dt;
    if (w->dynres_timer < 0.5f) return;
    w->dynres_timer = 0.0f;
    w->dynres.dynamic_res = true;
    w->dynres.target_fps = s->target_fps;
    w->dynres.dynamic_res_min = s->dynamic_res_min;
    w->dynres.render_scale = hta_gfx_render_scale(g);
    if (hta_gfx_settings_adapt(&w->dynres, w->frame_ms, s->render_scale))
        hta_gfx_set_render_scale(g, w->dynres.render_scale);
}
