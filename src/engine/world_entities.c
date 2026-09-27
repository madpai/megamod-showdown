/* World entities at runtime (world_entities.h). */
#include "world_entities.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void diag(hta_world_entities *w, const char *what, uint32_t entity)
{
    const char *id = w->defs && entity < w->defs->count ? w->defs->entity[entity].id : "?";
    snprintf(w->diag, sizeof(w->diag), "%s (%s)", what, id);
    w->diag_count++;
}

hta_went_handle hta_went_handle_of(const hta_world_entities *w, uint32_t index)
{
    if (!w || !w->defs || index >= w->defs->count) return 0;
    return ((hta_went_handle)w->st[index].generation << 16) | index;
}

int32_t hta_went_resolve(const hta_world_entities *w, hta_went_handle h)
{
    uint32_t index = h & 0xFFFFu;
    uint16_t gen = (uint16_t)(h >> 16);
    if (!w || !w->defs || !gen || index >= w->defs->count || w->st[index].generation != gen) return -1;
    return (int32_t)index;
}

static void resolve_links(hta_world_entities *w)
{
    for (uint32_t i = 0; i < w->defs->link_count; i++)
        w->link_target[i] = hta_went_handle_of(w, w->defs->link[i].target);
}

/* A placed mover's definition. Resolved (and checked) at load: an index,
 * never a name. */
static const hta_wmover_def *mdef(const hta_world_entities *w, uint32_t i)
{
    return &w->defs->mover_def[w->defs->entity[i].def];
}

static void place(hta_world_entities *w, uint32_t i)
{
    const hta_wdef *d = &w->defs->entity[i];
    const hta_wmover_def *m = mdef(w, i);
    hta_collision_instance *in = &w->inst[i];
    for (int k = 0; k < 3; k++) in->pos[k] = d->pos[k] + m->move[k] * w->st[i].t;
}

/* Definition `di`'s box grid, in its own space around the centre. */
static bool def_build(hta_world_entities *w, uint32_t di)
{
    const hta_wmover_def *md = &w->defs->mover_def[di];
    float h[3];
    for (int k = 0; k < 3; k++) h[k] = md->size[k] * 0.5f;
    hta_vertex *v = w->mover_verts[di];
    memset(v, 0, sizeof(w->mover_verts[di]));
    for (int c = 0; c < 8; c++)
        for (int k = 0; k < 3; k++) v[c].pos[k] = (c >> k) & 1 ? h[k] : -h[k];
    static const uint32_t ix[36] = {
        0,2,3, 0,3,1,  4,5,7, 4,7,6,  0,1,5, 0,5,4,
        2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
    };
    memcpy(w->mover_idx[di], ix, sizeof(ix));
    hta_bsp_mesh *m = &w->mover_mesh[di];
    memset(m, 0, sizeof(*m));
    m->vertices = v; m->vertex_count = 8;
    m->indices = w->mover_idx[di]; m->index_count = 36;
    for (int k = 0; k < 3; k++) { m->bounds_min[k] = -h[k]; m->bounds_max[k] = h[k]; }
    return hta_collision_build(&w->mover_coll[di], m);
}

/* Placed mover `i`: its own instance of its definition's grid. */
static void mover_place_init(hta_world_entities *w, uint32_t i)
{
    const hta_wmover_def *md = mdef(w, i);
    hta_collision_instance *in = &w->inst[i];
    memset(in, 0, sizeof(*in));
    in->rot[0] = in->rot[4] = in->rot[8] = 1.0f;
    in->grid = &w->mover_coll[w->defs->entity[i].def];
    in->radius = 0.5f * sqrtf(md->size[0] * md->size[0] + md->size[1] * md->size[1] + md->size[2] * md->size[2]) + 0.05f;
    in->active = true;
    place(w, i);
}

