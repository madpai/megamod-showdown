/* A skinned body standing in the world. Needs the Trial's own map: the
 * whole point is that the cyborg poses the way its tags say. */
#include "engine/actor.h"
#include "asset/cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } } while (0)

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)sz);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f); *n = (size_t)sz; return b;
}

/* How tall the posed body is, in world units. */
static float height(const hta_actor *a, float *out_lo)
{
    float lo = 1e9f, hi = -1e9f;
    for (uint32_t v = 0; v < a->mesh.vertex_count; v++) {
        float z = a->posed[v].pos[2];
        if (z < lo) lo = z;
        if (z > hi) hi = z;
    }
    if (out_lo) *out_lo = lo;
    return hi - lo;
}

int main(int argc, char **argv)
{
    printf("actor tests\n");
    hta_actor bad;
    CHECK(!hta_actor_load(&bad, NULL, NULL, 0, NULL, 0), "bad arguments refuse");
    hta_actor_free(&bad);
    hta_actor_free(NULL);
    CHECK(1, "freeing nothing is safe");

    if (argc < 2) {
        printf("\n  skip: pass bloodgulch.map\n");
        printf("\n%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    size_t n = 0;
    uint8_t *data = slurp(argv[1], &n);
    if (!data) { printf("  FAIL: cannot read %s\n", argv[1]); return 1; }
    hta_cache c;
    char err[HTA_ERRLEN] = {0};
    if (!hta_cache_open(&c, data, n, err, sizeof(err))) {
        printf("  FAIL: %s\n", err); return 1;
    }
    char bmp[1024];
    snprintf(bmp, sizeof(bmp), "%s", argv[1]);
    char *slash = strrchr(bmp, '/');
    if (slash) snprintf(slash + 1, sizeof(bmp) - (size_t)(slash + 1 - bmp), "bitmaps.map");
    size_t bsz = 0;
    uint8_t *bdata = slurp(bmp, &bsz);
    hta_resource_map bm;
    int have_bitmaps = bdata && hta_resource_open(&bm, bdata, bsz, err, sizeof(err));

    /* The multiplayer cyborg: the body you leave behind. */
    uint32_t bip = 0;
    for (uint32_t i = 0; i < c.tag_count && !bip; i++) {
        hta_tag_entry t;
        if (!hta_cache_tag(&c, i, &t)) continue;
        if (t.primary_class != HTA_FOURCC('b','i','p','d')) continue;
        char p[256];
        hta_cache_tag_path(&c, &t, p, sizeof(p));
        if (strstr(p, "cyborg_mp")) bip = t.tag_id;
    }
    printf("\n[loading]\n");
    CHECK(bip != 0, "the map has a multiplayer cyborg");

    hta_actor a;
    bool ok = hta_actor_load(&a, &c, have_bitmaps ? &bm : NULL, bip,
                             err, sizeof(err));
    printf("  %s\n", err);
    CHECK(ok, "the body loads");
    if (!ok) { printf("\n%d checks, %d failures\n", checks, failures); return 1; }

    CHECK(a.mesh.vertex_count > 500u, "it has a real amount of geometry");
    CHECK(a.graph.node_count > 10u, "and a skeleton to hang it on");
    CHECK(a.posed != NULL, "and somewhere to put the posed result");
    /* Without a texture table to intern into, the model loads with
     * submeshes and no art at all -- which is how it first came out. */
    printf("  %u submesh(es), %u texture(s)\n",
           a.mesh.submesh_count, a.mesh.texture_count);
    if (have_bitmaps)
        CHECK(a.mesh.texture_count > 0u, "and its textures, not bare submeshes");

    printf("\n[it dies]\n");
    {
        /* Halo has no clip called "die": the cyborg's deaths are named
         * `h-kill` for a hard one and `s-kill` for a soft one, by where the
         * blow landed. */
        uint32_t rng = 7u;
        CHECK(hta_actor_play_death(&a, &rng), "there is a death animation");
        printf("  playing '%s', %u frames\n",
               a.graph.anims[a.clip].name, a.graph.anims[a.clip].frame_count);
        CHECK(a.hold_last, "  which holds its last frame rather than looping");
        CHECK(!a.finished, "  and has not finished before it has run");

        float feet[3] = { 100.0f, -150.0f, 1.0f };
        hta_actor_place(&a, feet, 0.7f);
        float lo = 0.0f;
        float standing = height(&a, &lo);
        printf("  upright: %.2f wu (%.2f m), feet at z=%.2f\n",
               (double)standing, (double)(standing * 3.048f), (double)lo);
        /* A Spartan is about 2.1 m. If this comes out near zero the
         * skinning has collapsed; if it comes out huge the bind pose is
         * being applied twice. */
        CHECK(standing > 0.55f && standing < 0.80f,
              "  it starts out the height of a Spartan");
        /* Placed at its FEET, not through the floor or floating. */
        CHECK(fabsf(lo - feet[2]) < 0.15f, "  and stands on the spot given");

        for (int k = 0; k < 120; k++) hta_actor_update(&a, 1.0f / 60.0f);
        hta_actor_place(&a, feet, 0.7f);
        float down = height(&a, &lo);
        printf("  down:    %.2f wu (%.2f m)\n",
               (double)down, (double)(down * 3.048f));
        CHECK(a.finished, "  the clip finishes");
        CHECK(down < standing * 0.6f, "  and the body ends up on the floor");

        /* Held, not looped: running on must not stand it back up. */
        float held = a.frame;
        for (int k = 0; k < 600; k++) hta_actor_update(&a, 1.0f / 60.0f);
        CHECK(a.frame == held, "  and stays down however long you wait");
    }

    printf("\n[where it is put]\n");
    {
        /* The body goes where it died, facing where the player was looking.
         * A skinning bug that dropped the root transform would leave it at
         * the origin of the map, which is what this catches. */
        const float spots[3][3] = {
            { 100.0f, -150.0f, 1.0f },
            {  20.0f,  -60.0f, 3.5f },
            {   0.0f,    0.0f, 0.0f }
        };
        for (int q = 0; q < 3; q++) {
            hta_actor_place(&a, spots[q], (float)q * 1.3f);
            float cx = 0.0f, cy = 0.0f;
            for (uint32_t v = 0; v < a.mesh.vertex_count; v++) {
                cx += a.posed[v].pos[0];
                cy += a.posed[v].pos[1];
            }
            cx /= (float)a.mesh.vertex_count;
            cy /= (float)a.mesh.vertex_count;
            printf("  asked for %.1f %.1f, body centres on %.2f %.2f\n",
                   (double)spots[q][0], (double)spots[q][1], (double)cx, (double)cy);
            CHECK(fabsf(cx - spots[q][0]) < 0.6f &&
                  fabsf(cy - spots[q][1]) < 0.6f,
                  "  the body is where it was put");
        }

        /* Facing actually turns it. */
        hta_actor_place(&a, spots[0], 0.0f);
        float ax = a.posed[0].pos[0], ay = a.posed[0].pos[1];
        hta_actor_place(&a, spots[0], 3.14159f);
        float bx = a.posed[0].pos[0], by = a.posed[0].pos[1];
        float moved = sqrtf((ax-bx)*(ax-bx) + (ay-by)*(ay-by));
        printf("  turning it around moves a vertex %.3f wu\n", (double)moved);
        CHECK(moved > 0.01f, "  facing rotates the body");
    }

    /* The report: "players and bots flip upside down when getting hit".
     * Every overlay a body plays -- the flinches and the recoils -- over
     * every stance a bot stands, runs or crouches in, must leave it on its
     * feet, and at an overlay's own first frame must change nothing. */
    printf("\n[overlays over every stance]\n");
    {
        static const char *const BASES[] = {
            "stand rifle idle", "stand rifle move-front", "stand rifle move-left",
            "stand pistol idle", "stand pistol move-front", "crouch rifle idle",
            "crouch pistol move-front", "stand missile idle", "stand rifle airborne",
        };
        static const char *const OVERLAYS[] = {
            "s-ping front gut%0", "s-ping front gut%1", "s-ping front gut%2",
            "stand rifle ar fire-1", "stand pistol hp fire-1", "stand missile rl fire-1",
            "stand rifle sg fire-1",
        };
        const float at[3] = { 0.0f, 0.0f, 0.0f };
        int upright = 0, still = 0, total = 0;
        float worst = 0.0f, worst_start = 0.0f;
        for (size_t b = 0; b < sizeof(BASES) / sizeof(BASES[0]); b++)
        for (size_t o = 0; o < sizeof(OVERLAYS) / sizeof(OVERLAYS[0]); o++) {
            if (!hta_actor_play(&a, BASES[b], false)) continue;
            a.overlay = -1;
            hta_actor_place(&a, at, 0.0f);
            float zmin = 1e9f, zmax = -1e9f;
            for (uint32_t v = 0; v < a.mesh.vertex_count; v++) {
                zmin = fminf(zmin, a.posed[v].pos[2]); zmax = fmaxf(zmax, a.posed[v].pos[2]);
            }
            static hta_vertex before[8192];
            uint32_t nv = a.mesh.vertex_count < 8192 ? a.mesh.vertex_count : 8192;
            memcpy(before, a.posed, nv * sizeof(hta_vertex));
            if (!hta_actor_play_overlay(&a, OVERLAYS[o])) continue;
            total++;
            /* Frame 0: the pose underneath, untouched. */
            hta_actor_place(&a, at, 0.0f);
            float drift = 0.0f;
            for (uint32_t v = 0; v < nv; v++)
                for (int k = 0; k < 3; k++)
                    drift = fmaxf(drift, fabsf(a.posed[v].pos[k] - before[v].pos[k]));
            if (drift < 0.02f) still++;
            if (drift > worst_start) worst_start = drift;
            /* Mid-clip: the head end still above the feet. */
            const hta_animation *an = &a.graph.anims[a.overlay];
            a.overlay_frame = (float)(an->frame_count > 1 ? an->frame_count - 1 : 0) * 0.5f;
            hta_actor_place(&a, at, 0.0f);
            /* The highest point must stay near where the head was: upside
             * down puts the feet up there instead. */
            float top = -1e9f, bottom = 1e9f;
            for (uint32_t v = 0; v < a.mesh.vertex_count; v++) {
                top = fmaxf(top, a.posed[v].pos[2]); bottom = fminf(bottom, a.posed[v].pos[2]);
            }
            float off = fmaxf(fabsf(top - zmax), fabsf(bottom - zmin));
            if (off > worst) worst = off;
            if (off < 0.15f) upright++;
            a.overlay = -1;
        }
        printf("  %d stance/overlay pairs; worst height change %.3f wu, worst frame-0 drift %.4f wu\n",
               total, (double)worst, (double)worst_start);
        CHECK(total >= 40, "  the cyborg has these stances and overlays");
        CHECK(upright == total, "  every one stays on its feet mid-overlay");
        CHECK(still == total, "  and changes nothing at its first frame");
    }

    printf("\n[clips it does not have]\n");
    CHECK(!hta_actor_play(&a, "no such animation at all", false),
          "an unknown clip is refused");
    CHECK(hta_actor_play(&a, "s-kill", true), "a substring finds one");

    hta_actor_free(&a);
    CHECK(a.mesh.vertex_count == 0, "free clears it");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
