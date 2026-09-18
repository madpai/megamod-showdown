/* Android platform layer: the ONLY file in the PoC that knows about Android.
 * Owns the NativeActivity lifecycle, feeds input into the portable engine,
 * and drives the renderer. See investigation §6. */
#include "platform.h"
#include "../engine/engine.h"
#include "../gfx/gfx.h"

#include <android/log.h>
#include <android/input.h>
#include <android_native_app_glue.h>

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#define TAG "halo-trial-android"

void hta_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, TAG, fmt, ap);
    va_end(ap);
}

double hta_time_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

bool hta_probe_fixed_map(uint64_t addr, size_t len)
{
    void *want = (void *)(uintptr_t)addr;
    void *got = mmap(want, len, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (got == MAP_FAILED) {
        hta_log("[probe] MAP_FIXED_NOREPLACE at 0x%llx FAILED (errno path)",
                (unsigned long long)addr);
        return false;
    }
    bool exact = (got == want);
    /* touch it, to be sure it is really usable memory */
    if (exact) ((volatile unsigned char *)got)[0] = 0xAB;
    hta_log("[probe] mmap at 0x%llx -> %p (%s)", (unsigned long long)addr, got,
            exact ? "EXACT: tag cache can keep native 32-bit pointers"
                  : "MOVED: must translate tag pointers to offsets");
    munmap(got, len);
    return exact;
}

/* ------------------------------------------------------------------ */

typedef struct {
    struct android_app *app;
    hta_engine   engine;
    hta_gfx     *gfx;
    bool         has_window;
    double       last_time;
    bool         probe_done;
} hta_android;

static void emit(hta_android *s, hta_input_kind kind, int32_t id, float x, float y, bool pressed)
{
    hta_input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = kind; ev.id = id; ev.x = x; ev.y = y; ev.pressed = pressed;
    hta_engine_input(&s->engine, &ev);
}

static int32_t on_input(struct android_app *app, AInputEvent *event)
{
    hta_android *s = (hta_android *)app->userData;
    int32_t src  = AInputEvent_getSource(event);
    int32_t type = AInputEvent_getType(event);

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        /* Gamepad / joystick sticks arrive as motion events too. */
        if ((src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK) {
            float lx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            float ly = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            emit(s, HTA_INPUT_GAMEPAD_AXIS, AMOTION_EVENT_AXIS_X, lx, ly, false);
            hta_log("[input] gamepad axis x=%.2f y=%.2f", lx, ly);
            return 1;
        }

        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        size_t  count  = AMotionEvent_getPointerCount(event);
        int32_t w = ANativeWindow_getWidth(app->window);
        int32_t h = ANativeWindow_getHeight(app->window);
        if (w <= 0) w = 1;
        if (h <= 0) h = 1;

        for (size_t i = 0; i < count; i++) {
            float nx = AMotionEvent_getX(event, i) / (float)w;
            float ny = AMotionEvent_getY(event, i) / (float)h;
            int32_t id = AMotionEvent_getPointerId(event, i);
            hta_input_kind k = HTA_INPUT_TOUCH_MOVE;
            if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN)
                k = HTA_INPUT_TOUCH_DOWN;
            else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP ||
                     action == AMOTION_EVENT_ACTION_CANCEL)
                k = HTA_INPUT_TOUCH_UP;
            emit(s, k, id, nx, ny, k != HTA_INPUT_TOUCH_UP);
        }
        if (action == AMOTION_EVENT_ACTION_DOWN)
            hta_log("[input] touch down (%zu pointer(s))", count);
        return 1;
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(event);
        bool down = (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN);
        if (code == AKEYCODE_BACK) {
            if (down) {
                hta_log("[input] BACK -> requesting clean exit");
                emit(s, HTA_INPUT_BACK, code, 0, 0, true);
            }
            return 1;
        }
        emit(s, HTA_INPUT_GAMEPAD_BUTTON, code, down ? 1.0f : 0.0f, 0, down);
        hta_log("[input] key %d %s", code, down ? "down" : "up");
        return 1;
    }
    return 0;
}

static void on_cmd(struct android_app *app, int32_t cmd)
{
    hta_android *s = (hta_android *)app->userData;

    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window) {
            hta_log("[app] INIT_WINDOW %dx%d",
                    ANativeWindow_getWidth(app->window), ANativeWindow_getHeight(app->window));
            s->gfx = hta_gfx_create(app->window);
            s->has_window = (s->gfx != NULL);
            if (s->has_window)
                hta_log("[app] renderer ready on: %s", hta_gfx_device_name(s->gfx));

            if (!s->probe_done) {
                /* Stage-2 answer, investigation §5.1 */
                hta_probe_fixed_map(0x40440000ull, 64u * 1024u * 1024u);
                s->probe_done = true;
            }
        }
        break;

    case APP_CMD_TERM_WINDOW:
        hta_log("[app] TERM_WINDOW");
        hta_gfx_destroy(s->gfx);
        s->gfx = NULL;
        s->has_window = false;
        break;

    case APP_CMD_GAINED_FOCUS: hta_log("[app] focus gained"); break;
    case APP_CMD_LOST_FOCUS:   hta_log("[app] focus lost");   break;
    default: break;
    }
}

void android_main(struct android_app *app)
{
    hta_android state;
    memset(&state, 0, sizeof(state));
    state.app = app;

    app->userData     = &state;
    app->onAppCmd     = on_cmd;
    app->onInputEvent = on_input;

    hta_engine_init(&state.engine);
    state.last_time = hta_time_seconds();

    hta_log("[app] android_main entered; pointer size = %zu bytes", sizeof(void *));

    while (1) {
        int events;
        struct android_poll_source *source;
        /* block only when we have nothing to draw */
        int timeout = state.has_window ? 0 : -1;
        while (ALooper_pollOnce(timeout, NULL, &events, (void **)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) goto done;
            timeout = 0;
        }

        double now = hta_time_seconds();
        double dt = now - state.last_time;
        state.last_time = now;
        hta_engine_tick(&state.engine, dt);

        if (state.has_window && state.gfx) {
            if (!hta_gfx_draw(state.gfx, &state.engine)) {
                /* surface changed: rebuild the renderer */
                hta_log("[app] rebuilding renderer");
                hta_gfx_destroy(state.gfx);
                state.gfx = hta_gfx_create(app->window);
                state.has_window = (state.gfx != NULL);
            }
        }

        if (state.engine.frame_count % 120 == 0 && state.engine.frame_count)
            hta_log("[app] frame %llu t=%.1fs touches=%d inputs=%d pads=%d",
                    (unsigned long long)state.engine.frame_count,
                    state.engine.time_seconds, state.engine.active_touches,
                    state.engine.input_events_seen, state.engine.gamepad_events_seen);

        if (hta_engine_wants_quit(&state.engine)) {
            hta_log("[app] engine requested quit -> ANativeActivity_finish");
            ANativeActivity_finish(app->activity);
        }
    }

done:
    hta_log("[app] shutting down cleanly after %llu frames",
            (unsigned long long)state.engine.frame_count);
    hta_gfx_destroy(state.gfx);
    state.gfx = NULL;
}
