/* Portable engine core. Must not include any platform or graphics header.
 * This is the top of the architecture in docs/ANDROID_PORT_INVESTIGATION.md §6. */
#ifndef HTA_ENGINE_H
#define HTA_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#define HTA_MAX_TOUCHES 8

typedef enum {
    HTA_INPUT_NONE = 0,
    HTA_INPUT_TOUCH_DOWN,
    HTA_INPUT_TOUCH_MOVE,
    HTA_INPUT_TOUCH_UP,
    HTA_INPUT_GAMEPAD_AXIS,
    HTA_INPUT_GAMEPAD_BUTTON,
    HTA_INPUT_BACK,      /* Android back gesture -> request quit */
} hta_input_kind;

typedef struct {
    hta_input_kind kind;
    int32_t id;          /* pointer id, or button/axis code */
    float x, y;          /* normalized 0..1 for touch; axis value for pads */
    bool pressed;
} hta_input_event;

typedef struct {
    /* what the engine wants drawn this frame */
    float clear_r, clear_g, clear_b;
    float tri_spin;              /* radians */
    bool  quit_requested;

    /* observed state, for logging/tests */
    uint64_t frame_count;
    double   time_seconds;
    int      active_touches;
    int      input_events_seen;
    int      gamepad_events_seen;
} hta_engine;

void hta_engine_init(hta_engine *e);
void hta_engine_input(hta_engine *e, const hta_input_event *ev);
void hta_engine_tick(hta_engine *e, double dt_seconds);
bool hta_engine_wants_quit(const hta_engine *e);

#endif
