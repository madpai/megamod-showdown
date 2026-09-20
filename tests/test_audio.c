/* The portable voice mixer: resampling, mixing, stealing, ring behaviour.
 * Needs no device and no map. */
#include "engine/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

/* A clip that counts up, so a resampling error shows as a wrong value rather
 * than as something merely plausible. */
static int16_t ramp[100];
/* A constant tone for gain and mixing checks. */
static int16_t flat[64];

int main(void)
{
    printf("audio mixer\n");
    for (int i = 0; i < 100; i++) ramp[i] = (int16_t)(i * 100);
    for (int i = 0; i < 64; i++) flat[i] = 1000;

    printf("\n[clips and voices]\n");
    {
        hta_audio a;
        hta_audio_init(&a, 22050, 2);
        CHECK(a.out_rate == 22050 && a.out_channels == 2, "init takes the device format");

        uint32_t c = hta_audio_add_clip(&a, ramp, 100, 22050, 1);
        CHECK(c == 0, "first clip is index 0");
        CHECK(hta_audio_add_clip(&a, NULL, 100, 22050, 1) == HTA_AUDIO_NO_CLIP,
              "a null clip is refused");
        CHECK(hta_audio_add_clip(&a, ramp, 100, 22050, 7) == HTA_AUDIO_NO_CLIP,
              "7 channels is refused");

        int16_t out[64 * 2];
        /* Nothing playing must produce silence, not stale buffer contents. */
        memset(out, 0x7F, sizeof(out));
        hta_audio_mix(&a, out, 64);
        int silent = 1;
        for (int i = 0; i < 64 * 2; i++) if (out[i]) silent = 0;
        CHECK(silent, "an idle mixer clears the buffer");
        CHECK(hta_audio_active_voices(&a) == 0, "and starts no voices");

        /* At matching rates the clip must come out sample for sample, in
         * both output channels, because the source is mono. */
        hta_audio_play(&a, c, 1.0f);
        hta_audio_mix(&a, out, 64);
        CHECK(hta_audio_active_voices(&a) == 1, "playing one clip uses one voice");
        int exact = 1;
        for (int f = 0; f < 64; f++)
            if (out[f*2] != ramp[f] || out[f*2+1] != ramp[f]) exact = 0;
        CHECK(exact, "at 1:1 rate the samples pass through untouched");
        CHECK(a.started == 1, "one voice start was counted");

        /* It must finish and release the voice, not loop or hang. */
        hta_audio_mix(&a, out, 64);
        CHECK(hta_audio_active_voices(&a) == 0, "the voice frees itself at the end");
    }

    printf("\n[resampling]\n");
    {
        /* 22050 clip into a 44100 output: each source frame lasts two output
         * frames, and the interpolated midpoint sits halfway between. */
        hta_audio a;
        hta_audio_init(&a, 44100, 1);
        uint32_t c = hta_audio_add_clip(&a, ramp, 100, 22050, 1);
        int16_t out[16];
        hta_audio_play(&a, c, 1.0f);
        hta_audio_mix(&a, out, 16);
        CHECK(out[0] == 0, "starts at the first sample");
        CHECK(out[2] == 100, "one source frame later after two output frames");
        CHECK(out[4] == 200, "and keeps that 2:1 pace");
        CHECK(out[1] == 50, "the frame between is interpolated, not repeated");
        CHECK(out[3] == 150, "and so is the next");

        /* Downsampling must advance faster, not read out of bounds. */
        hta_audio b;
        hta_audio_init(&b, 11025, 1);
        uint32_t cb = hta_audio_add_clip(&b, ramp, 100, 22050, 1);
        int16_t o2[8];
        hta_audio_play(&b, cb, 1.0f);
        hta_audio_mix(&b, o2, 8);
        CHECK(o2[0] == 0 && o2[1] == 200 && o2[2] == 400,
              "a 2:1 downsample steps two source frames per output frame");
    }

    printf("\n[gain and summing]\n");
    {
        hta_audio a;
        hta_audio_init(&a, 22050, 1);
        uint32_t c = hta_audio_add_clip(&a, flat, 64, 22050, 1);
        int16_t out[16];

        hta_audio_play(&a, c, 0.5f);
        hta_audio_mix(&a, out, 16);
        CHECK(out[0] == 500, "gain scales the sample");

        /* Two voices of the same clip must sum. */
        hta_audio_init(&a, 22050, 1);
        c = hta_audio_add_clip(&a, flat, 64, 22050, 1);
        hta_audio_play(&a, c, 1.0f);
        hta_audio_play(&a, c, 1.0f);
        hta_audio_mix(&a, out, 16);
        CHECK(hta_audio_active_voices(&a) == 2, "two requests, two voices");
        CHECK(out[0] == 2000, "and their samples add");

        /* Summing past full scale must clamp, not wrap to a loud crack. */
        hta_audio_init(&a, 22050, 1);
        static int16_t loud[16];
        for (int i = 0; i < 16; i++) loud[i] = 30000;
        c = hta_audio_add_clip(&a, loud, 16, 22050, 1);
        for (int i = 0; i < 4; i++) hta_audio_play(&a, c, 1.0f);
        hta_audio_mix(&a, out, 16);
        CHECK(out[0] == 32767, "four loud voices clamp at the ceiling");
    }

    printf("\n[pressure]\n");
    {
        hta_audio a;
        hta_audio_init(&a, 22050, 1);
        uint32_t c = hta_audio_add_clip(&a, flat, 64, 22050, 1);
        int16_t out[8];

        /* More simultaneous sounds than voices: the mixer steals rather than
         * overruns, and every request is still accounted for. */
        for (int i = 0; i < HTA_AUDIO_MAX_VOICES + 5; i++)
            hta_audio_play(&a, c, 1.0f);
        hta_audio_mix(&a, out, 8);
        CHECK(hta_audio_active_voices(&a) <= HTA_AUDIO_MAX_VOICES,
              "never more voices than the mixer has");
        CHECK(a.started == (uint32_t)(HTA_AUDIO_MAX_VOICES + 5),
              "every queued request started a voice");
        CHECK(a.stolen == 5, "the excess stole voices, exactly as many as needed");

        /* Overfilling the request ring drops requests instead of corrupting
         * it -- a dropped gunshot beats a torn one. */
        hta_audio_init(&a, 22050, 1);
        c = hta_audio_add_clip(&a, flat, 64, 22050, 1);
        for (int i = 0; i < HTA_AUDIO_REQ_RING + 10; i++)
            hta_audio_play(&a, c, 1.0f);
        CHECK(atomic_load(&a.dropped) == 10u, "a full ring drops the overflow");
        hta_audio_mix(&a, out, 8);
        CHECK(a.started == HTA_AUDIO_REQ_RING,
              "and the ring's worth still played");

        /* An out-of-range clip index must be ignored, not indexed. */
        hta_audio_init(&a, 22050, 1);
        hta_audio_add_clip(&a, flat, 64, 22050, 1);
        hta_audio_play(&a, 99u, 1.0f);
        hta_audio_mix(&a, out, 8);
        CHECK(hta_audio_active_voices(&a) == 0, "an unknown clip id plays nothing");
    }

    printf("\n[continuous sounds]\n");
    {
        /* The flamethrower's roar is a looping sound on the weapon object,
         * held for as long as the trigger is. */
        hta_audio a;
        hta_audio_init(&a, 22050, 1);
        int16_t tone[8];
        for (int i = 0; i < 8; i++) tone[i] = (int16_t)(i < 4 ? 8000 : -8000);
        uint32_t clip = hta_audio_add_clip(&a, tone, 8, 22050, 1);

        int16_t out[64];
        hta_audio_loop(&a, 1u, clip, 1.0f);
        hta_audio_mix(&a, out, 64);
        CHECK(hta_audio_active_voices(&a) == 1, "a loop starts one voice");
        int silent = 1;
        for (int i = 0; i < 64; i++) if (out[i] != 0) silent = 0;
        CHECK(!silent, "and sounds past the end of an 8-frame clip");

        /* Asking again while it runs must not restart or stack it. */
        for (int i = 0; i < 4; i++) {
            hta_audio_loop(&a, 1u, clip, 1.0f);
            hta_audio_mix(&a, out, 64);
        }
        CHECK(hta_audio_active_voices(&a) == 1, "asking again does not stack voices");

        hta_audio_loop_stop(&a, 1u);
        hta_audio_mix(&a, out, 64);
        CHECK(hta_audio_active_voices(&a) == 0, "releasing the trigger stops it");
        silent = 1;
        for (int i = 0; i < 64; i++) if (out[i] != 0) silent = 0;
        CHECK(silent, "and it goes quiet");

        hta_audio_loop_stop(&a, 1u);
        hta_audio_mix(&a, out, 64);
        CHECK(hta_audio_active_voices(&a) == 0, "stopping it twice is harmless");

        /* A loop must survive a storm of one-shots rather than being stolen. */
        hta_audio_loop(&a, 7u, clip, 1.0f);
        hta_audio_mix(&a, out, 64);
        for (int i = 0; i < HTA_AUDIO_MAX_VOICES * 3; i++) {
            hta_audio_play(&a, clip, 1.0f);
            hta_audio_mix(&a, out, 4);
        }
        int loop_alive = 0;
        for (uint32_t i = 0; i < HTA_AUDIO_MAX_VOICES; i++)
            if (a.voices[i].active && a.voices[i].loop == 7u) loop_alive = 1;
        CHECK(loop_alive, "one-shots never steal the continuous voice");
        hta_audio_loop_stop(&a, 7u);
        hta_audio_mix(&a, out, 8);
        CHECK(hta_audio_active_voices(&a) == 0, "and it still stops on demand");
    }

    printf("\n[placing a sound in the stereo field]\n");
    {
        /* A world sound is panned toward where it actually is. The curve is
         * constant power, so a shot sweeping past does not dip as it
         * crosses the middle. */
        hta_audio a;
        hta_audio_init(&a, 22050, 2);
        int16_t tone[16];
        for (int i = 0; i < 16; i++) tone[i] = 12000;
        uint32_t clip = hta_audio_add_clip(&a, tone, 16, 22050, 1);
        int16_t out[32];

        double energy[3];
        const float pans[3] = { -1.0f, 0.0f, 1.0f };
        for (int p = 0; p < 3; p++) {
            for (uint32_t v = 0; v < HTA_AUDIO_MAX_VOICES; v++)
                a.voices[v].active = false;
            hta_audio_play_pan(&a, clip, 1.0f, pans[p]);
            memset(out, 0, sizeof(out));
            hta_audio_mix(&a, out, 8);
            double l = 0, r = 0;
            for (int i = 0; i < 8; i++) {
                l += (double)out[i * 2] * out[i * 2];
                r += (double)out[i * 2 + 1] * out[i * 2 + 1];
            }
            energy[p] = l + r;
            if (p == 0) {
                CHECK(l > r * 4.0, "hard left is mostly in the left channel");
                CHECK(r < l, "  and quieter on the right");
            } else if (p == 1) {
                double d = l > r ? l - r : r - l;
                CHECK(d < (l + r) * 0.05, "centre is even across both");
            } else {
                CHECK(r > l * 4.0, "hard right is mostly in the right channel");
            }
        }
        /* Constant power: the total must not sag in the middle. */
        double lo = energy[0] < energy[2] ? energy[0] : energy[2];
        printf("  energy: left %.0f centre %.0f right %.0f\n",
               energy[0], energy[1], energy[2]);
        CHECK(energy[1] > lo * 0.8 && energy[1] < lo * 1.25,
              "and a centred sound is as loud as one hard over");

        /* Panning must not disturb the plain play path. */
        for (uint32_t v = 0; v < HTA_AUDIO_MAX_VOICES; v++)
            a.voices[v].active = false;
        hta_audio_play(&a, clip, 1.0f);
        memset(out, 0, sizeof(out));
        hta_audio_mix(&a, out, 8);
        double l2 = 0, r2 = 0;
        for (int i = 0; i < 8; i++) { l2 += abs(out[i*2]); r2 += abs(out[i*2+1]); }
        CHECK(l2 > 0 && r2 > 0, "an unplaced sound still reaches both ears");
        double d2 = l2 > r2 ? l2 - r2 : r2 - l2;
        CHECK(d2 < (l2 + r2) * 0.05, "evenly");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
