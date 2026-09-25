#include "audio_sdl.h"
#include <SDL.h>
#include <stdio.h>

static SDL_AudioDeviceID g_dev;
static uint8_t g_channels;

static void callback(void *user, Uint8 *stream, int len)
{
    hta_audio *a = (hta_audio *)user;
    uint32_t frames = (uint32_t)len / (2u * g_channels);
    hta_audio_mix(a, (int16_t *)stream, frames);
}

bool hta_audio_sdl_start(hta_audio *a, char *err, size_t errlen)
{
    if (!a) return false;
    if (g_dev) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        if (err) snprintf(err, errlen, "SDL audio: %s", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;             /* ~11 ms: short enough for gunfire */
    want.callback = callback;
    want.userdata = a;
    /* Keep the rate the device likes; the mixer resamples every clip. */
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!g_dev) {
        if (err) snprintf(err, errlen, "no audio device: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    g_channels = have.channels;
    hta_audio_init(a, (uint32_t)have.freq, have.channels);
    SDL_PauseAudioDevice(g_dev, 0);
    return true;
}

void hta_audio_sdl_stop(void)
{
    if (!g_dev) return;
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool hta_audio_sdl_running(void) { return g_dev != 0; }
