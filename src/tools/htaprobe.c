/* htaprobe — measure the pawn's fit at a point on the map.
 *
 * A screenshot with coordinates tells you *where* the reporter got stuck; this
 * tells you *why*. It builds the same collision world the device builds
 * (structure BSP + scenery/vehicle coll tags, slope from the biped tag) and
 * reports, at a point or over a patch:
 *
 *   - ground height, and the headroom above it (first ceiling hit going up)
 *   - whether a standing pawn is pushed, and whether a crouching one is
 *   - every triangle that pushes, with its normal and z range, so a roof
 *     mistaken for a wall is visible as such
 *
 *   htaprobe bloodgulch.map --at 96.57 -155.72
 *   htaprobe bloodgulch.map --at 96.57 -155.72 --span 8 --step 0.25
 *
 * You must supply your own legally obtained Trial data.
 */
#include "asset/cache.h"
#include "asset/bsp.h"
#include "asset/biped.h"
#include "asset/model.h"
#include "engine/player.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)n;
    return buf;
}

/* Distance from the queried point to the closest ceiling above it. */
static float headroom(const hta_collision *col, float x, float y, float z, float limit)
{
    float orig[3] = { x, y, z };
    float dir[3]  = { 0.0f, 0.0f, 1.0f };
    float t = 0.0f, hit[3], nrm[3];
    if (!hta_collision_ray(col, orig, dir, limit, &t, hit, nrm)) return limit;
    return t;
}

/* How far the depenetration pass moves a pawn of this collision height. */
static float push_for(const hta_collision *col, float x, float y, float z_feet,
                      float height, float radius)
{
    float px = x, py = y;
    hta_collision_depenetrate(col, &px, &py, z_feet, height, radius);
    return sqrtf((px - x) * (px - x) + (py - y) * (py - y));
}

/* Replays depenetrate's triangle filter and prints what would push, and why.
 * Kept structurally parallel to hta_collision_depenetrate: if that filter
 * changes, this has to change with it or the report starts lying. */
