/* Host-side tests for the portable engine core. No platform, no GPU.
 * This is the layer that will eventually hold game logic, so it must stay
 * testable without a device. */
#include "engine/engine.h"
#include <assert.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } \
} while (0)

static void send(hta_engine *e, hta_input_kind k, float x, float y)
{
    hta_input_event ev = {0};
    ev.kind = k; ev.x = x; ev.y = y;
    hta_engine_input(e, &ev);
}

int main(void)
{
    printf("engine tests\n");

    hta_engine e;
    hta_engine_init(&e);
    CHECK(e.frame_count == 0, "init zeroes frame count");
    CHECK(!hta_engine_wants_quit(&e), "does not start wanting to quit");

    hta_engine_tick(&e, 0.016);
    CHECK(e.frame_count == 1, "tick advances frame count");
    CHECK(e.time_seconds > 0.0, "tick advances clock");

    float before = e.clear_r;
    send(&e, HTA_INPUT_TOUCH_DOWN, 1.0f, 0.0f);
    CHECK(e.active_touches == 1, "touch down tracked");
    CHECK(e.clear_r > before, "touch changes render state (input reaches engine)");

    send(&e, HTA_INPUT_TOUCH_UP, 1.0f, 0.0f);
    CHECK(e.active_touches == 0, "touch up tracked");

    /* touch bookkeeping must not go negative on a stray UP */
    send(&e, HTA_INPUT_TOUCH_UP, 0.0f, 0.0f);
    CHECK(e.active_touches == 0, "stray touch up does not underflow");

    for (int i = 0; i < HTA_MAX_TOUCHES + 4; i++) send(&e, HTA_INPUT_TOUCH_DOWN, 0.5f, 0.5f);
    CHECK(e.active_touches <= HTA_MAX_TOUCHES, "touch count clamped to max");

    hta_engine gp;
    hta_engine_init(&gp);
    send(&gp, HTA_INPUT_GAMEPAD_AXIS, -0.8f, 0.0f);
    CHECK(gp.gamepad_events_seen == 1, "gamepad events counted");

    hta_engine q;
    hta_engine_init(&q);
    send(&q, HTA_INPUT_BACK, 0, 0);
    CHECK(hta_engine_wants_quit(&q), "back gesture requests clean exit");

    /* spin must stay bounded so it can't drift out of float precision */
    hta_engine s;
    hta_engine_init(&s);
    for (int i = 0; i < 100000; i++) hta_engine_tick(&s, 0.016);
    CHECK(s.tri_spin >= 0.0f && s.tri_spin < 6.2832f, "spin stays wrapped over long runs");

    printf("%s (%d failure(s))\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
