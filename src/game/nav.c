#include "nav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The eight neighbours, orthogonals first so a diagonal can ask whether
 * both of the orthogonals it cuts between are open. */
static const int DX[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
static const int DY[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
/* The two orthogonals each diagonal passes between. */
static const int DIAG_A[8] = { -1, -1, -1, -1, 0, 1, 2, 3 };
static const int DIAG_B[8] = { -1, -1, -1, -1, 1, 2, 3, 0 };

/* The ground query's own step allowance: a rise under this is a step. */
#define NAV_STEP 0.18f
/* Rays at knee and chest height between neighbours. */
#define NAV_KNEE 0.25f

void hta_nav_free(hta_nav *n)
{
    if (!n) return;
    free(n->col_start);
    free(n->nodes);
    free(n->g);
    free(n->f);
    free(n->parent);
    free(n->heap);
    free(n->stamp);
    memset(n, 0, sizeof(*n));
}

static bool blocked(const hta_collision *c, const float a[3], const float b[3])
{
    float d[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (len < 1e-5f) return false;
    for (int k = 0; k < 3; k++) d[k] /= len;
    float t;
    return hta_collision_ray(c, a, d, len, &t, NULL, NULL);
}

static uint32_t column(const hta_nav *n, int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= (int)n->nx || cy >= (int)n->ny) return HTA_NAV_NONE;
    return (uint32_t)cy * n->nx + (uint32_t)cx;
}

bool hta_nav_build(hta_nav *n, const hta_collision *col_in,
                   const float bmin[3], const float bmax[3],
                   const hta_nav_params *p, char *err, size_t errlen)
{
    if (!n || !col_in || !col_in->built || !p) {
        if (err) snprintf(err, errlen, "no collision to build on");
        return false;
    }
    memset(n, 0, sizeof(*n));
    /* Only the static world: a parked Warthog is not a wall forever. */
    hta_collision col = *col_in;
    col.extra = NULL;

    n->cell = HTA_NAV_CELL;
    n->min[0] = bmin[0];
    n->min[1] = bmin[1];
    n->nx = (uint32_t)ceilf((bmax[0] - bmin[0]) / n->cell);
    n->ny = (uint32_t)ceilf((bmax[1] - bmin[1]) / n->cell);
    if (!n->nx || !n->ny || n->nx > 4096u || n->ny > 4096u) {
        if (err) snprintf(err, errlen, "bad bounds");
        return false;
    }
    uint32_t cols = n->nx * n->ny;
    n->col_start = (uint32_t *)calloc((size_t)cols + 1u, sizeof(uint32_t));
    uint32_t cap = cols + cols / 4u + 64u;
    n->nodes = (hta_nav_node *)malloc((size_t)cap * sizeof(hta_nav_node));
    if (!n->col_start || !n->nodes) {
        hta_nav_free(n);
        if (err) snprintf(err, errlen, "out of memory");
        return false;
    }

    const float top = bmax[2] + 1.0f;
    const float half = n->cell * 0.5f;
    for (uint32_t cy = 0; cy < n->ny; cy++)
    for (uint32_t cx = 0; cx < n->nx; cx++) {
        n->col_start[cy * n->nx + cx] = n->node_count;
        float x = n->min[0] + ((float)cx + 0.5f) * n->cell;
        float y = n->min[1] + ((float)cy + 0.5f) * n->cell;
        float from = top;
        for (uint32_t layer = 0; layer < HTA_NAV_MAX_LAYERS; layer++) {
            float z;
            if (!hta_collision_ground(&col, x, y, from, &z)) break;
            from = z - 0.3f;
            /* Headroom: a standing biped, not its crouch. */
            float up0[3] = { x, y, z + 0.05f };
            float up1[3] = { x, y, z + p->height };
            if (blocked(&col, up0, up1)) continue;
            /* Does it fit between the walls? The player's own push says. */
            float px = x, py = y;
            hta_collision_depenetrate(&col, &px, &py, z, p->height, p->radius);
            float moved = hypotf(px - x, py - y);
            if (moved > half) continue;
            if (n->node_count >= cap) {
                cap += cap / 2u;
                hta_nav_node *nn = (hta_nav_node *)realloc(n->nodes,
                    (size_t)cap * sizeof(hta_nav_node));
                if (!nn) { hta_nav_free(n); if (err) snprintf(err, errlen, "out of memory"); return false; }
                n->nodes = nn;
            }
            hta_nav_node *nd = &n->nodes[n->node_count++];
            memset(nd, 0, sizeof(*nd));
            nd->z = z;
            nd->cx = (uint16_t)cx;
            nd->cy = (uint16_t)cy;
            nd->flags = moved > 0.01f ? HTA_NAV_NEAR_WALL : 0u;
            for (int k = 0; k < 8; k++) nd->link[k] = HTA_NAV_NONE;
        }
    }
    n->col_start[cols] = n->node_count;

    /* Links. Orthogonals first, so diagonals can refuse to cut a corner. */
    float tan_slope = tanf(p->max_slope > 0.1f ? p->max_slope : 0.7f);
    for (int pass = 0; pass < 2; pass++)
    for (uint32_t i = 0; i < n->node_count; i++) {
        hta_nav_node *a = &n->nodes[i];
        for (int d = pass ? 4 : 0; d < (pass ? 8 : 4); d++) {
            if (d >= 4) {
                if (a->link[DIAG_A[d]] == HTA_NAV_NONE ||
                    a->link[DIAG_B[d]] == HTA_NAV_NONE) continue;
            }
            uint32_t ci = column(n, (int)a->cx + DX[d], (int)a->cy + DY[d]);
            if (ci == HTA_NAV_NONE) continue;
            float dist = n->cell * (d >= 4 ? 1.41421356f : 1.0f);
            float rise = NAV_STEP + dist * tan_slope;
            uint32_t best = HTA_NAV_NONE;
            float best_dz = 1e9f;
            for (uint32_t j = n->col_start[ci]; j < n->col_start[ci + 1u]; j++) {
                float dz = n->nodes[j].z - a->z;
                if (dz > rise || -dz > p->max_drop) continue;
                if (fabsf(dz) < best_dz) { best_dz = fabsf(dz); best = j; }
            }
            if (best == HTA_NAV_NONE) continue;
            const hta_nav_node *b = &n->nodes[best];
            float ax = n->min[0] + ((float)a->cx + 0.5f) * n->cell;
            float ay = n->min[1] + ((float)a->cy + 0.5f) * n->cell;
            float bx = n->min[0] + ((float)b->cx + 0.5f) * n->cell;
            float by = n->min[1] + ((float)b->cy + 0.5f) * n->cell;
            /* A drop walks off at the upper height and falls. */
            float hi = a->z > b->z ? a->z : b->z;
            float k0[3] = { ax, ay, a->z + NAV_KNEE };
            float k1[3] = { bx, by, (b->z < a->z ? a->z : b->z) + NAV_KNEE };
            if (blocked(&col, k0, k1)) continue;
            float c0[3] = { ax, ay, hi + p->height * 0.85f };
            float c1[3] = { bx, by, hi + p->height * 0.85f };
            if (blocked(&col, c0, c1)) continue;
            a->link[d] = best;
            n->link_count++;
        }
    }

    /* Regions: which nodes can reach which, links taken both ways. */
    uint32_t *stack = (uint32_t *)malloc((size_t)(n->node_count + 1u) * sizeof(uint32_t));
    uint32_t sizes[256];
    memset(sizes, 0, sizeof(sizes));
    uint32_t region = 0;
    if (stack) {
        /* A reverse-link pass is needed for undirected flood; build a
         * cheap one by flooding forward repeatedly until stable is too
         * slow, so treat any link as joining both ends through a union. */
        uint32_t *parent = (uint32_t *)malloc((size_t)n->node_count * sizeof(uint32_t));
        if (parent) {
            for (uint32_t i = 0; i < n->node_count; i++) parent[i] = i;
            for (uint32_t i = 0; i < n->node_count; i++)
                for (int d = 0; d < 8; d++) {
                    uint32_t j = n->nodes[i].link[d];
                    if (j == HTA_NAV_NONE) continue;
                    uint32_t ra = i, rb = j;
                    while (parent[ra] != ra) { parent[ra] = parent[parent[ra]]; ra = parent[ra]; }
                    while (parent[rb] != rb) { parent[rb] = parent[parent[rb]]; rb = parent[rb]; }
                    if (ra != rb) parent[ra] = rb;
                }
            /* Number the roots by size: the big ones get real region ids. */
            uint32_t *count = stack;
            memset(count, 0, (size_t)(n->node_count + 1u) * sizeof(uint32_t));
            for (uint32_t i = 0; i < n->node_count; i++) {
                uint32_t r = i;
                while (parent[r] != r) r = parent[r];
                parent[i] = r;
                count[r]++;
            }
            uint32_t *id = (uint32_t *)calloc((size_t)n->node_count, sizeof(uint32_t));
            if (id) {
                for (uint32_t i = 0; i < n->node_count; i++) {
                    uint32_t r = parent[i];
                    if (count[r] < 32u) continue;   /* a ledge nobody goes to */
                    if (!id[r] && region < 255u) { id[r] = ++region; }
                    n->nodes[i].region = (uint8_t)id[r];
                    if (id[r]) sizes[id[r]]++;
                }
                free(id);
            }
            free(parent);
        }
        free(stack);
    }
    uint32_t best_size = 0;
    for (uint32_t r = 1; r <= region; r++)
        if (sizes[r] > best_size) { best_size = sizes[r]; n->main_region = (uint8_t)r; }

    n->g = (float *)malloc((size_t)n->node_count * sizeof(float));
    n->f = (float *)malloc((size_t)n->node_count * sizeof(float));
    n->parent = (uint32_t *)malloc((size_t)n->node_count * sizeof(uint32_t));
    n->heap = (uint32_t *)malloc((size_t)n->node_count * sizeof(uint32_t));
    n->stamp = (uint32_t *)calloc((size_t)n->node_count, sizeof(uint32_t));
    if (!n->g || !n->f || !n->parent || !n->heap || !n->stamp) {
        hta_nav_free(n);
        if (err) snprintf(err, errlen, "out of memory");
        return false;
    }
    n->built = true;
    if (err && errlen)
        snprintf(err, errlen, "%ux%u cells, %u nodes, %u links, %u regions, main %u nodes",
                 n->nx, n->ny, n->node_count, n->link_count, region, best_size);
    return true;
}

void hta_nav_pos(const hta_nav *n, uint32_t node, float out[3])
{
    const hta_nav_node *nd = &n->nodes[node];
    out[0] = n->min[0] + ((float)nd->cx + 0.5f) * n->cell;
    out[1] = n->min[1] + ((float)nd->cy + 0.5f) * n->cell;
    out[2] = nd->z;
}

uint32_t hta_nav_nearest(const hta_nav *n, const float feet[3], float reach)
{
    if (!n || !n->built) return HTA_NAV_NONE;
    int cx = (int)floorf((feet[0] - n->min[0]) / n->cell);
    int cy = (int)floorf((feet[1] - n->min[1]) / n->cell);
    int r = (int)ceilf(reach / n->cell);
    uint32_t best = HTA_NAV_NONE;
    float best_d = 1e30f;
    for (int ring = 0; ring <= r; ring++) {
        for (int y = cy - ring; y <= cy + ring; y++)
        for (int x = cx - ring; x <= cx + ring; x++) {
            if (ring && x != cx - ring && x != cx + ring &&
                y != cy - ring && y != cy + ring) continue;
            uint32_t ci = column(n, x, y);
            if (ci == HTA_NAV_NONE) continue;
            for (uint32_t j = n->col_start[ci]; j < n->col_start[ci + 1u]; j++) {
                const hta_nav_node *nd = &n->nodes[j];
                if (!nd->region) continue;
                float dz = feet[2] - nd->z;
                /* Standing on it, or a little above it in a jump; never a
                 * floor overhead. */
                if (dz < -0.3f) continue;
                float px = n->min[0] + ((float)nd->cx + 0.5f) * n->cell - feet[0];
                float py = n->min[1] + ((float)nd->cy + 0.5f) * n->cell - feet[1];
                float d = px*px + py*py + dz*dz * 4.0f;
                if (d < best_d) { best_d = d; best = j; }
            }
        }
        if (best != HTA_NAV_NONE) return best;
    }
    return best;
}

/* Binary min-heap keyed on f = g + h, kept in n->heap. */
typedef struct { const hta_nav *n; float *f; uint32_t size; } heap_t;

static void heap_push(heap_t *h, uint32_t *arr, uint32_t node)
{
    uint32_t i = h->size++;
    arr[i] = node;
    while (i) {
        uint32_t pi = (i - 1u) / 2u;
        if (h->f[arr[pi]] <= h->f[arr[i]]) break;
        uint32_t t = arr[pi]; arr[pi] = arr[i]; arr[i] = t;
        i = pi;
    }
}

static uint32_t heap_pop(heap_t *h, uint32_t *arr)
{
    uint32_t top = arr[0];
    arr[0] = arr[--h->size];
    uint32_t i = 0;
    for (;;) {
        uint32_t l = i * 2u + 1u, r = l + 1u, m = i;
        if (l < h->size && h->f[arr[l]] < h->f[arr[m]]) m = l;
        if (r < h->size && h->f[arr[r]] < h->f[arr[m]]) m = r;
        if (m == i) break;
        uint32_t t = arr[m]; arr[m] = arr[i]; arr[i] = t;
        i = m;
    }
    return top;
}

uint32_t hta_nav_path(hta_nav *n, uint32_t from, uint32_t to,
                      uint32_t *out, uint32_t max, uint32_t budget)
{
    if (!n || !n->built || from >= n->node_count || to >= n->node_count || !max)
        return 0;
    if (n->nodes[from].region != n->nodes[to].region) return 0;
    if (from == to) { out[0] = from; return 1; }

    float *fbuf = n->f;
    if (++n->generation == 0u) {
        memset(n->stamp, 0, (size_t)n->node_count * sizeof(uint32_t));
        n->generation = 1u;
    }
    const uint32_t gen = n->generation;
    float goal[3];
    hta_nav_pos(n, to, goal);
    heap_t h = { n, fbuf, 0 };

    n->stamp[from] = gen;
    n->g[from] = 0.0f;
    n->parent[from] = HTA_NAV_NONE;
    {
        float p[3];
        hta_nav_pos(n, from, p);
        fbuf[from] = sqrtf((p[0]-goal[0])*(p[0]-goal[0]) + (p[1]-goal[1])*(p[1]-goal[1]));
    }
    heap_push(&h, n->heap, from);
    uint32_t expanded = 0;
    bool found = false;
    while (h.size) {
        uint32_t cur = heap_pop(&h, n->heap);
        if (cur == to) { found = true; break; }
        if (++expanded > budget) break;
        const hta_nav_node *a = &n->nodes[cur];
        for (int d = 0; d < 8; d++) {
            uint32_t nb = a->link[d];
            if (nb == HTA_NAV_NONE) continue;
            const hta_nav_node *b = &n->nodes[nb];
            float step = n->cell * (d >= 4 ? 1.41421356f : 1.0f);
            float dz = b->z - a->z;
            float cost = step + (dz > 0.0f ? dz * 2.0f : -dz * 0.5f);
            if (b->flags & HTA_NAV_NEAR_WALL) cost *= 1.6f;
            float ng = n->g[cur] + cost;
            if (n->stamp[nb] == gen && ng >= n->g[nb]) continue;
            n->stamp[nb] = gen;
            n->g[nb] = ng;
            n->parent[nb] = cur;
            float p[3];
            hta_nav_pos(n, nb, p);
            float hx = p[0]-goal[0], hy = p[1]-goal[1], hz = p[2]-goal[2];
            fbuf[nb] = ng + sqrtf(hx*hx + hy*hy + hz*hz);
            if (h.size < n->node_count) heap_push(&h, n->heap, nb);
        }
    }
    if (!found) return 0;

    /* Walk back from the goal into the heap's storage, which the search
     * no longer needs, then hand out the first `max` from the start. */
    uint32_t len = 0;
    for (uint32_t c = to; c != HTA_NAV_NONE && len < n->node_count; c = n->parent[c])
        n->heap[len++] = c;
    uint32_t w = len < max ? len : max;
    for (uint32_t k = 0; k < w; k++) out[k] = n->heap[len - 1u - k];
    return w;
}

bool hta_nav_straight(const hta_nav *n, uint32_t a, uint32_t b)
{
    if (!n || !n->built) return false;
    float pa[3], pb[3];
    hta_nav_pos(n, a, pa);
    hta_nav_pos(n, b, pb);
    float dx = pb[0] - pa[0], dy = pb[1] - pa[1];
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1e-4f) return true;
    int steps = (int)ceilf(len / (n->cell * 0.5f));
    float z = pa[2];
    for (int s = 1; s <= steps; s++) {
        float t = (float)s / (float)steps;
        float x = pa[0] + dx * t, y = pa[1] + dy * t;
        int cx = (int)floorf((x - n->min[0]) / n->cell);
        int cy = (int)floorf((y - n->min[1]) / n->cell);
        uint32_t ci = column(n, cx, cy);
        if (ci == HTA_NAV_NONE) return false;
        bool ok = false;
        for (uint32_t j = n->col_start[ci]; j < n->col_start[ci + 1u]; j++) {
            const hta_nav_node *nd = &n->nodes[j];
            if (nd->flags & HTA_NAV_NEAR_WALL) continue;
            if (fabsf(nd->z - z) <= 0.22f) { z = nd->z; ok = true; break; }
        }
        if (!ok) return false;
    }
    return fabsf(z - pb[2]) <= 0.25f;
}

uint32_t hta_nav_smooth(const hta_nav *n, uint32_t *path, uint32_t len)
{
    if (len <= 2) return len;
    uint32_t w = 1, anchor = 0;
    uint32_t i = 1;
    while (i < len) {
        /* Reach as far ahead as a straight walk allows. */
        uint32_t far = i;
        for (uint32_t k = i + 1u; k < len && k < i + 40u; k++)
            if (hta_nav_straight(n, path[anchor], path[k])) far = k;
        path[w++] = path[far];
        anchor = far;
        i = far + 1u;
    }
    if (path[w - 1u] != path[len - 1u]) path[w++] = path[len - 1u];
    return w;
}

uint32_t hta_nav_random(const hta_nav *n, uint32_t *rng)
{
    if (!n || !n->built || !n->node_count) return HTA_NAV_NONE;
    for (int tries = 0; tries < 64; tries++) {
        *rng = *rng * 1664525u + 1013904223u;
        uint32_t i = (*rng >> 8) % n->node_count;
        if (n->nodes[i].region == n->main_region &&
            !(n->nodes[i].flags & HTA_NAV_NEAR_WALL)) return i;
    }
    return HTA_NAV_NONE;
}

#define NAV_MAGIC   0x4E415648u   /* "HVAN" */
#define NAV_VERSION 1u

typedef struct {
    uint32_t magic, version, key, node_size;
    uint32_t nx, ny, node_count, link_count;
    float    min[2], cell;
    uint32_t main_region;
} nav_file_header;

bool hta_nav_save(const hta_nav *n, const char *path, uint32_t key)
{
    if (!n || !n->built || !path) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    nav_file_header h = { NAV_MAGIC, NAV_VERSION, key, (uint32_t)sizeof(hta_nav_node),
                          n->nx, n->ny, n->node_count, n->link_count,
                          { n->min[0], n->min[1] }, n->cell, n->main_region };
    size_t cols = (size_t)n->nx * n->ny + 1u;
    bool ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
              fwrite(n->col_start, sizeof(uint32_t), cols, f) == cols &&
              fwrite(n->nodes, sizeof(hta_nav_node), n->node_count, f) == n->node_count;
    ok = (fclose(f) == 0) && ok;
    if (!ok) remove(path);
    return ok;
}

bool hta_nav_load(hta_nav *n, const char *path, uint32_t key)
{
    if (!n || !path) return false;
    memset(n, 0, sizeof(*n));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    nav_file_header h;
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == NAV_MAGIC &&
              h.version == NAV_VERSION && h.key == key &&
              h.node_size == sizeof(hta_nav_node) && h.nx && h.ny &&
              h.nx <= 4096u && h.ny <= 4096u && h.node_count <= 8u * h.nx * h.ny;
    if (ok) {
        size_t cols = (size_t)h.nx * h.ny + 1u;
        n->col_start = (uint32_t *)malloc(cols * sizeof(uint32_t));
        n->nodes = (hta_nav_node *)malloc((size_t)(h.node_count ? h.node_count : 1u) * sizeof(hta_nav_node));
        ok = n->col_start && n->nodes &&
             fread(n->col_start, sizeof(uint32_t), cols, f) == cols &&
             fread(n->nodes, sizeof(hta_nav_node), h.node_count, f) == h.node_count &&
             n->col_start[cols - 1u] == h.node_count;
        /* Never trust a link out of range: a truncated or foreign file
         * must not become an out-of-bounds read in the middle of a game. */
        for (uint32_t i = 0; ok && i < h.node_count; i++)
            for (int d = 0; d < 8; d++)
                if (n->nodes[i].link[d] != HTA_NAV_NONE && n->nodes[i].link[d] >= h.node_count)
                    ok = false;
        for (size_t c = 1; ok && c < cols; c++)
            if (n->col_start[c] < n->col_start[c - 1u]) ok = false;
    }
    fclose(f);
    if (!ok) { hta_nav_free(n); return false; }
    n->nx = h.nx; n->ny = h.ny;
    n->node_count = h.node_count; n->link_count = h.link_count;
    n->min[0] = h.min[0]; n->min[1] = h.min[1]; n->cell = h.cell;
    n->main_region = (uint8_t)h.main_region;
    size_t nc = n->node_count ? n->node_count : 1u;
    n->g = (float *)malloc(nc * sizeof(float));
    n->f = (float *)malloc(nc * sizeof(float));
    n->parent = (uint32_t *)malloc(nc * sizeof(uint32_t));
    n->heap = (uint32_t *)malloc(nc * sizeof(uint32_t));
    n->stamp = (uint32_t *)calloc(nc, sizeof(uint32_t));
    if (!n->g || !n->f || !n->parent || !n->heap || !n->stamp) { hta_nav_free(n); return false; }
    n->built = true;
    return true;
}
