#include "sound.h"
#include "bsp.h"     /* hta_read_reflexive */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sound (164) */
#define SND_SOUND_CLASS      4u
#define SND_SAMPLE_RATE      6u
#define SND_CHANNEL_COUNT  108u
#define SND_FORMAT         110u
#define SND_PITCH_RANGES   152u

/* SoundPitchRange (72) */
#define SPR_SIZE            72u
#define SPR_PERMUTATIONS    60u

/* SoundPermutation (124) */
#define SPERM_SIZE         124u
#define SPERM_FORMAT        40u
#define SPERM_SAMPLES       64u   /* TagDataOffset: size@0, external@4, offset@8 */

static void fail(char *err, size_t n, const char *fmt, ...)
{
    if (!err || !n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

void hta_pcm_free(hta_pcm *p)
{
    if (!p) return;
    free(p->samples);
    memset(p, 0, sizeof(*p));
}

/* Standard IMA/DVI tables. Xbox ADPCM is the IMA variant with a fixed
 * 36-byte block and a 4-byte per-channel header. */
static const int16_t step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
    41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173,
    190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};
#define STEP_MAX 88

typedef struct { int32_t sample; int32_t index; } adpcm_state;

static void step_sample(adpcm_state *st, uint8_t code)
{
    int32_t step = step_table[st->index];
    int32_t delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    if (code & 8) delta = -delta;

    int32_t r = st->sample + delta;
    if (r > 32767) r = 32767;
    else if (r < -32768) r = -32768;
    st->sample = r;

    st->index += index_table[code];
    if (st->index < 0) st->index = 0;
    else if (st->index > STEP_MAX) st->index = STEP_MAX;
}

bool hta_xbox_adpcm_decode(const uint8_t *src, uint32_t src_len, uint8_t channels,
                           int16_t *dst, uint32_t dst_frames)
{
    if (!src || !dst || channels < 1 || channels > 2) return false;
    uint32_t block_bytes = HTA_XBOX_ADPCM_BLOCK * channels;
    if (src_len % block_bytes) return false;
    uint32_t blocks = src_len / block_bytes;
    if ((uint64_t)blocks * HTA_XBOX_ADPCM_FRAMES > dst_frames) return false;

    adpcm_state st[2];
    for (uint32_t b = 0; b < blocks; b++) {
        const uint8_t *p = src + (size_t)b * block_bytes;
        int16_t *out = dst + (size_t)b * HTA_XBOX_ADPCM_FRAMES * channels;

        /* Per-channel 4-byte header: seed sample (int16 LE), step index, pad.
         * The seed is itself the block's first output frame. */
        for (uint32_t ch = 0; ch < channels; ch++) {
            int16_t seed = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
            int32_t idx = p[2];
            if (idx < 0) idx = 0;
            if (idx > STEP_MAX) idx = STEP_MAX;
            st[ch].sample = seed;
            st[ch].index = idx;
            out[ch] = seed;
            p += 4;
        }

        /* Then 8 chunks of 4 bytes per channel: 8 codes each, low nibble
         * first. One block yields 64 frames, the seed included, so the very
         * last code of the last chunk is unused -- mirror that exactly or
         * everything after the first block drifts. */
        uint32_t frame = 1;
        while (frame < HTA_XBOX_ADPCM_FRAMES) {
            uint32_t n = HTA_XBOX_ADPCM_FRAMES - frame;
            if (n > 8) n = 8;
            for (uint32_t ch = 0; ch < channels; ch++) {
                uint32_t codes = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                               | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                p += 4;
                for (uint32_t i = 0; i < n; i++) {
                    step_sample(&st[ch], (uint8_t)(codes & 0xFu));
                    codes >>= 4;
                    out[(frame + i) * channels + ch] = (int16_t)st[ch].sample;
                }
            }
            frame += n;
        }
    }
    return true;
}

/* Offset of pitch range 0's permutation array, plus its count. */
static bool pitch_range_0(const hta_cache *c, uint32_t tag_id,
                          uint32_t *out_perm_off, uint32_t *out_count)
{
    int32_t ti = hta_cache_find_tag_by_id(c, tag_id);
    if (ti < 0) return false;
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t) || t.indexed) return false;
    uint32_t base;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) return false;

    uint32_t pr_count = 0, pr_ptr = 0, pr_off = 0;
    if (!hta_read_reflexive(c, base + SND_PITCH_RANGES, &pr_count, &pr_ptr)) return false;
    if (!pr_count || !hta_cache_ptr_to_offset(c, pr_ptr, &pr_off)) return false;

    uint32_t n = 0, ptr = 0, off = 0;
    if (!hta_read_reflexive(c, pr_off + SPR_PERMUTATIONS, &n, &ptr)) return false;
    if (!n || !hta_cache_ptr_to_offset(c, ptr, &off)) return false;
    *out_perm_off = off;
    *out_count = n;
    return true;
}

