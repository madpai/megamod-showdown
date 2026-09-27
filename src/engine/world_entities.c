/* World entities at runtime (world_entities.h). */
#include "world_entities.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* X7: one trace line (only when tracing; bounded per step). */
#if defined(__GNUC__)
static void trace(hta_world_entities *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
static void trace(hta_world_entities *w, const char *fmt, ...)
{
    if (!w->trace) return;
    if (w->trace_count >= HTA_WENT_TRACE) { w->trace_dropped++; return; }
    va_list a;
    va_start(a, fmt);
    vsnprintf(w->trace_line[w->trace_count++], sizeof(w->trace_line[0]), fmt, a);
    va_end(a);
}

static const char *eid(const hta_world_entities *w, uint32_t i) { return i < w->defs->count ? w->defs->entity[i].id : "?"; }

/* "binding open_door" or "x7:prefab/security_door binding toggle_door (north_door)". */
static const char *bname(const hta_world_entities *w, uint32_t b, char *buf, size_t n)
{
    const hta_wbinding *x = &w->defs->binding[b];
    if (x->instance && x->instance <= w->defs->prefab_instance_count)
        snprintf(buf, n, "%s binding %s (%s)", w->defs->prefab_instance[x->instance - 1].prefab, x->id,
                 w->defs->prefab_instance[x->instance - 1].id);
    else snprintf(buf, n, "binding %s", x->id);
    return buf;
}

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
    /* X7: an action's queue target -- what it is requested of, or, for a
     * damage or a sound, the entity it happens at -- as a handle, so a
     * round reset stales it like a link's. */
    for (uint32_t b = 0; b < w->defs->binding_count; b++) {
        const hta_wbinding *x = &w->defs->binding[b];
        for (uint32_t k = 0; k < x->action_count; k++) {
            const hta_waction *a = &w->defs->action[x->first_action + k];
            uint32_t t = a->target != HTA_WDEF_NO_DEF ? a->target : a->at != HTA_WDEF_NO_DEF ? a->at : x->source;
            w->act_target[x->first_action + k] = hta_went_handle_of(w, t);
        }
    }
}

/* X7: bindings indexed by (source, event), each run in canonical binding
 * order -- a counting sort, stable -- so an event looks at only its own. */
