#include "engine/rigid.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static hta_vertex V[64];
static uint32_t I[96];
static uint32_t nv, ni;

static void tri(const float a[3], const float b[3], const float c[3])
{
    const float *p[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        memset(&V[nv], 0, sizeof(V[nv]));
        memcpy(V[nv].pos, p[i], 12);
        I[ni++] = nv++;
    }
}
static void quad(const float a[3], const float b[3], const float c[3], const float d[3])
{
    tri(a, b, c); tri(a, c, d);
}

/* A floor at z=0 from x -20..20, a 30-degree ramp rising from x=20, and a
 * thin wall at x=-10 (a single quad, no thickness at all). */
static void build(hta_bsp_mesh *m, hta_collision *col)
{
    nv = ni = 0;
    { float a[3]={-20,-20,0}, b[3]={20,-20,0}, c[3]={20,20,0}, d[3]={-20,20,0}; quad(a,b,c,d); }
    float s = tanf(30.0f * 3.14159265f / 180.0f);
    { float a[3]={20,-20,0}, b[3]={60,-20,40*s}, c[3]={60,20,40*s}, d[3]={20,20,0}; quad(a,b,c,d); }
    { float a[3]={-10,-20,0}, b[3]={-10,20,0}, c[3]={-10,20,10}, d[3]={-10,-20,10}; quad(a,b,c,d); }
    memset(m, 0, sizeof(*m));
    m->vertices = V; m->vertex_count = nv; m->indices = I; m->index_count = ni;
    m->bounds_min[0] = -20; m->bounds_min[1] = -20; m->bounds_min[2] = 0;
    m->bounds_max[0] = 60; m->bounds_max[1] = 20; m->bounds_max[2] = 40 * s;
    assert(hta_collision_build(col, m));
}

static void run(hta_rigid_world *w, float seconds)
{
    for (float t = 0; t < seconds; t += 1.0f / 60.0f) hta_rigid_step(w, 1.0f / 60.0f);
}