bool hta_went_load(hta_world_entities *w, const hta_world_defs *defs, char *err, size_t errlen)
{
    if (!w) return false;
    hta_went_free(w);
    if (!defs || !defs->count) return true;
    if (!hta_world_defs_check(defs, err, errlen)) return false;
    w->defs = defs;
    for (uint32_t i = 0; i < defs->count; i++) w->st[i].generation = 1;
    for (uint32_t k = 0; k < defs->mover_def_count; k++)
        if (!def_build(w, k)) {
            if (err && errlen) snprintf(err, errlen, "%s: mover collision failed",
                                        defs->mover_def[k].id[0] ? defs->mover_def[k].id : "(inline mover)");
            hta_went_free(w);
            return false;
        }
    for (uint32_t i = 0; i < defs->count; i++)
        if (defs->entity[i].kind == HTA_WDEF_MOVER) mover_place_init(w, i);
    resolve_links(w);
    w->loaded = true;
    w->version++;
    return true;
}

void hta_went_free(hta_world_entities *w)
{
    if (!w) return;
    /* Every slot: `defs` may already be gone (a world unloaded first). */
    for (uint32_t i = 0; i < HTA_WDEF_MAX_MOVER_DEFS; i++) hta_collision_free(&w->mover_coll[i]);
    uint32_t version = w->version;
    memset(w, 0, sizeof(*w));
    w->version = version + 1;
}

void hta_went_reset(hta_world_entities *w)
{
    if (!w || !w->loaded) return;
    for (uint32_t i = 0; i < w->defs->count; i++) {
        hta_went_state *s = &w->st[i];
        uint16_t g = (uint16_t)(s->generation + 1u);
        memset(s, 0, sizeof(*s));
        s->generation = g ? g : 1;
        if (w->defs->entity[i].kind == HTA_WDEF_MOVER) place(w, i);
    }
    resolve_links(w);
    w->head = w->count = 0;
    w->teleport_count = 0;
    w->call_count = 0;
    w->version++;
}

static bool push(hta_world_entities *w, hta_went_handle target, uint16_t source, uint8_t input,
                 uint8_t actor, uint8_t depth)
{
    if (w->count >= HTA_WENT_QUEUE) {
        w->stats.dropped_full++;
        diag(w, "world events: queue full, event dropped", source);
        return false;
    }
    hta_went_event *e = &w->queue[(w->head + w->count) % HTA_WENT_QUEUE];
    e->target = target; e->source = source; e->input = input;
    e->actor = actor; e->depth = depth; e->seq = w->seq++;
    w->count++;
    if (w->count > w->stats.max_queue) w->stats.max_queue = w->count;
    return true;
}

/* Every link of `source` on `event`, in authored order. */
static void emit(hta_world_entities *w, uint32_t source, uint8_t event, uint8_t actor, uint8_t depth)
{
    const hta_wdef *d = &w->defs->entity[source];
    for (uint32_t k = 0; k < d->link_count; k++) {
        uint32_t li = d->first_link + k;
        if (w->defs->link[li].event != event) continue;
        push(w, w->link_target[li], (uint16_t)source, w->defs->link[li].input, actor, depth);
    }
}

bool hta_went_send(hta_world_entities *w, uint32_t target, uint8_t input, uint8_t actor)
{
    if (!w || !w->loaded || w->remote || target >= w->defs->count) return false;
    return push(w, hta_went_handle_of(w, target), (uint16_t)target, input, actor, 0);
}