static void index_bindings(hta_world_entities *w)
{
    const hta_world_defs *d = w->defs;
    memset(w->bind_n, 0, sizeof(w->bind_n));
    for (uint32_t b = 0; b < d->binding_count; b++) w->bind_n[d->binding[b].source][d->binding[b].event]++;
    uint16_t at = 0;
    for (uint32_t i = 0; i < HTA_WDEF_MAX_ENTITIES; i++)
        for (uint32_t e = 0; e < HTA_WEV_COUNT; e++) { w->bind_first[i][e] = at; at = (uint16_t)(at + w->bind_n[i][e]); }
    uint8_t fill[HTA_WDEF_MAX_ENTITIES][HTA_WEV_COUNT];
    memset(fill, 0, sizeof(fill));
    for (uint32_t b = 0; b < d->binding_count; b++) {
        const hta_wbinding *x = &d->binding[b];
        w->bind_list[w->bind_first[x->source][x->event] + fill[x->source][x->event]++] = (uint16_t)b;
    }
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

/* X6: a placement's rotation about +z, row-major local -> world (the
 * collision instance's convention); identity without a transform. */
static void set_rot(hta_collision_instance *in, const hta_wdef *d)
{
    memset(in->rot, 0, sizeof(in->rot));
    in->rot[0] = in->rot[4] = in->rot[8] = 1.0f;
    if (!d->xform) return;
    in->rot[0] = d->rot_c; in->rot[1] = -d->rot_s;
    in->rot[3] = d->rot_s; in->rot[4] = d->rot_c;
}

/* A box grid of half extents `h`, in its own space around the centre. */
static bool box_build(hta_bsp_mesh *m, hta_vertex v[8], uint32_t idx[36], hta_collision *coll, const float h[3])
{
    memset(v, 0, 8 * sizeof(hta_vertex));
    for (int c = 0; c < 8; c++)
        for (int k = 0; k < 3; k++) v[c].pos[k] = (c >> k) & 1 ? h[k] : -h[k];
    static const uint32_t ix[36] = {
        0,2,3, 0,3,1,  4,5,7, 4,7,6,  0,1,5, 0,5,4,
        2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
    };
    memcpy(idx, ix, sizeof(ix));
    memset(m, 0, sizeof(*m));
    m->vertices = v; m->vertex_count = 8;
    m->indices = idx; m->index_count = 36;
    for (int k = 0; k < 3; k++) { m->bounds_min[k] = -h[k]; m->bounds_max[k] = h[k]; }
    return hta_collision_build(coll, m);
}

/* Definition `di`'s box grid. */
static bool def_build(hta_world_entities *w, uint32_t di)
{
    const hta_wmover_def *md = &w->defs->mover_def[di];
    float h[3];
    for (int k = 0; k < 3; k++) h[k] = md->size[k] * 0.5f;
    return box_build(&w->mover_mesh[di], w->mover_verts[di], w->mover_idx[di], &w->mover_coll[di], h);
}

/* X5: prop `i` is solid as its model's bounds where it stands (a thin
 * model gets 1 cm so the grid has a volume). Built once; it never moves. */
static bool prop_build(hta_world_entities *w, uint32_t i)
{
    const hta_wdef *d = &w->defs->entity[i];
    float h[3], c[3];
    for (int k = 0; k < 3; k++) {
        /* X6: a transformed prop is its oriented box, not the world-axis
         * box around it -- collision is what is drawn. */
        h[k] = d->xform ? d->box_h[k] : (d->max[k] - d->min[k]) * 0.5f;
        if (h[k] < 0.005f) h[k] = 0.005f;
        c[k] = d->xform ? d->box_c[k] : (d->max[k] + d->min[k]) * 0.5f;
    }
    if (!box_build(&w->prop_mesh[i], w->prop_verts[i], w->prop_idx[i], &w->prop_coll[i], h)) return false;
    hta_collision_instance *in = &w->inst[i];
    memset(in, 0, sizeof(*in));
    set_rot(in, d);
    in->grid = &w->prop_coll[i];
    memcpy(in->pos, c, sizeof(c));
    in->radius = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]) + 0.05f;
    in->active = true;
    return true;
}

/* Placed mover `i`: its own instance of its definition's grid. */
static void mover_place_init(hta_world_entities *w, uint32_t i)
{
    const hta_wmover_def *md = mdef(w, i);
    hta_collision_instance *in = &w->inst[i];
    memset(in, 0, sizeof(*in));
    set_rot(in, &w->defs->entity[i]);      /* X6: a prefab's mover turns with it */
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
    for (uint32_t i = 0; i < defs->count; i++) {
        if (defs->entity[i].kind == HTA_WDEF_MOVER) mover_place_init(w, i);
        if (defs->entity[i].kind == HTA_WDEF_PROP && !prop_build(w, i)) {
            if (err && errlen) snprintf(err, errlen, "%s: prop collision failed", defs->entity[i].id);
            hta_went_free(w);
            return false;
        }
    }
    resolve_links(w);
    index_bindings(w);
    w->casc_cur = HTA_WENT_NO_CASCADE;
    w->loaded = true;
    w->version++;
    return true;
}

void hta_went_free(hta_world_entities *w)
{
    if (!w) return;
    /* Every slot: `defs` may already be gone (a world unloaded first). */
    for (uint32_t i = 0; i < HTA_WDEF_MAX_MOVER_DEFS; i++) hta_collision_free(&w->mover_coll[i]);
    for (uint32_t i = 0; i < HTA_WDEF_MAX_ENTITIES; i++) hta_collision_free(&w->prop_coll[i]);
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
    w->cue_count = 0;
    w->hurt_count = 0;
    memset(w->cascade, 0, sizeof(w->cascade));
    w->casc_cur = HTA_WENT_NO_CASCADE;
    w->version++;
}

/* X7: the cascade an event pushed now belongs to -- the one being
 * dispatched, or, outside dispatch (a use, a trigger, a script's request,
 * a mover arriving), a new root's, opened the first time it queues. Never
 * full: more slots than the queue can hold events. */
