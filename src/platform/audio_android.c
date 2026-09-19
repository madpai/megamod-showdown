/* AAudio output for the portable mixer.
 *
 * All this file does is own a stream and hand its callback buffer to
 * hta_audio_mix. The mixer itself is in src/engine and knows nothing about
 * Android, so the mixing is tested on the host.
 *
 * The device picks the sample rate and channel count, not us -- the Trial's
 * 22050 Hz mono samples are resampled per voice inside the mixer.
 */
#include "audio_android.h"
#include "platform.h"

#include <aaudio/AAudio.h>
#include <stdatomic.h>
#include <string.h>

static AAudioStream      *g_stream;
static _Atomic bool       g_disconnected;

static aaudio_data_callback_result_t on_audio(AAudioStream *stream, void *user,
                                              void *audio_data, int32_t frames)
{
    (void)stream;
    hta_audio *a = (hta_audio *)user;
    if (!a || frames <= 0) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    /* Real-time thread: mix only. No allocation, no logging, no locks. */
    hta_audio_mix(a, (int16_t *)audio_data, (uint32_t)frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* Fires when the route changes -- headphones in or out, a Bluetooth speaker
 * connecting. The stream is dead after this and has to be rebuilt, which
 * cannot happen on this thread. Flag it; the game loop restarts it. */
static void on_error(AAudioStream *stream, void *user, aaudio_result_t error)
{
    (void)stream; (void)user; (void)error;
    atomic_store(&g_disconnected, true);
}

bool hta_audio_android_start(hta_audio *a)
{
    if (!a || g_stream) return g_stream != NULL;

    AAudioStreamBuilder *b = NULL;
    aaudio_result_t r = AAudio_createStreamBuilder(&b);
    if (r != AAUDIO_OK || !b) {
        hta_log("[audio] cannot create stream builder: %s", AAudio_convertResultToText(r));
        return false;
    }
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    /* Deliberately no setUsage(AAUDIO_USAGE_GAME): it is API 28, and
     * __builtin_available does not gate it in the NDK's C path. It only hints
     * routing and ducking, and the default is fine, so it is not worth
     * raising minSdk over. */
    AAudioStreamBuilder_setDataCallback(b, on_audio, a);
    AAudioStreamBuilder_setErrorCallback(b, on_error, a);

    r = AAudioStreamBuilder_openStream(b, &g_stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !g_stream) {
        hta_log("[audio] openStream failed: %s", AAudio_convertResultToText(r));
        g_stream = NULL;
        return false;
    }

    /* Take the format the device actually gave us, which is often not what
     * was asked for. The mixer is configured before the stream starts, so the
     * first callback cannot arrive before it is ready. */
    int32_t rate = AAudioStream_getSampleRate(g_stream);
    int32_t chans = AAudioStream_getChannelCount(g_stream);
    hta_audio_init(a, (uint32_t)rate, (uint8_t)chans);

    r = AAudioStream_requestStart(g_stream);
    if (r != AAUDIO_OK) {
        hta_log("[audio] requestStart failed: %s", AAudio_convertResultToText(r));
        AAudioStream_close(g_stream);
        g_stream = NULL;
        return false;
    }
    atomic_store(&g_disconnected, false);
    hta_log("[audio] AAudio started: %d Hz, %d channels, %d frames/burst",
            rate, chans, AAudioStream_getFramesPerBurst(g_stream));
    return true;
}

void hta_audio_android_stop(void)
{
    if (!g_stream) return;
    AAudioStream_requestStop(g_stream);
    AAudioStream_close(g_stream);
    g_stream = NULL;
}

bool hta_audio_android_running(void)
{
    return g_stream != NULL && !atomic_load(&g_disconnected);
}

void hta_audio_android_poll(hta_audio *a)
{
    if (!a || !g_stream) return;
    if (!atomic_load(&g_disconnected)) return;
    /* Rebuild on the game thread. Clips are registered again by the caller
     * only if the mixer was re-inited, so preserve the clip table across the
     * restart by copying it back after hta_audio_init wipes it. */
    hta_log("[audio] stream disconnected (route change); restarting");
    hta_audio_clip clips[HTA_AUDIO_MAX_CLIPS];
    uint32_t n = a->clip_count;
    float gain = a->master_gain;
    memcpy(clips, a->clips, sizeof(clips));

    hta_audio_android_stop();
    if (!hta_audio_android_start(a)) {
        atomic_store(&g_disconnected, false);   /* do not spin on it */
        hta_log("[audio] restart failed; continuing without sound");
        return;
    }
    memcpy(a->clips, clips, sizeof(clips));
    a->clip_count = n;
    a->master_gain = gain;
}
