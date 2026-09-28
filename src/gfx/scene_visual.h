/* Small bridge from package-authored visual data to the shared Vulkan scene.
 * A light's relay is an existing X8 logical flag. Rendering reads the
 * replicated state; it never evaluates gameplay on a joiner. */
#ifndef HTA_SCENE_VISUAL_H
#define HTA_SCENE_VISUAL_H
#include "gfx.h"
#include "../asset/world_def.h"
#include "../engine/world_entities.h"
#include <string.h>

static inline void hta_scene_apply_visual(hta_scene *s, const hta_world_defs *d,
                                           const hta_world_entities *w, const float eye[3])
{
    if (!s || !d || !d->has_environment) return;
    s->authored = true;
    memcpy(s->ambient, d->environment.ambient, sizeof(s->ambient));
    memcpy(s->clear, d->environment.clear, sizeof(s->clear));
    memcpy(s->fog_color, d->environment.fog_color, sizeof(s->fog_color));
    s->fog_density = d->environment.fog_density;
    s->fog_start = d->environment.fog_start;
    s->light_count = 0;
    float chosen[HTA_SCENE_MAX_LIGHTS];
    for (uint32_t i = 0; i < HTA_SCENE_MAX_LIGHTS; i++) chosen[i] = 1e30f;
    for (uint32_t i = 0; i < d->light_count; i++) {
        const hta_wlight_def *l = &d->light[i];
        if (l->relay && hta_went_relay_active(w, l->relay - 1) != HTA_WRELAY_ACTIVE) continue;
        float dx = l->position[0] - eye[0], dy = l->position[1] - eye[1], dz = l->position[2] - eye[2];
        float dist2 = dx*dx + dy*dy + dz*dz;
        float reach = l->range + 4.0f;
        if (dist2 > reach * reach) continue;
        uint32_t at = 0;
        while (at < s->light_count && chosen[at] <= dist2) at++;
        if (at >= HTA_SCENE_MAX_LIGHTS) continue;
        uint32_t end = s->light_count < HTA_SCENE_MAX_LIGHTS ? s->light_count++ : HTA_SCENE_MAX_LIGHTS - 1;
        for (uint32_t j = end; j > at; j--) { chosen[j] = chosen[j-1]; s->lights[j] = s->lights[j-1]; }
        chosen[at] = dist2;
        hta_scene_light *out = &s->lights[at];
        memcpy(out->position, l->position, sizeof(out->position)); out->range = l->range;
        memcpy(out->color, l->color, sizeof(out->color)); out->intensity = l->intensity;
        memcpy(out->direction, l->direction, sizeof(out->direction));
        out->inner_cos = l->inner_cos; out->outer_cos = l->spot ? l->outer_cos : -1.0f;
    }
}
#endif