static uint16_t cascade_of(hta_world_entities *w, uint16_t *root, uint16_t source, uint8_t event)
{
    if (w->casc_cur != HTA_WENT_NO_CASCADE) return w->casc_cur;
    if (*root != HTA_WENT_NO_CASCADE) return *root;
    for (uint32_t k = 0; k < HTA_WENT_CASCADES; k++) {
        uint16_t c = (uint16_t)((w->casc_next + k) % HTA_WENT_CASCADES);
        if (w->cascade[c].live) continue;
        w->cascade[c] = (hta_went_cascade){ 0, 0, source, event, 0 };
        w->casc_next = (uint16_t)((c + 1) % HTA_WENT_CASCADES);
        return *root = c;
    }
    return HTA_WENT_NO_CASCADE;
}

static bool push(hta_world_entities *w, hta_went_handle target, uint16_t source, uint8_t input,
                 uint8_t actor, uint8_t depth, uint16_t cascade, uint16_t binding, uint16_t action)
{
    if (w->count >= HTA_WENT_QUEUE) {
        w->stats.dropped_full++;
        diag(w, "world events: queue full, event dropped", source);
        return false;
    }
    hta_went_event *e = &w->queue[(w->head + w->count) % HTA_WENT_QUEUE];
    e->target = target; e->source = source; e->input = input;
    e->actor = actor; e->depth = depth; e->seq = w->seq++;
    e->binding = binding; e->action = action; e->cascade = cascade;
    if (cascade != HTA_WENT_NO_CASCADE) w->cascade[cascade].live++;
    w->count++;
    if (w->count > w->stats.max_queue) w->stats.max_queue = w->count;
    return true;
}

/* X7: a condition, read now. Read-only. */
static bool cond_holds(const hta_world_entities *w, const hta_wcond *c)
{
    const hta_went_state *s = &w->st[c->entity];
    switch (c->kind) {
    case HTA_WCOND_MOVER_STATE: return s->phase == c->value;
    case HTA_WCOND_RELAY_STATE: return (s->active ? HTA_WRELAY_ACTIVE : HTA_WRELAY_INACTIVE) == c->value;
    default: return false;
    }
}

/* An event of `source`: every LINK on it, in authored order (X1), then
 * (X7) every BINDING on it, in canonical order -- each binding's
 * conditions read now, against the same state for all of them, and when
 * all hold its actions queued in authored order. Nothing runs here: the
 * queue dispatches. */
static void emit(hta_world_entities *w, uint32_t source, uint8_t event, uint8_t actor, uint8_t depth)
{
    const hta_wdef *d = &w->defs->entity[source];
    uint16_t root = HTA_WENT_NO_CASCADE;
    for (uint32_t k = 0; k < d->link_count; k++) {
        uint32_t li = d->first_link + k;
        if (w->defs->link[li].event != event) continue;
        push(w, w->link_target[li], (uint16_t)source, w->defs->link[li].input, actor, depth,
             cascade_of(w, &root, (uint16_t)source, event), 0, 0);
    }
    uint32_t nb = w->bind_n[source][event];
    if (!nb) return;
    const hta_wevent_info *ev = hta_wevent_get(event);
    if (actor != HTA_WENT_NO_ACTOR) trace(w, "step %u: event %s %s by unit %u (depth %u)", w->step_count, eid(w, source), ev->name, actor, depth);
    else trace(w, "step %u: event %s %s (depth %u)", w->step_count, eid(w, source), ev->name, depth);
    for (uint32_t k = 0; k < nb; k++) {
        uint16_t b = w->bind_list[w->bind_first[source][event] + k];
        const hta_wbinding *x = &w->defs->binding[b];
        char who[HTA_WDEF_ID_MAX + 80];
        bool ok = true;
        for (uint32_t c = 0; c < x->cond_count; c++) {
            const hta_wcond *cd = &w->defs->cond[x->first_cond + c];
            bool h = cond_holds(w, cd);
            if (w->trace) {
                const hta_wcond_info *ci = hta_wcond_get(cd->kind);
                trace(w, "  %s: %s %s is %s -> %s", bname(w, b, who, sizeof(who)), ci->name, eid(w, cd->entity),
                      ci->values[cd->value], h ? "true" : "false");
            }
            if (!h) { ok = false; break; }
        }
        if (!ok) { w->stats.bindings_skipped++; continue; }
        w->stats.bindings_matched++;
        for (uint32_t a = 0; a < x->action_count; a++) {
            uint32_t ai = x->first_action + a;
            const hta_waction *act = &w->defs->action[ai];
            uint16_t c = cascade_of(w, &root, (uint16_t)source, event);
            if (c != HTA_WENT_NO_CASCADE && w->cascade[c].ops >= HTA_WENT_CASCADE_BUDGET) {
                w->stats.dropped_budget++;
                if (!w->cascade[c].over) {
                    w->cascade[c].over = 1;
                    char m[sizeof(w->diag)];
                    snprintf(m, sizeof(m), "world events: the cascade from %s %s exceeded %u binding actions (a loop?); "
                             "%s and the rest of it dropped", eid(w, w->cascade[c].source),
                             hta_wevent_get(w->cascade[c].event) ? hta_wevent_get(w->cascade[c].event)->name : "?",
                             HTA_WENT_CASCADE_BUDGET, bname(w, b, who, sizeof(who)));
                    memcpy(w->diag, m, sizeof(m));
                    w->diag_count++;
                }
                trace(w, "  %s: action %s dropped (cascade budget)", bname(w, b, who, sizeof(who)), hta_waction_get(act->op)->name);
                continue;
            }
            if (c != HTA_WENT_NO_CASCADE) w->cascade[c].ops++;
            if (push(w, w->act_target[ai], (uint16_t)source, act->input, actor, depth, c, (uint16_t)(b + 1), (uint16_t)ai)) {
                w->stats.actions_queued++;
                if (w->trace) {
                    uint32_t t = act->target != HTA_WDEF_NO_DEF ? act->target : act->at != HTA_WDEF_NO_DEF ? act->at : source;
                    trace(w, "  %s: action %s %s queued", bname(w, b, who, sizeof(who)), hta_waction_get(act->op)->name,
                          act->op == HTA_WACT_DAMAGE ? "(the actor)" : eid(w, t));
                }
            }
        }
    }
}

