/* AAudio backend for the portable mixer. Android only. */
#ifndef HTA_AUDIO_ANDROID_H
#define HTA_AUDIO_ANDROID_H

#include "../engine/audio.h"

/* Opens an AAudio stream and configures `a` for the format the device gave.
 * Register clips AFTER this returns: hta_audio_init wipes the clip table. */
bool hta_audio_android_start(hta_audio *a);
void hta_audio_android_stop(void);
bool hta_audio_android_running(void);

/* Call from the game loop. Rebuilds the stream after a route change,
 * preserving the clip table. Cheap when nothing is wrong. */
void hta_audio_android_poll(hta_audio *a);

#endif
