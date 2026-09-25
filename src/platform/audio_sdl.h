/* Desktop audio out: an SDL2 device feeding the portable mixer
 * (engine/audio.h), the desktop twin of audio_android.c. hta_audio_init
 * clears the clip table, so start the device before adding clips. */
#ifndef HTA_AUDIO_SDL_H
#define HTA_AUDIO_SDL_H

#include <stdbool.h>
#include <stddef.h>
#include "../engine/audio.h"

/* Opens the default device (stereo, the device's own rate), initialises
 * `a` for it and starts mixing. False, with the reason, when there is no
 * audio (a headless box): the game runs silent. */
bool hta_audio_sdl_start(hta_audio *a, char *err, size_t errlen);
void hta_audio_sdl_stop(void);
bool hta_audio_sdl_running(void);

#endif
