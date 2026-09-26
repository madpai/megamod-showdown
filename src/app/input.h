/* What the player did since the last frame, device-neutral
 * (docs/DESKTOP_AGENT.md step 1, "input in"). Android fills it once a
 * frame from touch, the Java HUD and a gamepad; the desktop from keyboard
 * and mouse; the agent harness from its control channel. The session takes
 * it through hta_session_input and never asks a device anything.
 *
 * Portable: no platform header may be included here. */
#ifndef HTA_APP_INPUT_H
#define HTA_APP_INPUT_H

#include <stdbool.h>
#include "../engine/player.h"

typedef struct hta_session hta_session;

typedef struct {
    float move_forward, move_right;    /* -1..1 */
    float look_yaw, look_pitch;        /* radians this frame */
    /* Held now, and pressed since the last frame. A press matters apart
     * from held: the game releases a held button itself (a held fire
     * does not shoot the car you just got into) until it is pressed again. */
    bool  jump, crouch, fire, alt_fire, use;
    bool  fire_pressed, alt_pressed, use_pressed, jump_pressed, crouch_pressed;
    /* One-shot requests since the last frame. Each is taken once, however
     * many taps arrived; the game drops the ones it cannot act on. */
    bool  reload, melee, swap, zoom, grenade, fly, ability;
    int   debug;                       /* a debug-pad action + 1; 0 none */
    /* desktop only */
    bool  key_pressed[512];            /* SDL scancodes pressed THIS frame */
    bool  quit;
    bool  resized;
} hta_input;

/* Takes this frame's input into the session: one-shot requests become the
 * session's pending actions (hud_swap, hud_melee, ...), held buttons its
 * latched ones (hud_jump, hud_fire, hud_crouch, hud_alt), and `walk` gets
 * the frame's movement for hta_player_update. */
void hta_session_input(hta_session *s, const hta_input *in, hta_player_input *walk);

#endif
