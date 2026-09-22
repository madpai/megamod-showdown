#ifndef HTA_OGG_H
#define HTA_OGG_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
/* Decode a whole Ogg Vorbis stream to interleaved 16-bit PCM (malloc'd;
 * free() it). False if it is not one. */
bool hta_ogg_decode(const uint8_t *data, uint32_t size, int16_t **out_pcm,
                    uint32_t *out_frames, uint8_t *out_channels, uint32_t *out_rate);
#endif