bool hta_went_send(hta_world_entities *w, uint32_t target, uint8_t input, uint8_t actor)
{
    if (!w || !w->loaded || w->remote || target >= w->defs->count) return false;
    uint16_t root = HTA_WENT_NO_CASCADE;
    return push(w, hta_went_handle_of(w, target), (uint16_t)target, input, actor, 0,
                cascade_of(w, &root, (uint16_t)target, HTA_WEV_NONE), 0, 0);
}

uint8_t hta_went_relay_active(const hta_world_entities *w, uint32_t i)
{
    return w && w->loaded && i < w->defs->count && w->defs->entity[i].kind == HTA_WDEF_RELAY && w->st[i].active
        ? HTA_WRELAY_ACTIVE : HTA_WRELAY_INACTIVE;
}

bool hta_went_position(const hta_world_entities *w, uint32_t i, float out[3])
{
    if (!w || !w->loaded || i >= w->defs->count) return false;
    const hta_wdef *d = &w->defs->entity[i];
    switch (d->kind) {
    case HTA_WDEF_MOVER: memcpy(out, w->inst[i].pos, 3 * sizeof(float)); return true;
    case HTA_WDEF_TRIGGER: for (int k = 0; k < 3; k++) out[k] = (d->min[k] + d->max[k]) * 0.5f; return true;
    case HTA_WDEF_PROP: if (d->xform) { memcpy(out, d->box_c, 3 * sizeof(float)); return true; } /* fall through */
    case HTA_WDEF_INTERACTABLE: case HTA_WDEF_TELEPORT: memcpy(out, d->pos, 3 * sizeof(float)); return true;
    default: return false;
    }
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
    hta_went_use(w, (uint32_t)i, actor, 1);
    return i;
}

bool hta_went_use(hta_world_entities *w, uint32_t i, uint8_t actor, uint8_t depth)
{
    if (!w || !w->loaded || w->remote || i >= w->defs->count || w->defs->entity[i].kind != HTA_WDEF_INTERACTABLE) return false;
    if (w->st[i].cooldown > 0.0f) return false;
    w->st[i].cooldown = HTA_WENT_COOLDOWN;
    emit(w, i, HTA_WEV_USED, actor, depth);
    if (w->defs->entity[i].script) {
        if (w->call_count < HTA_WENT_MAX_CALLS) w->calls[w->call_count++] = (hta_went_call){ (uint8_t)i, actor };
        else diag(w, "world events: too many scripted uses this step, one dropped", i);
    }
    return true;
}