static void explain(const hta_collision *col, float x, float y, float z_feet,
                    float height, float radius)
{
    float walk = (col->walkable_nz > 0.1f) ? col->walkable_nz : 0.50f;
    float z0 = z_feet, z1 = z_feet + height;
    int cx0 = (int)((x - radius - col->min[0]) / col->cell);
    int cx1 = (int)((x + radius - col->min[0]) / col->cell);
    int cy0 = (int)((y - radius - col->min[1]) / col->cell);
    int cy1 = (int)((y + radius - col->min[1]) / col->cell);
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 >= (int)col->nx) cx1 = (int)col->nx - 1;
    if (cy1 >= (int)col->ny) cy1 = (int)col->ny - 1;

    uint32_t seen = 0;
    printf("  triangles within reach of the body column [%.2f .. %.2f]:\n", z0, z1);
    for (int cy = cy0; cy <= cy1; cy++)
    for (int cx = cx0; cx <= cx1; cx++) {
        uint32_t ci = (uint32_t)cy * col->nx + (uint32_t)cx;
        for (uint32_t k = col->cell_start[ci]; k < col->cell_start[ci + 1u]; k++) {
            uint32_t t = col->tri_index[k];
            const float *a = col->verts[col->indices[t*3+0]].pos;
            const float *b = col->verts[col->indices[t*3+1]].pos;
            const float *d = col->verts[col->indices[t*3+2]].pos;
            float e1x=b[0]-a[0], e1y=b[1]-a[1], e1z=b[2]-a[2];
            float e2x=d[0]-a[0], e2y=d[1]-a[1], e2z=d[2]-a[2];
            float nx=e1y*e2z-e1z*e2y, ny=e1z*e2x-e1x*e2z, nz=e1x*e2y-e1y*e2x;
            float nlen = sqrtf(nx*nx+ny*ny+nz*nz);
            if (nlen < 1e-8f) continue;
            float unz = nz / nlen;

            float zmin = a[2], zmax = a[2];
            if (b[2] < zmin) zmin = b[2];
            if (d[2] < zmin) zmin = d[2];
            if (b[2] > zmax) zmax = b[2];
            if (d[2] > zmax) zmax = d[2];
            /* Only report what is anywhere near the column. */
            if (zmax < z0 - 0.05f || zmin > z1 + 0.05f) continue;

            float cxx = (a[0]+b[0]+d[0]) / 3.0f, cyy = (a[1]+b[1]+d[1]) / 3.0f;
            float dxy = sqrtf((cxx-x)*(cxx-x) + (cyy-y)*(cyy-y));
            if (dxy > radius + 1.0f) continue;

            const char *verdict;
            if (fabsf(unz) >= walk)      verdict = fabsf(unz) == unz ? "skipped: floor" : "skipped: ceiling";
            else if (zmax <= z0 + 0.18f) verdict = "skipped: ledge lip";
            else                         verdict = "WALL: can push";
            printf("    tri %6u  n=(%+.2f %+.2f %+.2f)  z %.2f..%.2f  d=%.2f  %s\n",
                   t, nx/nlen, ny/nlen, unz, zmin, zmax, dxy, verdict);
            if (++seen >= 24u) { printf("    ... (more)\n"); return; }
        }
    }
    if (!seen) printf("    (none)\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <cache.map> --at X Y [--z Z] [--span WU] [--step WU]\n"
                "       %s <cache.map> --find Z [--near X Y] [--tol WU]\n"
                "\nYou must supply your own legally obtained Halo Trial data.\n",
                argv[0], argv[0]);
        return 2;
    }
    float at_x = 0.0f, at_y = 0.0f, span = 6.0f, step = 0.25f;
    /* Where to drop the ground probe from. The engine probes from the pawn's
     * own Z, so under a roof it finds the floor you stand on; probing from the
     * sky finds the roof instead and reports a different world entirely. */
    float from_z = 40.0f;
    /* --find: a screenshot readout can lose a digit to the status bar. Given
     * the numbers that survived, search the map for floor where a pawn would
     * stand at that height, and say which of those cells block standing. */
    float find_z = 0.0f, find_tol = 0.06f, near_x = 0.0f, near_y = 0.0f;
    bool have_find = false, have_near = false;
    bool have_at = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--at") && i + 2 < argc) {
            at_x = strtof(argv[i+1], NULL);
            at_y = strtof(argv[i+2], NULL);
            have_at = true;
            i += 2;
        } else if (!strcmp(argv[i], "--find") && i + 1 < argc) {
            find_z = strtof(argv[++i], NULL);
            have_find = true;
        } else if (!strcmp(argv[i], "--near") && i + 2 < argc) {
            near_x = strtof(argv[i+1], NULL);
            near_y = strtof(argv[i+2], NULL);
            have_near = true;
            i += 2;
        } else if (!strcmp(argv[i], "--tol") && i + 1 < argc) {
            find_tol = strtof(argv[++i], NULL);
        } else if (!strcmp(argv[i], "--z") && i + 1 < argc) {
            from_z = strtof(argv[++i], NULL) + 0.30f;
        } else if (!strcmp(argv[i], "--span") && i + 1 < argc) {
            span = strtof(argv[++i], NULL);
        } else if (!strcmp(argv[i], "--step") && i + 1 < argc) {
            step = strtof(argv[++i], NULL);
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }
    if (!have_at && !have_find) {
        fprintf(stderr, "--at X Y or --find Z is required\n");
        return 2;
    }
    if (step < 0.02f) step = 0.02f;

    size_t size = 0;
    uint8_t *data = slurp(argv[1], &size);
    if (!data) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    hta_cache c;
    char err[HTA_ERRLEN] = {0};
    if (!hta_cache_open(&c, data, size, err, sizeof(err))) {
        fprintf(stderr, "not a usable cache file: %s\n", err);
        free(data);
        return 1;
    }

    hta_player_physics phys;
    hta_player_physics_defaults(&phys);
    hta_player_physics_load(&phys, &c, err, sizeof(err));

    hta_bsp_mesh coll;
    if (!hta_bsp_load_collision(&c, &coll, err, sizeof(err))) {
        fprintf(stderr, "collision BSP: %s\n", err);
        free(data);
        return 1;
    }
    uint32_t struct_tris = coll.index_count / 3;
    hta_scenario_add_collision(&coll, &c, err, sizeof(err));

    hta_collision col;
    if (!hta_collision_build(&col, &coll)) {
        fprintf(stderr, "collision grid failed\n");
        free(data);
        return 1;
    }
    hta_collision_set_slope(&col, phys.max_slope);

    printf("map        %s\n", argv[1]);
    printf("collision  %u tris (%u structure + %u object)\n",
           coll.index_count / 3, struct_tris, coll.index_count / 3 - struct_tris);
    printf("pawn       radius %.2f  stand %.2f  crouch %.2f  slope %.1f deg\n",
           phys.radius, phys.coll_stand, phys.coll_crouch,
           phys.max_slope * 57.2957795f);

    /* Every surface in the column, top down. A single ground query answers
     * "what is below Z", which is the wrong question when the reporter is
     * inside a structure: you get the roof and measure a different room. */
    printf("\n[column at %.2f %.2f]\n", at_x, at_y);
    {
        float z = 200.0f, surf = 0.0f;
        int n_surf = 0;
        while (n_surf < 16 && hta_collision_ground(&col, at_x, at_y, z, &surf)) {
            float head = headroom(&col, at_x, at_y, surf + 0.04f, 20.0f);
            printf("  surface z %8.3f   headroom above %6.3f%s\n", surf, head,
                   head < phys.coll_stand ? "   (cannot stand here)" : "");
            /* hta_collision_ground accepts anything up to z_from + a 0.18
             * step, so stepping down by less than that returns the same
             * surface forever. */
            z = surf - 0.20f;
            n_surf++;
        }
        if (!n_surf) printf("  (no surfaces at all in this column)\n");
    }

    if (have_find) {
        printf("\n[floor cells standing at z = %.2f +/- %.2f]\n", find_z, find_tol);
        uint32_t hits = 0, blocked = 0;
        float bx0 = col.min[0], by0 = col.min[1];
        float bx1 = col.min[0] + (float)col.nx * col.cell;
        float by1 = col.min[1] + (float)col.ny * col.cell;
        for (float y = by0; y <= by1; y += 0.5f)
        for (float x = bx0; x <= bx1; x += 0.5f) {
            float g;
            /* Probe just above the height we are looking for, so a roof
             * higher up in the same column cannot mask the floor. */
            if (!hta_collision_ground(&col, x, y, find_z + 0.10f, &g)) continue;
            if (fabsf(g - find_z) > find_tol) continue;
            hits++;
            float feet = g + 0.02f;
            float ps = push_for(&col, x, y, feet, phys.coll_stand, phys.radius);
            float pc = push_for(&col, x, y, feet, phys.coll_crouch, phys.radius);
            float head = headroom(&col, x, y, feet + 0.02f, 6.0f);
            bool stuck = ps > 0.001f;
            if (stuck) blocked++;
            if (have_near && (fabsf(x - near_x) > 12.0f || fabsf(y - near_y) > 12.0f))
                continue;
            if (hits <= 400u)
                printf("  %8.2f %9.2f  z %.3f  headroom %5.3f  stand %s  crouch %s\n",
                       x, y, g, head,
                       stuck ? "BLOCKED" : "ok     ",
                       pc > 0.001f ? "BLOCKED" : "ok");
        }
        printf("  %u cells at that height, %u block a standing pawn\n", hits, blocked);
    }

    if (!have_at) {
        hta_collision_free(&col);
        hta_bsp_free(&coll);
        free(data);
        return 0;
    }

    float gz = 0.0f;
    bool grounded = hta_collision_ground(&col, at_x, at_y, from_z, &gz);
    printf("\n[at %.2f %.2f, probing down from z=%.2f]\n", at_x, at_y, from_z);
    if (!grounded) {
        printf("  no ground below z=%.2f here\n", from_z);
    } else {
        float feet = gz + 0.02f;
        float head = headroom(&col, at_x, at_y, feet + 0.02f, 6.0f);
        float ps = push_for(&col, at_x, at_y, feet, phys.coll_stand, phys.radius);
        float pc = push_for(&col, at_x, at_y, feet, phys.coll_crouch, phys.radius);
        printf("  ground z   %.3f\n", gz);
        printf("  headroom   %.3f  (stand needs %.2f, crouch needs %.2f)\n",
               head, phys.coll_stand, phys.coll_crouch);
        printf("  push standing  %.3f %s\n", ps, ps > 0.001f ? "<- BLOCKED" : "");
        printf("  push crouching %.3f %s\n", pc, pc > 0.001f ? "<- BLOCKED" : "");
        explain(&col, at_x, at_y, feet, phys.coll_stand, phys.radius);
    }

    /* Patch map. '.' walk standing, 'c' crouch only, '#' blocked either way,
     * ' ' no ground. Y increases upward in the printout, X to the right. */
    printf("\n[%.1f wu patch, %.2f wu cells]  . stand   c crouch-only   # blocked   (space) no ground\n",
           span, step);
    int n = (int)(span / step);
    if (n > 120) n = 120;
    uint32_t stand_only = 0, both = 0, cells = 0;
    printf("      x %.2f", at_x - span * 0.5f);
    printf("  ..  %.2f\n", at_x + span * 0.5f);
    for (int j = n; j >= -n; j--) {
        float y = at_y + (float)j * step;
        printf("  %+8.2f  ", y);
        for (int i = -n; i <= n; i++) {
            float x = at_x + (float)i * step;
            float g;
            if (!hta_collision_ground(&col, x, y, from_z, &g)) { putchar(' '); continue; }
            cells++;
            float feet = g + 0.02f;
            float ps = push_for(&col, x, y, feet, phys.coll_stand, phys.radius);
            float pc = push_for(&col, x, y, feet, phys.coll_crouch, phys.radius);
            char ch;
            if (ps <= 0.001f)      ch = '.';
            else if (pc <= 0.001f) { ch = 'c'; stand_only++; }
            else                   { ch = '#'; both++; }
            if (i == 0 && j == 0) ch = (ch == '.') ? '+' : ch;
            putchar(ch);
        }
        putchar('\n');
    }
    printf("  %u floor cells, %u crouch-only, %u blocked either way\n",
           cells, stand_only, both);

    hta_collision_free(&col);
    hta_bsp_free(&coll);
    free(data);
    return 0;
}
