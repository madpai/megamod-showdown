/* Ogg Vorbis, for the sounds the Trial keeps that way: the title music and
 * a handful of others. The decoder is Sean Barrett's stb_vorbis (public
 * domain / MIT, vendored unmodified in src/third_party). */
#include "ogg.h"

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../third_party/stb_vorbis.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

bool hta_ogg_decode(const uint8_t *data, uint32_t size, int16_t **out_pcm,
                    uint32_t *out_frames, uint8_t *out_channels, uint32_t *out_rate)
{
    if (!data || !size || !out_pcm) return false;
    int channels = 0, rate = 0;
    short *pcm = NULL;
    int frames = stb_vorbis_decode_memory(data, (int)size, &channels, &rate, &pcm);
    if (frames <= 0 || !pcm || channels < 1 || channels > 2) { free(pcm); return false; }
    *out_pcm = pcm;
    *out_frames = (uint32_t)frames;
    *out_channels = (uint8_t)channels;
    *out_rate = (uint32_t)rate;
    return true;
}
