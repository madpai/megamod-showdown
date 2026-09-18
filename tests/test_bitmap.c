/* Bitmap pixel-format tests. No Halo data required. */
#include "asset/bitmap.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

int main(void)
{
    char err[HTA_ERRLEN];
    printf("bitmap decode tests\n\n[r5g6b5]\n");

    /* 2x1: red (0xF800) then white (0xFFFF) */
    uint8_t r565[4] = { 0x00, 0xF8, 0xFF, 0xFF };
    hta_bitmap b;
    CHECK(hta_bitmap_decode_pixels(HTA_FMT_R5G6B5, 2, 1, r565, 4, &b, err, sizeof(err)),
          "decodes r5g6b5");
    CHECK(b.width == 2 && b.height == 1 && b.rgba, "size 2x1");
    if (b.rgba) {
        CHECK(b.rgba[0] > 240 && b.rgba[1] < 20 && b.rgba[2] < 20 && b.rgba[3] == 255,
              "pixel 0 is red");
        CHECK(b.rgba[4] > 240 && b.rgba[5] > 240 && b.rgba[6] > 240,
              "pixel 1 is white");
    }
    hta_bitmap_free(&b);
    CHECK(b.rgba == NULL, "free clears");

    printf("\n[dxt1 solid red]\n");
    /* one 4x4 DXT1 block, both endpoints red 565, all pixels index 0 */
    uint8_t dxt[8] = { 0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0 };
    CHECK(hta_bitmap_decode_pixels(HTA_FMT_DXT1, 4, 4, dxt, 8, &b, err, sizeof(err)),
          "decodes dxt1");
    CHECK(b.width == 4 && b.height == 4, "size 4x4");
    if (b.rgba) {
        int reds = 0;
        for (int i = 0; i < 16; i++)
            if (b.rgba[i*4] > 240 && b.rgba[i*4+1] < 20 && b.rgba[i*4+2] < 20) reds++;
        CHECK(reds == 16, "all 16 pixels red");
    }
    hta_bitmap_free(&b);

    printf("\n[a8r8g8b8]\n");
    uint8_t bgra[4] = { 10, 20, 30, 40 }; /* B,G,R,A */
    CHECK(hta_bitmap_decode_pixels(HTA_FMT_A8R8G8B8, 1, 1, bgra, 4, &b, err, sizeof(err)),
          "decodes a8r8g8b8");
    CHECK(b.rgba && b.rgba[0] == 30 && b.rgba[1] == 20 && b.rgba[2] == 10 && b.rgba[3] == 40,
          "swizzles BGRA to RGBA");
    hta_bitmap_free(&b);

    printf("\n[reject]\n");
    CHECK(!hta_bitmap_decode_pixels(99, 4, 4, dxt, 8, &b, err, sizeof(err)),
          "unknown format rejected");
    CHECK(!hta_bitmap_decode_pixels(HTA_FMT_DXT1, 0, 4, dxt, 8, &b, err, sizeof(err)),
          "zero size rejected");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