hta_went_result hta_went_request(hta_world_entities *w, uint8_t op, uint32_t target, uint8_t actor)
{
    if (!w || !w->loaded || target >= w->defs->count) return HTA_WENT_REJECTED;
    if (w->remote) return HTA_WENT_REFUSED_HERE;
    const hta_waction_info *a = hta_waction_get(op);
    if (!a || !a->targets || !hta_wdef_affords(w->defs->entity[target].kind, op)) return HTA_WENT_REJECTED;
    return hta_went_send(w, target, a->input, actor) ? HTA_WENT_QUEUED : HTA_WENT_QUEUE_FULL;
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

/* X7: a mover's phase changed to `phase` on the host: arriving at open or
 * closed is its `opened` / `closed` event (a joiner only follows). */
static void mover_arrived(hta_world_entities *w, uint32_t i, uint8_t phase, uint8_t actor, uint8_t depth)
{
    if (w->remote) return;
    if (phase == HTA_MOVER_OPEN) emit(w, i, HTA_WEV_OPENED, actor, depth);
    else if (phase == HTA_MOVER_CLOSED) emit(w, i, HTA_WEV_CLOSED, actor, depth);
}

static void mover_input(hta_world_entities *w, uint32_t i, uint8_t input, uint8_t depth)
{
    hta_went_state *s = &w->st[i];
    bool open = input == HTA_WIN_OPEN ||
                (input == HTA_WIN_TOGGLE && (s->phase == HTA_MOVER_CLOSED || s->phase == HTA_MOVER_CLOSING));
    uint8_t phase = open ? (s->t >= 1.0f ? HTA_MOVER_OPEN : HTA_MOVER_OPENING)
                         : (s->t <= 0.0f ? HTA_MOVER_CLOSED : HTA_MOVER_CLOSING);
    if (phase != s->phase) {
        s->phase = phase; w->version++;
        /* Already at the end of its travel: it arrives now (no actor: an
         * arrival is the mover's, whoever asked). */
        mover_arrived(w, i, phase, HTA_WENT_NO_ACTOR, (uint8_t)(depth + 1));
    }
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
        if (e->binding) {
            char m[sizeof(w->diag)], who[HTA_WDEF_ID_MAX + 80];
            snprintf(m, sizeof(m), "world events: chain too long (a cycle?) at %s, event dropped", bname(w, e->binding - 1u, who, sizeof(who)));
            memcpy(w->diag, m, sizeof(m));
            w->diag_count++;
        } else diag(w, "world events: chain too long (a cycle?), event dropped", e->source);
        return;
    }
    const hta_wdef *d = &w->defs->entity[i];
    if (w->trace) {
        char who[HTA_WDEF_ID_MAX + 80];
        trace(w, "  dispatch %s -> %s (%s)", hta_wdef_input_name(e->input), eid(w, (uint32_t)i),
              e->binding ? bname(w, e->binding - 1u, who, sizeof(who)) : e->depth ? "a link" : "a direct request (Lua)");
    }
    /* X7: an action's damage or sound happens at the entity it names; the
     * game (damage) and the mixer (sound) do the rest after the step. */
    if (e->input == HTA_WIN_DAMAGE || e->input == HTA_WIN_SOUND) {
        const hta_waction *a = e->binding && e->action < w->defs->action_count ? &w->defs->action[e->action] : NULL;
        if (!a) { w->stats.dropped_input++; diag(w, "world events: input not accepted, event dropped", (uint32_t)i); return; }
        w->stats.dispatched++;
        if (e->input == HTA_WIN_DAMAGE) {
            if (e->actor == HTA_WENT_NO_ACTOR) { w->stats.no_actor++; trace(w, "  damage: no actor, nothing to hurt"); return; }
            if (w->hurt_count >= HTA_WENT_MAX_HURTS) {
                w->stats.dropped_hurts++;
                diag(w, "world events: too many damage actions this step, one dropped", (uint32_t)i);
                return;
            }
            w->hurts[w->hurt_count++] = (hta_went_hurt){ e->actor, a->amount, e->source, e->binding };
            trace(w, "  damage %g to unit %u", (double)a->amount, e->actor);
            return;
        }
        float pos[3];
        if (!hta_went_position(w, (uint32_t)i, pos)) return;
        if (w->cue_count >= HTA_WENT_MAX_CUES) { diag(w, "world sounds: too many at once, one dropped", (uint32_t)i); return; }
        hta_went_cue *c = &w->cues[w->cue_count++];
        c->entity = (uint8_t)i;
        c->sound = (uint16_t)(a->sound - 1u);
        memcpy(c->pos, pos, sizeof(pos));
        c->binding = e->binding;
        trace(w, "  sound at %s", eid(w, (uint32_t)i));
        return;
    }
    if (!hta_wdef_accepts(d->kind, e->input)) {
        w->stats.dropped_input++;
        diag(w, "world events: input not accepted, event dropped", (uint32_t)i);
        return;
    }
    w->stats.dispatched++;
    switch (d->kind) {
    case HTA_WDEF_RELAY:
        /* X7: a relay remembers the last it was told (conditions read it);
         * `activate` fires it every time, as in X1. */
        if (e->input == HTA_WIN_DEACTIVATE) {
            w->st[i].active = 0;
            emit(w, (uint32_t)i, HTA_WEV_DEACTIVATED, e->actor, (uint8_t)(e->depth + 1));
        } else {
            w->st[i].active = 1;
            emit(w, (uint32_t)i, HTA_WEV_FIRED, e->actor, (uint8_t)(e->depth + 1));
        }
        break;
    case HTA_WDEF_MOVER:
        mover_input(w, (uint32_t)i, e->input, e->depth);
        break;
    case HTA_WDEF_INTERACTABLE:
        /* X7 `use`: the same path a press takes past its reach test. */
        if (!hta_went_use(w, (uint32_t)i, e->actor, (uint8_t)(e->depth + 1))) trace(w, "  use: cooling down, nothing happens");
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
    w->hurt_count = 0;
    w->cue_count = 0;
    w->step_count++;
    if (!w->remote) {
        uint32_t n = 0;
        while (w->count && n < HTA_WENT_BUDGET) {
            hta_went_event e = w->queue[w->head];
            w->head = (w->head + 1) % HTA_WENT_QUEUE;
            w->count--;
            /* What it causes belongs to its root's cascade (X7). */
            w->casc_cur = e.cascade;
            dispatch(w, &e);
            w->casc_cur = HTA_WENT_NO_CASCADE;
            if (e.cascade != HTA_WENT_NO_CASCADE && w->cascade[e.cascade].live) w->cascade[e.cascade].live--;
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
                if (s->t >= 1.0f) { s->t = 1.0f; s->phase = HTA_MOVER_OPEN; w->version++; mover_arrived(w, i, s->phase, HTA_WENT_NO_ACTOR, 1); }
            } else {
                s->t -= step;
                if (s->t <= 0.0f) { s->t = 0.0f; s->phase = HTA_MOVER_CLOSED; w->version++; mover_arrived(w, i, s->phase, HTA_WENT_NO_ACTOR, 1); }
            }
        }
        place(w, i);
    }
    /* X5: a mover that started to open or close since the last step sounds
     * once -- the same on the host and on a joiner following it. (X7: the
     * cues were emptied at the start of the step; a binding's play_sound
     * may already be there.) */
    for (uint32_t i = 0; i < w->defs->count; i++) {
        hta_went_state *s = &w->st[i];
        if (w->defs->entity[i].kind != HTA_WDEF_MOVER || s->heard == s->phase) continue;
        s->heard = s->phase;
        uint16_t sound = mdef(w, i)->sound;
        if (!sound || (s->phase != HTA_MOVER_OPENING && s->phase != HTA_MOVER_CLOSING)) continue;
        if (w->cue_count >= HTA_WENT_MAX_CUES) { diag(w, "world sounds: too many at once, one dropped", i); continue; }
        hta_went_cue *c = &w->cues[w->cue_count++];
        c->entity = (uint8_t)i;
        c->sound = (uint16_t)(sound - 1u);
        c->binding = 0;
        for (int k = 0; k < 3; k++) c->pos[k] = w->inst[i].pos[k];
    }
}

uint32_t hta_went_instances(const hta_world_entities *w, hta_collision_instance *out, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; w && w->loaded && out && i < w->defs->count && n < cap; i++)
        if (w->defs->entity[i].kind == HTA_WDEF_MOVER || w->defs->entity[i].kind == HTA_WDEF_PROP) out[n++] = w->inst[i];
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
    if (snap) s->heard = s->phase;          /* the world as we found it: no sound */
    place(w, m->index);
    return true;
}
