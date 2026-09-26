/* The session takes device-neutral input (app/input.c): a request stays
 * pending until the game acts on it, a held button is latched and stays
 * released after the game lets go of it until the next press, and the
 * walk input is the frame's movement, clamped. What the Android HUD used
 * to write straight into the session, now in one place a desktop, a test
 * or an agent can drive. */
#include "app/input.h"
#include "app/session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static hta_player_input frame(hta_session *s, const hta_input *in)
{
    hta_player_input walk;
    hta_session_input(s, in, &walk);
    return walk;
}

int main(void)
{
    hta_session *s = calloc(1, sizeof(*s));
    assert(s);
    hta_input in, none;
    memset(&none, 0, sizeof(none));

    /* A tap is a pending request: it waits for the game, once. */
    memset(&in, 0, sizeof(in));
    in.swap = in.grenade = in.fly = true;
    frame(s, &in);
    assert(s->hud_swap && s->hud_grenade && s->hud_fly && !s->hud_melee);
    frame(s, &none);
    assert(s->hud_swap);                  /* not acted on yet: still pending */
    s->hud_swap = false;                  /* the game takes it */
    frame(s, &none);
    assert(!s->hud_swap);                 /* ...and it does not come back */

    /* Every request lands on its own flag. */
    memset(&in, 0, sizeof(in));
    in.reload = in.melee = in.zoom = in.ability = true;
    in.debug = 3;
    frame(s, &in);
    assert(s->hud_reload && s->hud_melee && s->hud_zoom && s->hud_ability && s->hud_debug == 3);
    frame(s, &none);
    assert(s->hud_debug == 3);            /* a debug action waits too */

    /* Held fire: down on the press, held while held. */
    memset(&in, 0, sizeof(in));
    in.fire = in.fire_pressed = true;
    assert(frame(s, &in).fire);
    in.fire_pressed = false;
    assert(frame(s, &in).fire);
    /* The game lets go of it (into a car): released until pressed again,
     * though the finger is still down. */
    s->hud_fire = false;
    assert(!frame(s, &in).fire);
    assert(!frame(s, &in).fire);
    in.fire_pressed = true;               /* a new press */
    assert(frame(s, &in).fire);
    in.fire = in.fire_pressed = false;    /* release */
    assert(!frame(s, &in).fire && !s->hud_fire);

    /* A tap shorter than a frame still counts, for that frame. */
    memset(&in, 0, sizeof(in));
    in.jump_pressed = true;
    assert(frame(s, &in).jump);
    assert(!frame(s, &none).jump);

    /* Jump released on the way out of a car stays released. */
    memset(&in, 0, sizeof(in));
    in.jump = in.jump_pressed = true;
    frame(s, &in);
    s->hud_jump = false;
    in.jump_pressed = false;
    assert(!frame(s, &in).jump);

    /* Crouch and the vehicle's second trigger latch the same way. */
    memset(&in, 0, sizeof(in));
    in.crouch = in.crouch_pressed = in.alt_fire = in.alt_pressed = true;
    assert(frame(s, &in).crouch && s->hud_alt);
    s->hud_alt = false;
    in.crouch_pressed = in.alt_pressed = false;
    assert(frame(s, &in).crouch && !s->hud_alt);

    /* Movement is clamped; look passes through. */
    memset(&in, 0, sizeof(in));
    in.move_forward = 1.7f; in.move_right = -2.0f;
    in.look_yaw = 0.25f; in.look_pitch = -0.1f;
    hta_player_input w = frame(s, &in);
    assert(w.move_forward == 1.0f && w.move_right == -1.0f);
    assert(w.look_yaw == 0.25f && w.look_pitch == -0.1f);

    /* NULL walk: the requests still land. */
    memset(&in, 0, sizeof(in));
    s->hud_melee = false;
    in.melee = true;
    hta_session_input(s, &in, NULL);
    assert(s->hud_melee);

    free(s);
    puts("input OK");
    return 0;
}