bool hta_sound_info_load(const hta_cache *c, uint32_t tag_id,
                         hta_sound_info *out, char *err, size_t errlen)
{
    if (!c || !out) { fail(err, errlen, "bad arguments"); return false; }
    memset(out, 0, sizeof(*out));
    int32_t ti = hta_cache_find_tag_by_id(c, tag_id);
    if (ti < 0) { fail(err, errlen, "snd! 0x%08X not found", tag_id); return false; }
    hta_tag_entry t;
    if (!hta_cache_tag(c, (uint32_t)ti, &t)) { fail(err, errlen, "tag unreadable"); return false; }
    if (t.indexed) { fail(err, errlen, "snd! 0x%08X is indexed", tag_id); return false; }
    uint32_t base;
    if (!hta_cache_ptr_to_offset(c, t.tag_data_ptr, &base)) {
        fail(err, errlen, "snd! body out of range"); return false;
    }
    hta_rd_u16(c, base + SND_SOUND_CLASS, &out->sound_class);
    hta_rd_u16(c, base + SND_SAMPLE_RATE, &out->sample_rate);
    hta_rd_u16(c, base + SND_CHANNEL_COUNT, &out->channels);
    hta_rd_u16(c, base + SND_FORMAT, &out->format);

    uint32_t poff = 0, n = 0;
    if (pitch_range_0(c, tag_id, &poff, &n)) out->permutations = n;
    return true;
}

bool hta_sound_decode(const hta_cache *c, const hta_resource_map *sounds,
                      uint32_t tag_id, uint32_t permutation,
                      hta_pcm *out, char *err, size_t errlen)
{
    if (!c || !out) { fail(err, errlen, "bad arguments"); return false; }
    memset(out, 0, sizeof(*out));

    hta_sound_info info;
    if (!hta_sound_info_load(c, tag_id, &info, err, errlen)) return false;

    uint32_t perm_off = 0, perm_count = 0;
    if (!pitch_range_0(c, tag_id, &perm_off, &perm_count)) {
        fail(err, errlen, "snd! 0x%08X has no permutations", tag_id);
        return false;
    }
    if (permutation >= perm_count) {
        fail(err, errlen, "permutation %u >= %u", permutation, perm_count);
        return false;
    }

    uint32_t pk = perm_off + permutation * SPERM_SIZE;
    uint16_t pfmt = 0;
    uint32_t ssize = 0, sext = 0, sfo = 0;
    if (!hta_rd_u16(c, pk + SPERM_FORMAT, &pfmt) ||
        !hta_rd_u32(c, pk + SPERM_SAMPLES + 0, &ssize) ||
        !hta_rd_u32(c, pk + SPERM_SAMPLES + 4, &sext) ||
        !hta_rd_u32(c, pk + SPERM_SAMPLES + 8, &sfo)) {
        fail(err, errlen, "permutation unreadable");
        return false;
    }
    if (ssize == 0) { fail(err, errlen, "permutation has no samples"); return false; }

    const uint8_t *src = NULL;
    if (sext & 1u) {
        if (!sounds || !sounds->data) {
            fail(err, errlen, "samples are in sounds.map, which was not supplied");
            return false;
        }
        if ((uint64_t)sfo + ssize > (uint64_t)sounds->size) {
            fail(err, errlen, "samples [0x%X +%u] outside sounds.map (%zu)",
                 sfo, ssize, sounds->size);
            return false;
        }
        src = sounds->data + sfo;
    } else {
        if ((uint64_t)sfo + ssize > (uint64_t)c->size) {
            fail(err, errlen, "samples [0x%X +%u] outside the cache", sfo, ssize);
            return false;
        }
        src = c->data + sfo;
    }

    uint8_t channels = (info.channels == 1) ? 2 : 1;
    uint32_t rate = (info.sample_rate == 1) ? 44100u : 22050u;

    if (pfmt == HTA_SND_FMT_XBOX) {
        uint32_t block_bytes = HTA_XBOX_ADPCM_BLOCK * channels;
        if (ssize % block_bytes) {
            fail(err, errlen, "%u bytes is not whole Xbox ADPCM blocks of %u",
                 ssize, block_bytes);
            return false;
        }
        uint32_t frames = (ssize / block_bytes) * HTA_XBOX_ADPCM_FRAMES;
        int16_t *pcm = (int16_t *)malloc((size_t)frames * channels * sizeof(int16_t));
        if (!pcm) { fail(err, errlen, "oom for %u frames", frames); return false; }
        if (!hta_xbox_adpcm_decode(src, ssize, channels, pcm, frames)) {
            free(pcm);
            fail(err, errlen, "Xbox ADPCM decode failed");
            return false;
        }
        out->samples = pcm;
        out->frame_count = frames;
        out->channels = channels;
        out->sample_rate = rate;
        return true;
    }

    if (pfmt == HTA_SND_FMT_PCM16) {
        uint32_t frames = ssize / (2u * channels);
        int16_t *pcm = (int16_t *)malloc((size_t)frames * channels * sizeof(int16_t));
        if (!pcm) { fail(err, errlen, "oom for %u frames", frames); return false; }
        /* Cache PCM is big-endian on Gearbox maps; swap on the way out. */
        for (uint32_t i = 0; i < frames * channels; i++)
            pcm[i] = (int16_t)(((uint16_t)src[i*2] << 8) | (uint16_t)src[i*2+1]);
        out->samples = pcm;
        out->frame_count = frames;
        out->channels = channels;
        out->sample_rate = rate;
        return true;
    }

    fail(err, errlen, "permutation format %u (%s) is not decoded yet", pfmt,
         pfmt == HTA_SND_FMT_OGG ? "Ogg Vorbis" :
         pfmt == HTA_SND_FMT_IMA ? "IMA ADPCM" : "?");
    return false;
}
