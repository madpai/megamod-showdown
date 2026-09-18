#include "engine.h"
#include <string.h>
#include <math.h>

void hta_engine_init(hta_engine *e)
{
    memset(e, 0, sizeof(*e));
    e->clear_r = 0.05f;
    e->clear_g = 0.06f;
    e->clear_b = 0.10f;
}

void hta_engine_input(hta_engine *e, const hta_input_event *ev)
{
    e->input_events_seen++;

    switch (ev->kind) {
    case HTA_INPUT_TOUCH_DOWN:
        if (e->active_touches < HTA_MAX_TOUCHES) e->active_touches++;
        /* Touch position tints the clear colour: visible proof that Android
         * input reached the portable engine and affected rendering. */
        e->clear_r = 0.05f + ev->x * 0.6f;
        e->clear_b = 0.10f + ev->y * 0.6f;
        break;
    case HTA_INPUT_TOUCH_MOVE:
        e->clear_r = 0.05f + ev->x * 0.6f;
        e->clear_b = 0.10f + ev->y * 0.6f;
        break;
    case HTA_INPUT_TOUCH_UP:
        if (e->active_touches > 0) e->active_touches--;
        break;
    case HTA_INPUT_GAMEPAD_AXIS:
    case HTA_INPUT_GAMEPAD_BUTTON:
        e->gamepad_events_seen++;
        e->clear_g = 0.06f + (ev->x < 0 ? -ev->x : ev->x) * 0.6f;
        break;
    case HTA_INPUT_BACK:
        e->quit_requested = true;
        break;
    default:
        break;
    }
}

void hta_engine_tick(hta_engine *e, double dt_seconds)
{
    e->frame_count++;
    e->time_seconds += dt_seconds;
    e->tri_spin = (float)fmod(e->time_seconds, 6.2831853);
}

bool hta_engine_wants_quit(const hta_engine *e) { return e->quit_requested; }