static hta_rigid_desc box_at(float x, float y, float z, float hx, float hy, float hz)
{
    hta_rigid_desc d;
    memset(&d, 0, sizeof(d));
    d.shape = HTA_RIGID_BOX; d.material = HTA_RMAT_WOOD;
    d.half[0] = hx; d.half[1] = hy; d.half[2] = hz;
    d.pos[0] = x; d.pos[1] = y; d.pos[2] = z;
    return d;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    hta_bsp_mesh mesh;
    hta_collision col;
    build(&mesh, &col);

    /* Contacts: a sphere half sunk into the floor. */
    {
        hta_contact c[4];
        float p[3] = { 1, 1, 0.1f };
        uint32_t n = hta_collision_sphere(&col, p, 0.25f, c, 4);
        assert(n == 1);                      /* two floor triangles, one merged contact */
        assert(fabsf(c[0].normal[2] - 1.0f) < 1e-4f && fabsf(c[0].depth - 0.15f) < 1e-4f);
        float q[3] = { 1, 1, 0.3f };
        assert(hta_collision_sphere(&col, q, 0.25f, c, 4) == 0);
    }

    hta_rigid_world w;
    assert(hta_rigid_init(&w, 32, &col));

    /* 1. A ball dropped from 3 wu comes to rest on the floor and sleeps,
     * and landing is reported as an impact. */
    {
        hta_rigid_desc d = box_at(0, 0, 3, 0.2f, 0, 0);
        d.shape = HTA_RIGID_SPHERE; d.material = HTA_RMAT_METAL;
        uint32_t i = hta_rigid_spawn(&w, &d);
        int impacts = 0;
        for (int s = 0; s < 240; s++) { hta_rigid_step(&w, 1.0f / 60.0f); impacts += w.impact_count > 0; }
        const hta_rigid_body *b = &w.bodies[i];
        assert(impacts >= 1);
        assert(fabsf(b->pos[2] - 0.2f) < 0.02f);
        assert(b->asleep);
        hta_rigid_clear(&w);
    }

    /* 2. A plank dropped tumbling lands, tips and ends up lying FLAT: its
     * thin axis vertical, its centre at its half-thickness. That needs the
     * contact torque to be right. */
    {
        hta_rigid_desc d = box_at(0, 0, 2.0f, 0.5f, 0.2f, 0.05f);
        d.rot[0] = cosf(0.6f); d.rot[1] = sinf(0.6f) * 0.6f; d.rot[2] = sinf(0.6f) * 0.8f; d.rot[3] = 0;
        d.ang[0] = 3.0f; d.ang[1] = -2.0f;
        uint32_t i = hta_rigid_spawn(&w, &d);
        run(&w, 6.0f);
        const hta_rigid_body *b = &w.bodies[i];
        float m[16];
        hta_rigid_matrix(b, m);
        /* Column 2 is the plank's local Z (its thin axis) in the world. */
        float up = fabsf(m[10]);
        printf("plank: z=%.3f thin-axis.z=%.3f asleep=%d\n", b->pos[2], up, b->asleep);
        assert(up > 0.97f);
        assert(fabsf(b->pos[2] - 0.05f) < 0.03f);
        assert(b->asleep);
        hta_rigid_clear(&w);
    }

    /* 3. On the 30-degree ramp: a rough box (friction 0.6 > tan 30 = 0.577)
     * stays; a ball rolls down onto the floor. */
    {
        float s = tanf(30.0f * 3.14159265f / 180.0f);
        float x = 40, z = (x - 20) * s;
        hta_rigid_desc d = box_at(x, 0, z + 0.25f, 0.3f, 0.3f, 0.2f);
        float half = 15.0f * 3.14159265f / 180.0f;   /* sit it along the ramp */
        d.rot[0] = cosf(half); d.rot[2] = -sinf(half);
        uint32_t bx = hta_rigid_spawn(&w, &d);
        hta_rigid_desc e = box_at(x, 5, z + 0.35f, 0.2f, 0, 0);
        e.shape = HTA_RIGID_SPHERE; e.material = HTA_RMAT_METAL;
        uint32_t bl = hta_rigid_spawn(&w, &e);
        run(&w, 3.0f);
        /* Rolling, not sliding: 5/7 g sin 30 = 1.15 wu/s^2 down the slope,
         * so after 3 s about 5.2 wu along it, 4.5 in x. */
        float rolled = x - w.bodies[bl].pos[0];
        printf("ramp @3s: ball rolled %.2f in x (expect ~4.5)\n", rolled);
        assert(rolled > 3.5f && rolled < 5.5f);
        run(&w, 6.0f);
        printf("ramp: box x %.2f->%.2f, ball x %.2f->%.2f\n", x, w.bodies[bx].pos[0], x, w.bodies[bl].pos[0]);
        assert(fabsf(w.bodies[bx].pos[0] - x) < 1.0f);
        assert(w.bodies[bl].pos[0] < 20.0f);
        hta_rigid_clear(&w);
    }

    /* 4. No tunnelling: a pebble at 200 wu/s (600 m/s) into a wall with
     * no thickness stops on the near side. */
    {
        hta_rigid_desc d = box_at(0, 0, 5, 0.05f, 0, 0);
        d.shape = HTA_RIGID_SPHERE; d.material = HTA_RMAT_CONCRETE;
        d.vel[0] = -200.0f;
        uint32_t i = hta_rigid_spawn(&w, &d);
        run(&w, 1.0f);
        printf("bullet-pebble: x=%.3f\n", w.bodies[i].pos[0]);
        assert(w.bodies[i].pos[0] > -10.0f);
        hta_rigid_clear(&w);
    }

    /* 5. A blast throws resting bodies away from it and wakes them. */
    {
        uint32_t ids[4];
        for (int k = 0; k < 4; k++) {
            hta_rigid_desc d = box_at((float)(k - 2) * 1.0f + 0.5f, 0, 0.15f, 0.15f, 0.15f, 0.15f);
            ids[k] = hta_rigid_spawn(&w, &d);
        }
        run(&w, 1.0f);
        for (int k = 0; k < 4; k++) assert(w.bodies[ids[k]].asleep);
        float c[3] = { 0, 0, 0 };
        hta_rigid_blast(&w, c, 5.0f, 8.0f);
        for (int k = 0; k < 4; k++) {
            const hta_rigid_body *b = &w.bodies[ids[k]];
            assert(!b->asleep && b->vel[2] > 0.0f);
            assert((b->pos[0] < 0) == (b->vel[0] < 0));   /* away from the centre */
        }
        run(&w, 0.3f);
        assert(w.awake >= 4);
        hta_rigid_clear(&w);
    }

    /* 6. Pool: spawning past capacity recycles, the count stays put, and
     * a body with a life fades and goes. */
    {
        for (int k = 0; k < 40; k++) {
            hta_rigid_desc d = box_at((float)(k % 8) - 4, (float)(k / 8) - 2, 1, 0.1f, 0.1f, 0.1f);
            d.life = 1.0f; d.fade = 0.5f;
            hta_rigid_spawn(&w, &d);
        }
        assert(hta_rigid_active(&w) == 32);
        run(&w, 1.2f);
        float a = 1.0f;
        for (uint32_t k = 0; k < w.cap; k++) if (w.bodies[k].active) a = fminf(a, hta_rigid_alpha(&w.bodies[k]));
        assert(a < 1.0f && a > 0.0f);
        run(&w, 0.5f);
        assert(hta_rigid_active(&w) == 0);
        /* Shrinking the pool keeps the youngest. */
        for (int k = 0; k < 10; k++) {
            hta_rigid_desc d = box_at((float)k, 0, 1, 0.1f, 0.1f, 0.1f);
            d.user = (uint32_t)k;
            hta_rigid_spawn(&w, &d);
            run(&w, 0.05f);
        }
        assert(hta_rigid_resize(&w, 4));
        assert(hta_rigid_active(&w) == 4);
        for (uint32_t k = 0; k < 4; k++) assert(w.bodies[k].user >= 6);
    }

    /* 7. Without a world, the floor plane holds things up. */
    {
        hta_rigid_world f;
        assert(hta_rigid_init(&f, 4, NULL));
        f.floor_z = -1.0f;
        hta_rigid_desc d = box_at(0, 0, 2, 0.2f, 0.2f, 0.2f);
        uint32_t i = hta_rigid_spawn(&f, &d);
        run(&f, 6.0f);
        float m[16];
        hta_rigid_matrix(&f.bodies[i], m);
        printf("floor: z=%.3f asleep=%d up=%.3f\n", f.bodies[i].pos[2], f.bodies[i].asleep, m[10]);
        assert(fabsf(f.bodies[i].pos[2] - (-0.8f)) < 0.01f);
        assert(f.bodies[i].asleep && fabsf(m[10]) > 0.99f);   /* flat, not on an edge */
        hta_rigid_free(&f);
    }

    hta_rigid_free(&w);
    hta_collision_free(&col);
    puts("rigid OK");
    return 0;
}