static float dist3(const float a[3], const float b[3])
{
    float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

int32_t hta_went_can_interact(const hta_world_entities *w, const float eye[3], const float fwd[3])
{
    if (!w || !w->loaded || !eye || !fwd) return -1;
    int32_t best = -1;
    float best_d = 1e9f;
    for (uint32_t i = 0; i < w->defs->count; i++) {
        const hta_wdef *d = &w->defs->entity[i];
        if (d->kind != HTA_WDEF_INTERACTABLE) continue;
        float dist = dist3(eye, d->pos);
        if (!(dist <= d->reach) || dist >= best_d) continue;
        /* In front of the eye: within 60 degrees, unless touching it. */
        if (dist > 0.25f) {
            float dot = ((d->pos[0] - eye[0]) * fwd[0] + (d->pos[1] - eye[1]) * fwd[1] +
                         (d->pos[2] - eye[2]) * fwd[2]) / dist;
            if (dot < 0.5f) continue;
        }
        best = (int32_t)i; best_d = dist;
    }
    return best;
}

int32_t hta_went_interact(hta_world_entities *w, uint8_t actor, const float eye[3], const float fwd[3])
{
    if (!w || w->remote) return -1;
    int32_t i = hta_went_can_interact(w, eye, fwd);
    if (i < 0) return -1;
    /* A press during the cooldown is still consumed: it was aimed here. */
    if (w->st[i].cooldown > 0.0f) return i;
    w->st[i].cooldown = HTA_WENT_COOLDOWN;
    emit(w, (uint32_t)i, HTA_WEV_USED, actor, 1);
    if (w->defs->entity[i].script) {
        if (w->call_count < HTA_WENT_MAX_CALLS) w->calls[w->call_count++] = (hta_went_call){ (uint8_t)i, actor };
        else diag(w, "world events: too many scripted uses this step, one dropped", (uint32_t)i);
    }
    return i;
}

void hta_went_sense(hta_world_entities *w, uint8_t actor, const float feet[3], bool present)
{
    if (!w || !w->loaded || w->remote || actor >= HTA_WENT_MAX_ACTORS) return;
    uint64_t bit = (uint64_t)1 << actor;
    for (uint32_t i = 0; i < w->defs->count; i++) {
        const hta_wdef *d = &w->defs->entity[i];
        if (d->kind != HTA_WDEF_TRIGGER) continue;
        /* The body's middle, a little above its feet. */
        bool in = present && feet && isfinite(feet[0]) && isfinite(feet[1]) && isfinite(feet[2]) &&
                  feet[0] >= d->min[0] && feet[0] <= d->max[0] && feet[1] >= d->min[1] &&
                  feet[1] <= d->max[1] && feet[2] + 0.3f >= d->min[2] && feet[2] + 0.3f <= d->max[2];
        hta_went_state *s = &w->st[i];
        if (in && !(s->inside & bit)) {
            s->inside |= bit;
            emit(w, i, HTA_WEV_ENTERED, actor, 1);
        } else if (!in) s->inside &= ~bit;
    }
}

static void mover_input(hta_world_entities *w, uint32_t i, uint8_t input)
{
    hta_went_state *s = &w->st[i];
    bool open = input == HTA_WIN_OPEN ||
                (input == HTA_WIN_TOGGLE && (s->phase == HTA_MOVER_CLOSED || s->phase == HTA_MOVER_CLOSING));
    uint8_t phase = open ? (s->t >= 1.0f ? HTA_MOVER_OPEN : HTA_MOVER_OPENING)
                         : (s->t <= 0.0f ? HTA_MOVER_CLOSED : HTA_MOVER_CLOSING);
    if (phase != s->phase) { s->phase = phase; w->version++; }
}

static void dispatch(hta_world_entities *w, const hta_went_event *e)
{
    int32_t i = hta_went_resolve(w, e->target);
    if (i < 0) {
        w->stats.dropped_stale++;
        diag(w, "world events: stale target, event dropped", e->source);
        return;
    }
    if (e->depth > HTA_WDEF_MAX_CHAIN) {
        w->stats.dropped_depth++;
        diag(w, "world events: chain too long (a cycle?), event dropped", e->source);
        return;
    }
    const hta_wdef *d = &w->defs->entity[i];
    if (!hta_wdef_accepts(d->kind, e->input)) {
        w->stats.dropped_input++;
        diag(w, "world events: input not accepted, event dropped", (uint32_t)i);
        return;
    }
    w->stats.dispatched++;
    switch (d->kind) {
    case HTA_WDEF_RELAY:
        emit(w, (uint32_t)i, HTA_WEV_FIRED, e->actor, (uint8_t)(e->depth + 1));
        break;
    case HTA_WDEF_MOVER:
        mover_input(w, (uint32_t)i, e->input);
        break;
    case HTA_WDEF_TELEPORT: {
        if (e->actor == HTA_WENT_NO_ACTOR) break;
        /* One teleport per actor per step: the first wins. */
        for (uint32_t k = 0; k < w->teleport_count; k++) if (w->teleports[k].actor == e->actor) return;
        if (w->teleport_count >= HTA_WENT_MAX_TELEPORTS) { diag(w, "world events: too many teleports", (uint32_t)i); break; }
        hta_went_teleport *t = &w->teleports[w->teleport_count++];
        t->actor = e->actor;
        memcpy(t->pos, d->pos, sizeof(t->pos));
        t->yaw = d->yaw;
        break;
    }
    default: break;
    }
}

void hta_went_step(hta_world_entities *w, float dt)
{
    if (!w || !w->loaded) return;
    w->teleport_count = 0;
    if (!w->remote) {
        uint32_t n = 0;
        while (w->count && n < HTA_WENT_BUDGET) {
            hta_went_event e = w->queue[w->head];
            w->head = (w->head + 1) % HTA_WENT_QUEUE;
            w->count--;
            dispatch(w, &e);
            n++;
        }
        if (w->count) w->stats.deferred += w->count;
    }
    if (!(dt > 0.0f) || dt > 1.0f) dt = dt > 1.0f ? 1.0f : 0.0f;
    for (uint32_t i = 0; i < w->defs->count; i++) {
        const hta_wdef *d = &w->defs->entity[i];
        hta_went_state *s = &w->st[i];
        if (s->cooldown > 0.0f) s->cooldown -= dt;
        if (d->kind != HTA_WDEF_MOVER) continue;
        if (s->phase == HTA_MOVER_OPENING || s->phase == HTA_MOVER_CLOSING) {
            const hta_wmover_def *m = mdef(w, i);
            float len = sqrtf(m->move[0] * m->move[0] + m->move[1] * m->move[1] + m->move[2] * m->move[2]);
            float step = m->speed / len * dt;
            if (s->phase == HTA_MOVER_OPENING) {
                s->t += step;
                if (s->t >= 1.0f) { s->t = 1.0f; s->phase = HTA_MOVER_OPEN; w->version++; }
            } else {
                s->t -= step;
                if (s->t <= 0.0f) { s->t = 0.0f; s->phase = HTA_MOVER_CLOSED; w->version++; }
            }
        }
        place(w, i);
    }
}

uint32_t hta_went_instances(const hta_world_entities *w, hta_collision_instance *out, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; w && w->loaded && out && i < w->defs->count && n < cap; i++)
        if (w->defs->entity[i].kind == HTA_WDEF_MOVER) out[n++] = w->inst[i];
    return n;
}

