/* The session takes a frame's device-neutral input (app/input.h).
 *
 * The pending actions keep the meaning they had when the Java HUD wrote
 * them directly: a request stays pending until the frame acts on it or
 * drops it (pause, a seat with no gun). A held button is latched: it goes
 * down on a press and up on release, and the game may let go of it early
 * -- getting into a car releases fire, getting out releases jump -- so it
 * stays up until the next press. */
#include "app/input.h"
#include "app/session.h"
#include <string.h>

static bool latch(bool was, bool held, bool pressed)
{
    if (pressed) return true;
    return held && was;
}

void hta_session_input(hta_session *s, const hta_input *in, hta_player_input *walk)
{
    if (!s || !in) return;
    s->hud_jump   = latch(s->hud_jump, in->jump, in->jump_pressed);
    s->hud_fire   = latch(s->hud_fire, in->fire, in->fire_pressed);
    s->hud_crouch = latch(s->hud_crouch, in->crouch, in->crouch_pressed);
    s->hud_alt    = latch(s->hud_alt, in->alt_fire, in->alt_pressed);
    if (in->reload)  s->hud_reload = true;
    if (in->melee)   s->hud_melee = true;
    if (in->swap)    s->hud_swap = true;
    if (in->zoom)    s->hud_zoom = true;
    if (in->grenade) s->hud_grenade = true;
    if (in->fly)     s->hud_fly = true;
    if (in->ability) s->hud_ability = true;
    if (in->debug)   s->hud_debug = in->debug;
    if (!walk) return;
    memset(walk, 0, sizeof(*walk));
    walk->move_forward = in->move_forward;
    walk->move_right = in->move_right;
    if (walk->move_forward >  1.0f) walk->move_forward =  1.0f;
    if (walk->move_forward < -1.0f) walk->move_forward = -1.0f;
    if (walk->move_right   >  1.0f) walk->move_right   =  1.0f;
    if (walk->move_right   < -1.0f) walk->move_right   = -1.0f;
    walk->look_yaw = in->look_yaw;
    walk->look_pitch = in->look_pitch;
    walk->jump = s->hud_jump;
    walk->fire = s->hud_fire;
    walk->crouch = s->hud_crouch;
}