void hta_went_offset(const hta_world_entities *w, uint32_t index, float out[3])
{
    out[0] = out[1] = out[2] = 0.0f;
    if (!w || !w->loaded || index >= w->defs->count) return;
    if (w->defs->entity[index].kind != HTA_WDEF_MOVER) return;
    for (int k = 0; k < 3; k++) out[k] = mdef(w, index)->move[k] * w->st[index].t;
}

uint32_t hta_went_snapshot(const hta_world_entities *w, hta_went_mover_state *out, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; w && w->loaded && out && i < w->defs->count && n < cap; i++) {
        if (w->defs->entity[i].kind != HTA_WDEF_MOVER) continue;
        float t = w->st[i].t < 0.0f ? 0.0f : w->st[i].t > 1.0f ? 1.0f : w->st[i].t;
        out[n].index = (uint8_t)i;
        out[n].phase = w->st[i].phase;
        out[n].t_q = (uint16_t)lrintf(t * 65535.0f);
        n++;
    }
    return n;
}

bool hta_went_apply(hta_world_entities *w, const hta_went_mover_state *m, bool snap)
{
    if (!w || !w->loaded || !m || m->index >= w->defs->count || m->phase > HTA_MOVER_CLOSING ||
        w->defs->entity[m->index].kind != HTA_WDEF_MOVER) return false;
    hta_went_state *s = &w->st[m->index];
    float t = (float)m->t_q / 65535.0f;
    if (s->phase != m->phase) w->version++;
    s->phase = m->phase;
    /* Between snapshots it moves on by itself; the host's word wins when
     * they drift apart, or at rest. */
    if (snap || fabsf(s->t - t) > 0.1f || m->phase == HTA_MOVER_OPEN || m->phase == HTA_MOVER_CLOSED) s->t = t;
    place(w, m->index);
    return true;
}
