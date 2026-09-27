/* The resource contract, printed (resource.h): what megamod-resources
 * --json and --markdown show, and what Open Asset Lab keeps a copy of
 * (assetlab/data/megamod_resources.json). Everything comes from the tables
 * the loader itself uses -- the type registry, the reference fields, the
 * limits in package.h and world_def.h, the world key's member lists -- so
 * the contract cannot say one thing while the loader does another. */
#include "asset_res.h"
#include "external_map.h"
#include "package.h"
#include "prefab.h"
#include "resource.h"
#include "world_def.h"
#include "../engine/world_entities.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct { char *buf; size_t cap, len; } out;

#if defined(__GNUC__)
static void put(out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
static void put(out *o, const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    int w = vsnprintf(o->buf && o->len < o->cap ? o->buf + o->len : NULL, o->buf && o->len < o->cap ? o->cap - o->len : 0, fmt, a);
    va_end(a);
    if (w > 0) o->len += (size_t)w;
}

static const char *scope_name(uint8_t s)
{
    return s == HTA_RS_PACKAGE ? "package" : s == HTA_RS_PLACEMENT ? "placement" : "definition";
}

static void referenced_by(out *o, uint8_t type)
{
    bool first = true;
    for (uint8_t f = 0; f < HTA_REF_FIELD_COUNT; f++) {
        const hta_ref_info *r = hta_ref_get(f);
        bool any = r->expects == HTA_RT_NONE && hta_rtype_get(type)->importable;
        if (r->expects != type && !any) continue;
        put(o, "%s\"%s\"", first ? "" : ", ", r->field);
        first = false;
    }
}

static void kinds_json(out *o, uint32_t bits)
{
    bool first = true;
    put(o, "[");
    for (uint8_t k = 1; k < HTA_WDEF_KIND_COUNT; k++)
        if (bits & (1u << k)) { put(o, "%s\"%s\"", first ? "" : ", ", hta_wdef_kind_name(k)); first = false; }
    put(o, "]");
}

static const char *ARG_NAME[] = { "target", "amount", "sound", "at" };
static void args_json(out *o, uint32_t bits)
{
    bool first = true;
    put(o, "[");
    for (uint32_t k = 0; k < 4; k++)
        if (bits & (1u << k)) { put(o, "%s\"%s\"", first ? "" : ", ", ARG_NAME[k]); first = false; }
    put(o, "]");
}

/* X7: the binding vocabulary, from the tables the parser, the checks and
 * the runtime use (world_def.c). */
static void bindings_json(out *o)
{
    put(o, "  \"bindings\": {\"member\": \"world_entities.bindings\", \"schema\": 6, "
           "\"fields\": [\"actions\", \"conditions\", \"event\", \"id\", \"source\"], "
           "\"id\": \"a local id (prefabs.local_id), unique in its world or prefab\", "
           "\"order\": \"canonical byte order of id, each once\", \"events\": [");
    for (uint8_t e = 1; e < HTA_WEV_COUNT; e++) {
        const hta_wevent_info *i = hta_wevent_get(e);
        put(o, "%s\n    {\"name\": \"%s\", \"sources\": ", e > 1 ? "," : "", i->name);
        kinds_json(o, i->sources);
        put(o, ", \"actor\": \"%s\", \"link_name\": ", i->actor == HTA_WACTOR_ALWAYS ? "always" :
            i->actor == HTA_WACTOR_CHAIN ? "the chain's, if any" : "never");
        if (e < HTA_WEV_LINK_COUNT) put(o, "\"%s\"", hta_wdef_event_name(e)); else put(o, "null");
        put(o, ", \"when\": \"%s\"}", i->when);
    }
    put(o, "],\n   \"conditions\": [");
    for (uint8_t c = 1; c < HTA_WCOND_COUNT; c++) {
        const hta_wcond_info *i = hta_wcond_get(c);
        put(o, "%s\n    {\"name\": \"%s\", \"fields\": [\"condition\", \"entity\", \"is\"], \"entity\": ", c > 1 ? "," : "", i->name);
        kinds_json(o, i->kinds);
        put(o, ", \"values\": [");
        for (uint8_t v = 0; v < i->value_count; v++) put(o, "%s\"%s\"", v ? ", " : "", i->values[v]);
        put(o, "], \"doc\": \"%s\"}", i->doc);
    }
    put(o, "],\n   \"actions\": [");
    for (uint8_t a = 1; a < HTA_WACT_COUNT; a++) {
        const hta_waction_info *i = hta_waction_get(a);
        put(o, "%s\n    {\"name\": \"%s\", \"targets\": ", a > 1 ? "," : "", i->name);
        kinds_json(o, i->targets);
        put(o, ", \"needs\": ");
        args_json(o, i->needs);
        put(o, ", \"takes\": ");
        args_json(o, i->takes);
        put(o, ", \"subject\": \"%s\", \"path\": \"%s\"}", i->needs_actor ? "the event's actor" : i->targets ? "the target" : "the place",
            i->path);
    }
    put(o, "],\n   \"arguments\": {\"target\": \"a placed entity (world) or local child id (prefab); its kind must afford the action\", "
           "\"amount\": \"damage, (0, %g]\", \"sound\": \"a sound resource (X5), imported\", "
           "\"at\": \"a placed entity (or child) with a position; default the source\"}, ", (double)HTA_WDEF_MAX_DAMAGE);
    put(o, "\"limits\": {\"bindings\": %u, \"conditions_per_binding\": %u, \"actions_per_binding\": %u, \"conditions\": %u, "
           "\"actions\": %u, \"bindings_per_source_event\": %u, \"chain_depth\": %u, \"cascade_actions\": %u, \"queue\": %u, "
           "\"dispatches_per_step\": %u, \"damage_per_step\": %u, \"sounds_per_step\": %u, \"entities\": %u, \"damage_max\": %g}, ",
        HTA_WDEF_MAX_BINDINGS, HTA_WDEF_MAX_CONDS_PER, HTA_WDEF_MAX_ACTIONS_PER, HTA_WDEF_MAX_CONDS, HTA_WDEF_MAX_ACTIONS,
        HTA_WDEF_MAX_BINDINGS_PER_EVENT, HTA_WDEF_MAX_CHAIN, HTA_WENT_CASCADE_BUDGET, HTA_WENT_QUEUE, HTA_WENT_BUDGET,
        HTA_WENT_MAX_HURTS, HTA_WENT_MAX_CUES, HTA_WDEF_MAX_ENTITIES, (double)HTA_WDEF_MAX_DAMAGE);
    /* Each kind's capabilities, derived from the tables above: what a
     * requester -- a player, Lua, a binding, later an agent -- may ask of it,
     * what it reports, what can be read of it. */
    put(o, "\"affordances\": {");
    for (uint8_t k = 1; k < HTA_WDEF_KIND_COUNT; k++) {
        bool f = true;
        put(o, "%s\"%s\": {\"actions\": [", k > 1 ? ", " : "", hta_wdef_kind_name(k));
        for (uint8_t a = 1; a < HTA_WACT_COUNT; a++)
            if (hta_wdef_affords(k, a)) { put(o, "%s\"%s\"", f ? "" : ", ", hta_waction_get(a)->name); f = false; }
        put(o, "], \"events\": [");
        f = true;
        for (uint8_t e = 1; e < HTA_WEV_COUNT; e++)
            if (hta_wbind_emits(k, e)) { put(o, "%s\"%s\"", f ? "" : ", ", hta_wevent_get(e)->name); f = false; }
        put(o, "], \"state\": [");
        f = true;
        for (uint8_t c = 1; c < HTA_WCOND_COUNT; c++)
            if (hta_wcond_get(c)->kinds & (1u << k)) { put(o, "%s\"%s\"", f ? "" : ", ", hta_wcond_get(c)->name); f = false; }
        put(o, "], \"positioned\": %s}", hta_wdef_positioned(k) ? "true" : "false");
    }
    put(o, "}, \"seam\": \"every requester (a player's press after its reach test, Lua's world.send, a binding, a future "
           "agent) asks hta_went_request for an action on a target for an actor; the target's kind must afford it; the bounded "
           "queue dispatches it into the one engine path; the result is queued, rejected, refused here (a joiner) or queue full\", ");
    put(o, "\"semantics\": {\"conditions\": \"all must hold; read when the event is delivered, the same state for every binding "
           "of that event; read-only\", \"ordering\": \"per event: its links in authored order, then its bindings in canonical order "
           "(the world's own by id, then each prefab instance's by instance id and binding id); a binding's actions in authored order; "
           "all queued, dispatched first in first out\", \"cycles\": \"allowed (conditions may break them); every cascade is bounded by "
           "chain_depth and cascade_actions, then dropped with a diagnostic naming the binding\", \"authority\": \"host (or offline) "
           "only; a joiner never evaluates a binding: it receives the resulting state (WORLD_STATE, WORLD, kills) and a binding's sound "
           "as a world-sound effect\", \"late_join\": \"state, never event history\", "
           "\"lua\": \"a scripted interactable's on_used runs in the script phase, after its links' and bindings' actions were "
           "queued and before they dispatch; its requests queue after them\"}, ");
    put(o, "\"phase\": [\"host.interact: use presses -> used (links, bindings queued; scripted uses recorded)\", "
           "\"game.update\", \"round restart (relays inactive, movers closed, the queue and cascades emptied)\", "
           "\"host.world.sense: trigger entries -> entered\", \"host.world.script: Lua on_used, on_ability (requests queued)\", "
           "\"host.world.dispatch: the queue, first in first out, %u per step; a relay's activated/deactivated and a mover told to "
           "open at the end of its travel emit here and queue behind\", \"world.movers: movers move; arriving -> opened/closed, "
           "queued for the next step's dispatch\", \"host.apply: teleports, damage (hta_game_hurt), sounds (and the world-sound "
           "effect to joiners)\", \"network: snapshots and WORLD_STATE out\"]},\n", HTA_WENT_BUDGET);
}

size_t hta_resource_contract_json(char *buf, size_t cap)
{
    out o = { buf, cap, 0 };
    if (buf && cap) buf[0] = 0;
    put(&o, "{\n  \"contract\": \"megamod.resources\",\n  \"version\": 1,\n");
    put(&o, "  \"id_grammar\": {\"version\": %u, \"form\": \"namespace:type/name\", \"segment\": \"[a-z][a-z0-9_]*\", "
            "\"limits\": {\"namespace\": %u, \"type\": %u, \"name\": %u, \"total\": %u}, "
            "\"case\": \"lowercase ASCII only; compared byte for byte; never folded\", "
            "\"normalization\": \"none: any other spelling is refused, never rewritten\", \"reserved_namespaces\": [",
        HTA_RID_GRAMMAR, HTA_RID_NS_MAX, HTA_RID_TYPE_MAX, HTA_RID_NAME_MAX, HTA_RID_MAX);
    for (size_t i = 0; i < HTA_RID_RESERVED_NS_COUNT; i++) put(&o, "%s\"%s\"", i ? ", " : "", HTA_RID_RESERVED_NS[i]);
    put(&o, "]},\n");
    put(&o, "  \"package_id_grammar\": {\"form\": \"segment(.segment)*\", \"segment\": \"[a-z][a-z0-9_]*\", "
            "\"max_bytes\": %u, \"max_segments\": %u},\n", HTA_PKG_ID_MAX, HTA_PKG_SEG_MAX);
    put(&o, "  \"types\": [\n");
    for (uint8_t t = 1; t < HTA_RT_COUNT; t++) {
        const hta_rtype_info *i = hta_rtype_get(t);
        put(&o, "    {\"name\": \"%s\", \"noun\": \"%s\", \"status\": \"%s\", \"scope\": \"%s\", \"runtime_resolved\": %s, "
                "\"importable\": %s, \"listed_in_provides\": %s, \"referenced_by\": [",
            i->name, i->noun, i->status == HTA_RT_SUPPORTED ? "supported" : "reserved", scope_name(i->scope),
            i->runtime ? "true" : "false", i->importable ? "true" : "false",
            i->scope != HTA_RS_PLACEMENT && i->status == HTA_RT_SUPPORTED ? "true" : "false");
        referenced_by(&o, t);
        put(&o, "], \"since\": \"%s\", \"doc\": \"%s\"}%s\n", i->since, i->doc, t + 1 < HTA_RT_COUNT ? "," : "");
    }
    put(&o, "  ],\n  \"references\": [\n");
    for (uint8_t f = 0; f < HTA_REF_FIELD_COUNT; f++) {
        const hta_ref_info *r = hta_ref_get(f);
        put(&o, "    {\"field\": \"%s\", \"label\": \"%s\", \"expects\": ", r->field, r->label);
        if (r->expects) put(&o, "\"%s\"", hta_rtype_get(r->expects)->name);
        else put(&o, "null");
        put(&o, ", \"resolves\": \"%s\", \"since\": \"%s\", \"doc\": \"%s\"}%s\n",
            r->from == HTA_REF_SELF ? "same package" : r->from == HTA_REF_NAMED ? "the required package it is listed under" :
            "same package, or an import from a required package",
            r->since, r->doc, f + 1 < HTA_REF_FIELD_COUNT ? "," : "");
    }
    put(&o, "  ],\n");
    put(&o, "  \"package\": {\"member\": \"package\", \"schema\": %u, \"kinds\": [\"world\", \"library\"], "
            "\"fields\": {\"id\": \"package ID\", \"schema\": %u, "
            "\"provides\": \"every resource the package defines except placements; canonical byte order; each once; must equal the content\", "
            "\"requires\": \"[{package, resources}] in canonical package-ID order; resources: the imports, canonical order, each provided by that package\"}, "
            "\"implicit\": \"a manifest with no package member (every pre-X4 package): provides what it defines, requires nothing\", "
            "\"library\": {\"container\": \"OALASSET v1, kind library: the manifest, then the payload of its asset members (X5), nothing else\", "
            "\"location\": \"%s/<package id>.oalasset\", "
            "\"provides\": \"its scripts and its assets, exactly\"}, "
            "\"graph\": \"package requirements are acyclic (a cycle is refused with its path); each package loads once; "
            "the set is kept sorted by package ID; references may only reach the package itself or its declared imports\", "
            "\"limits\": {\"provides\": %u, \"requires\": %u, \"imports\": %u, \"packages_per_set\": %u, \"depth\": %u, "
            "\"resources_per_set\": %u}},\n",
        HTA_PKG_SCHEMA, HTA_PKG_SCHEMA, HTA_PKG_LIBRARY_DIR, HTA_PKG_MAX_PROVIDES, HTA_PKG_MAX_REQUIRES,
        HTA_PKG_MAX_IMPORTS, HTA_PKG_MAX_SET, HTA_PKG_MAX_DEPTH, HTA_RES_MAX);
    put(&o, "  \"world_key\": {\"schema\": %u, \"played_members\": [", HTA_WORLD_KEY_SCHEMA);
    for (uint32_t i = 0; hta_world_key_played(i); i++) put(&o, "%s\"%s\"", i ? ", " : "", hta_world_key_played(i));
    put(&o, "], \"library_members\": [");
    for (uint32_t i = 0; hta_library_key_played(i); i++) put(&o, "%s\"%s\"", i ? ", " : "", hta_library_key_played(i));
    put(&o, "], \"dependencies\": \"after the members: per package of the closure, sorted by package ID: "
            "OALD, u32 ID length, ID, u64 digest (FNV-1a 64 of OALL, u32 schema, its library members; then, when it "
            "declares assets, OALP, u32 payload length and every payload byte)\"},\n");
    /* X5: package-backed asset resources (asset_res.h). */
    put(&o, "  \"assets\": {\"member\": \"assets\", \"schema\": %u, \"in\": \"library packages\", "
            "\"fields\": [\"materials\", \"members\", \"models\", \"schema\", \"sounds\", \"textures\"], "
            "\"members\": {\"entry\": {\"path\": \"member path\", \"size\": \"bytes, at least 1\"}, "
            "\"order\": \"canonical byte order of path, each once; the payload is the members' bytes in this order, right after the manifest\", "
            "\"use\": \"every member backs exactly one resource; every resource's member exists\"}, "
            "\"member_path\": {\"form\": \"segment(/segment)*, the last with one extension: name.ext\", \"segment\": \"[a-z0-9_]+\", "
            "\"max_bytes\": %u, \"max_segments\": %u, \"identity\": \"none: storage inside the package; never a host path, never a resource ID\"}, "
            "\"types\": {"
            "\"texture\": {\"fields\": [\"format\", \"height\", \"id\", \"member\", \"width\"], \"formats\": [\"rgba8\"], \"max_side\": %u, "
            "\"payload\": \"width x height x 4 bytes, rows top down\"}, "
            "\"material\": {\"fields\": [\"draw\", \"id\", \"texture\"], \"draw\": [\"opaque\", \"alpha\"], \"references\": [\"assets.materials[].texture\"]}, "
            "\"model\": {\"fields\": [\"format\", \"id\", \"materials\", \"member\"], \"formats\": [\"mesh1\"], \"max_slots\": %u, "
            "\"max_vertices\": %u, \"max_indices\": %u, \"max_groups\": %u, \"references\": [\"assets.models[].materials[]\"], "
            "\"payload\": \"MSH1, u32 vertex, index, group counts; vertices of 10 f32 (position, normal, uv, lightmap uv; |v| <= 4096); "
            "u32 indices; groups of u32 first, count, material slot, contiguous from 0\"}, "
            "\"sound\": {\"fields\": [\"channels\", \"format\", \"frames\", \"id\", \"member\", \"rate\"], \"formats\": [\"pcm_s16le\"], "
            "\"rate\": [4000, 96000], \"channels\": [1, 2], \"max_frames\": %u, \"payload\": \"frames x channels x 2 bytes, interleaved, little endian\"}}, "
            "\"limits\": {\"per_type\": %u, \"members\": %u, \"payload_bytes\": %u}, "
            "\"lists\": \"canonical byte order of id, each once; every descriptor field required, nothing else allowed\", "
            "\"runtime\": \"decoded once when the package set loads into one table (by package ID, then resource ID, per type); "
            "placements hold indices; a resource imported by several consumers or placed many times exists once\"},\n",
        HTA_ASSET_SCHEMA, HTA_ASSET_MEMBER_MAX, HTA_ASSET_MEMBER_SEGS, HTA_ASSET_TEX_MAX, HTA_ASSET_MAX_SLOTS,
        HTA_ASSET_MESH_MAX_VERTS, HTA_ASSET_MESH_MAX_INDICES, HTA_ASSET_MESH_MAX_GROUPS, HTA_ASSET_SOUND_MAX_FRAMES,
        HTA_ASSET_MAX_PER_TYPE, HTA_ASSET_MAX_MEMBERS, HTA_ASSET_MAX_PAYLOAD);
    /* X6: prefabs (prefab.h), and how a world places them. */
    put(&o, "  \"prefabs\": {\"member\": \"prefabs\", \"schema\": %u, \"schemas\": {\"1\": \"X6: children\", "
            "\"2\": \"X7: adds a prefab's bindings; a schema 1 member is read exactly as before and may not hold them\"}, "
            "\"in\": \"library packages\", \"fields\": [\"prefabs\", \"schema\"], "
            "\"prefab_fields\": [\"bindings\", \"children\", \"id\"], \"bindings\": {\"schema\": 2, \"form\": \"as "
            "world_entities.bindings, but source, conditions[].entity, actions[].target and actions[].at name the prefab's children by "
            "local id; a play_sound's sound resolves from the prefab's own package\", \"order\": \"canonical byte order of id, each once "
            "in the prefab\", \"expansion\": \"per instance, after its children: each local child becomes that instance's own entity\", "
            "\"limits\": {\"bindings\": %u, \"conditions\": %u, \"actions\": %u}}, ",
        HTA_PREFAB_SCHEMA, HTA_PREFAB_MAX_BINDINGS, HTA_PREFAB_MAX_BIND_CONDS, HTA_PREFAB_MAX_BIND_ACTIONS);
    put(&o, "\"children\": {\"kinds\": [");
    for (uint8_t k = 1; k < HTA_WDEF_KIND_COUNT; k++) put(&o, "%s\"%s\"", k > 1 ? ", " : "", hta_wdef_kind_name(k));
    put(&o, "], \"common\": [\"id\", \"kind\", \"links\"], \"fields\": {"
            "\"interactable\": {\"takes\": [\"position\", \"reach\", \"script\"], \"needs\": [\"position\", \"reach\"]}, "
            "\"relay\": {\"takes\": [], \"needs\": []}, "
            "\"mover\": {\"takes\": [\"position\", \"size\", \"move\", \"speed\", \"yaw_degrees\", \"sound\", \"model\"], \"needs\": [\"position\", \"size\", \"move\", \"speed\"]}, "
            "\"trigger\": {\"takes\": [\"bounds\"], \"needs\": [\"bounds\"]}, "
            "\"teleport\": {\"takes\": [\"position\", \"yaw_degrees\"], \"needs\": [\"position\"]}, "
            "\"prop\": {\"takes\": [\"position\", \"model\", \"yaw_degrees\"], \"needs\": [\"position\", \"model\"]}}, "
            "\"space\": \"prefab space: +z up, the instance's origin at 0; the world's own parameter meanings\", "
            "\"mover\": \"size, move and speed inline (it becomes its own mover definition when expanded); an optional model draws it, "
            "its origin at the box's centre\", "
            "\"links\": \"{event, input, target}: target is a sibling's local id; the source must emit the event and the target accept the "
            "input; no self-link, no cycle, chains of at most %u\"}, ", HTA_WDEF_MAX_CHAIN);
    put(&o, "\"local_id\": {\"form\": \"[a-z][a-z0-9]*(_[a-z0-9]+)*\", \"max_bytes\": %u, "
            "\"applies_to\": [\"a prefab child's id\", \"a world's prefab instance id\"], "
            "\"identity\": \"local: never a resource ID; unique within its prefab (children) or world (instances)\"}, "
            "\"child_entity\": {\"form\": \"<world namespace>:entity/<instance>__<child>\", \"separator\": \"%s\", "
            "\"why\": \"neither part may hold __, so the split is unique; a schema 5 world's own placed IDs may not hold __ either\"}, ",
        HTA_PREFAB_LOCAL_MAX, HTA_PREFAB_SEP);
    put(&o, "\"order\": \"prefabs in canonical byte order of id, children in canonical byte order of local id, each once; a world's "
            "instances in canonical byte order of id; expansion appends, after the world's own entities, each instance's children in "
            "that order\", "
            "\"references\": \"a child's model, sound and script resolve from the prefab's own package (its own or its imports); a "
            "world imports only the prefab\", "
            "\"nesting\": {\"supported\": false, \"max_depth\": 0, \"refusal\": \"a child of kind prefab, or naming a prefab, is refused\"}, "
            "\"inheritance\": false, \"overrides\": \"none: an instance is the prefab, an instance id and a transform\", ");
    put(&o, "\"transform\": {\"position\": \"[x, y, z] wu\", \"yaw_degrees\": \"about +z, counter-clockwise seen from above (+x turns "
            "toward +y); |yaw| <= %g; exact at multiples of 90\", \"scale\": {\"uniform\": true, \"min\": %g, \"max\": %g, \"on\": "
            "\"instances and props\"}, \"composition\": \"child world pos = I.pos + I.scale * Rz(I.yaw) * c.pos; yaw = I.yaw + c.yaw; "
            "scale = I.scale; a mover's move turns with the instance, its box with its own yaw too; reach, size, move and speed scale\", "
            "\"triggers\": \"axis-aligned boxes: an instance holding a trigger must have a yaw that is a multiple of 90\", "
            "\"collision\": \"a prop and a prefab mover collide as their oriented box (the drawn model's bounds, turned and scaled)\"}, ",
        (double)HTA_PREFAB_MAX_YAW, (double)HTA_PREFAB_SCALE_MIN, (double)HTA_PREFAB_SCALE_MAX);
    put(&o, "\"limits\": {\"per_library\": %u, \"children\": %u, \"links\": %u, \"links_per_child\": %u, \"instances_per_world\": %u, "
            "\"expanded_entities\": %u, \"expanded_mover_definitions\": %u, \"local_position\": %g, \"yaw_degrees\": %g}, "
            "\"runtime\": \"expanded once when the world loads into ordinary placed entities (the same arrays, handles, links, "
            "collision, replication, scripts); nothing during play knows a prefab\"},\n",
        HTA_PREFAB_MAX_PER_LIBRARY, HTA_PREFAB_MAX_CHILDREN, HTA_PREFAB_MAX_LINKS, HTA_PREFAB_MAX_LINKS_PER, HTA_PREFAB_MAX_INSTANCES,
        HTA_WDEF_MAX_ENTITIES, HTA_WDEF_MAX_MOVER_DEFS, (double)HTA_PREFAB_MAX_LOCAL, (double)HTA_PREFAB_MAX_YAW);
    put(&o, "  \"world_entities\": {\"schema\": %u, \"kinds\": [", HTA_WDEF_SCHEMA);
    for (uint8_t k = 1; k < HTA_WDEF_KIND_COUNT; k++) put(&o, "%s\"%s\"", k > 1 ? ", " : "", hta_wdef_kind_name(k));
    put(&o, "], \"prop\": \"schema 4: {id, kind prop, links [], model, position}: draws its model where it stands, solid as the "
            "model's bounds\", \"mover_sound\": \"schema 4: a mover definition's optional sound, played when a mover starts to open or close\", "
            "\"prop_transform\": \"schema 5: a prop's optional yaw_degrees and scale (see prefabs.transform)\", "
            "\"prefab_instances\": {\"schema\": 5, \"fields\": [\"id\", \"position\", \"prefab\", \"scale\", \"yaw_degrees\"], "
            "\"needs\": [\"id\", \"position\", \"prefab\"], \"defaults\": {\"scale\": 1, \"yaw_degrees\": 0}, "
            "\"order\": \"canonical byte order of id, each once\", \"reserved\": \"a schema 5 world's own placed IDs may not hold __\"}, "
            "\"bindings\": {\"schema\": 6, \"see\": \"bindings\"}},\n");
    bindings_json(&o);
    put(&o, "  \"scripts\": {\"api\": \"%s\", \"max_scripts\": %u, \"max_source_bytes\": %u, \"max_pool_bytes\": %u}\n}\n",
        HTA_WDEF_SCRIPT_API, HTA_WDEF_MAX_SCRIPTS, HTA_WDEF_SCRIPT_MAX_BYTES, HTA_WDEF_SCRIPT_POOL);
    return o.len;
}

size_t hta_resource_types_markdown(char *buf, size_t cap)
{
    out o = { buf, cap, 0 };
    if (buf && cap) buf[0] = 0;
    put(&o, "| Type | Noun | Status | Scope | Resolved at load | Importable | Since | What |\n");
    put(&o, "|---|---|---|---|---|---|---|---|\n");
    for (uint8_t t = 1; t < HTA_RT_COUNT; t++) {
        const hta_rtype_info *i = hta_rtype_get(t);
        put(&o, "| `%s` | %s | %s | %s | %s | %s | %s | %s |\n", i->name, i->noun,
            i->status == HTA_RT_SUPPORTED ? "supported" : "reserved", scope_name(i->scope), i->runtime ? "yes" : "-",
            i->importable ? "yes" : "-", i->since, i->doc);
    }
    put(&o, "\n| Reference field | Expects | Resolves to | Since |\n|---|---|---|---|\n");
    for (uint8_t f = 0; f < HTA_REF_FIELD_COUNT; f++) {
        const hta_ref_info *r = hta_ref_get(f);
        put(&o, "| `%s` | %s | %s | %s |\n", r->field, r->expects ? hta_rtype_get(r->expects)->noun : "any importable type",
            r->from == HTA_REF_SELF ? "the same package" : r->from == HTA_REF_NAMED ? "the required package it is listed under" :
            "the same package or a declared import", r->since);
    }
    /* X7: the binding vocabulary. */
    put(&o, "\n| Event | Sources | Actor | X1 link name | When |\n|---|---|---|---|---|\n");
    for (uint8_t e = 1; e < HTA_WEV_COUNT; e++) {
        const hta_wevent_info *i = hta_wevent_get(e);
        char kl[80] = "";
        size_t at = 0;
        for (uint8_t k = 1; k < HTA_WDEF_KIND_COUNT; k++)
            if (i->sources & (1u << k)) at += (size_t)snprintf(kl + at, sizeof(kl) - at, "%s%s", at ? ", " : "", hta_wdef_kind_name(k));
        put(&o, "| `%s` | %s | %s | %s | %s |\n", i->name, kl, i->actor == HTA_WACTOR_ALWAYS ? "always" :
            i->actor == HTA_WACTOR_CHAIN ? "the chain's" : "-", e < HTA_WEV_LINK_COUNT ? hta_wdef_event_name(e) : "-", i->when);
    }
    put(&o, "\n| Condition | Reads | Values |\n|---|---|---|\n");
    for (uint8_t c = 1; c < HTA_WCOND_COUNT; c++) {
        const hta_wcond_info *i = hta_wcond_get(c);
        char vl[80] = "";
        size_t at = 0;
        for (uint8_t v = 0; v < i->value_count; v++) at += (size_t)snprintf(vl + at, sizeof(vl) - at, "%s`%s`", v ? ", " : "", i->values[v]);
        put(&o, "| `%s` | %s | %s |\n", i->name, (i->kinds & (1u << HTA_WDEF_MOVER)) ? "mover" : "relay", vl);
    }
    put(&o, "\n| Action | Targets | Needs | Takes | Acts on |\n|---|---|---|---|---|\n");
    for (uint8_t a = 1; a < HTA_WACT_COUNT; a++) {
        const hta_waction_info *i = hta_waction_get(a);
        char tl[80] = "", nl[48] = "", al[48] = "";
        size_t at = 0;
        for (uint8_t k = 1; k < HTA_WDEF_KIND_COUNT; k++)
            if (i->targets & (1u << k)) at += (size_t)snprintf(tl + at, sizeof(tl) - at, "%s%s", at ? ", " : "", hta_wdef_kind_name(k));
        at = 0;
        for (uint32_t k = 0; k < 4; k++) if (i->needs & (1u << k)) at += (size_t)snprintf(nl + at, sizeof(nl) - at, "%s%s", at ? ", " : "", ARG_NAME[k]);
        at = 0;
        for (uint32_t k = 0; k < 4; k++) if (i->takes & (1u << k)) at += (size_t)snprintf(al + at, sizeof(al) - at, "%s%s", at ? ", " : "", ARG_NAME[k]);
        put(&o, "| `%s` | %s | %s | %s | %s |\n", i->name, tl[0] ? tl : "-", nl, al,
            i->needs_actor ? "the event's actor" : i->targets ? "the target" : "a place");
    }
    return o.len;
}

/* The conformance corpus: resource and package IDs with the engine's own
 * verdict on each, printed live. Open Asset Lab keeps a copy and checks
 * its grammar gives the same verdict and the same words for every one
 * (assetlab/data/megamod_id_conformance.json); scripts/test_x4.sh checks
 * the copy is current. Add a case here when a grammar question comes up. */
static const char *const RID_CASES[] = {
    "x3:script/button_logic", "x1:entity/door_main", "x2:mover/basic_slide_door", "x4shared:script/pulse_ability",
    "lab_demo:world/relay_test", "owner:character/goku", "a:sounds/b", "a1_:script/b__2_", "showdown:script/door_controller",
    "n234567890123456789012345678901234567890:script/m23456789012345678901234567890123456789012345678",
    "n2345678901234567890123456789012345678901:script/a", "a:script/m234567890123456789012345678901234567890123456789",
    "n234567890123456789012345678901234567890:script/m234567890123456789012345678901234567890123456789",
    "", "script/door", "x4:script", ":script/door", "x4:/door", "x4:script/", "X4:script/door", "x4:Script/door",
    "x4:script/Door", "x4:script/door-logic", "x4:script/door.lua", "x4:script/door logic", " x4:script/door",
    "x4:script/door ", "x4:script/door\t", "x4:script//door", "x4::script/door", "x4:script/a/b", "x4:script/../door",
    "x4:script:x/door", "4x:script/door", "_x:script/door", "x4:script/_door", "x4:script/9mm", "x4:script/d\xc3\xb6r",
    "x4:widget/door", "x4:scripts/door", "x4:Entity/a", "showdown:model/red_crate", "common:material/industrial_metal",
    "x:texture/t", "x:sound/s", "community:animation/rifle_run", "showdown:prefab/security_door",
    "showdown:ruleset/team_deathmatch", "megamod:script/x", "x4:script/a%s", "x4:script/a\\b", "x4:script/a\"b",
    "x5shared:model/test_crate", "x5shared:material/test_crate", "x5shared:texture/test_crate", "x5shared:sound/test_impact",
    "x5shared:model/models/test_crate", "x5shared:model/test_crate.mesh", "x5shared:Model/test_crate", "x5shared:sounds/impact",
    "x6:prefab/security_door", "x6:prefab/Security_door", "x6:prefabs/security_door", "x6:prefab/security_door/button",
    "x6:entity/north_door__button", "megamod:prefab/door",
};
static const char *const PKG_CASES[] = {
    "x4.resource_lab", "x4.shared", "showdown", "a.b.c.d.e.f.g.h", "common.gameplay_scripts2", "", "x4:resource_lab",
    "x4/lab", "X4.lab", "x4..lab", ".x4", "x4.", "showdown-industrial-pack", "a.b.c.d.e.f.g.h.i", "../etc", "x4.Lab",
    "x4.1lab", "x4.lab ", "pppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppp",
    "ppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppp",
};

/* Member paths (X5, asset_res.h): package-local storage, checked by the
 * same rules on both sides. */
static const char *const PATH_CASES[] = {
    "models/test_crate.mesh", "textures/test_crate.rgba", "sounds/test_impact.pcm", "a.b", "a/b/c/d/e/f.x",
    "a/b/c/d/e/f/g.x", "", "/models/a.mesh", "../a.mesh", "models/../a.mesh", "models/./a.mesh", "models//a.mesh",
    "models\\a.mesh", "Models/a.mesh", "models/A.mesh", "models/a", "models/a.", "models/.mesh", "models/a.b.c",
    "models.x/a.mesh", "models/a-b.mesh", "models/a b.mesh", "models/a.mesh/", "C:/a.mesh", "models/a\xc3\xa9.mesh",
    "models/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.mesh",
    "models/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.mesh", "models/a:b.mesh", "%2e%2e/a.mesh",
};

/* Local IDs (X6, prefab.h): a prefab child's id and a world's instance id. */
static const char *const LOCAL_CASES[] = {
    "button", "door", "north_door", "frame_left", "a", "a1", "a_1", "door2_panel", "a2345678901234567890123",
    "a23456789012345678901234", "", "Button", "1door", "_door", "door_", "door__panel", "a___b", "door-panel", "door.panel",
    "door panel", "door/panel", "door:panel", "x6:entity/door", "d\xc3\xb6r", "north_door__button",
};

static void jstr(out *o, const char *s)
{
    put(o, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') put(o, "\\%c", c);
        else if (c < 0x20 || c >= 0x7F) put(o, "\\u%04x", c);
        else put(o, "%c", c);
    }
    put(o, "\"");
}

/* A case as written above: C escapes \t, \xNN, \\, \" decoded. */
static void unescape(const char *in, char *outb, size_t cap)
{
    size_t n = 0;
    for (; *in && n + 1 < cap; in++) {
        if (*in == '\\' && in[1] == 't') { outb[n++] = '\t'; in++; }
        else if (*in == '\\' && in[1] == 'x' && in[2] && in[3]) {
            unsigned v = 0;
            for (int k = 2; k < 4; k++) v = v * 16 + (unsigned)(in[k] <= '9' ? in[k] - '0' : in[k] - 'a' + 10);
            outb[n++] = (char)v; in += 3;
        } else if (*in == '\\' && (in[1] == '\\' || in[1] == '"')) { outb[n++] = in[1]; in++; }
        else outb[n++] = *in;
    }
    outb[n] = 0;
}

size_t hta_resource_conformance_json(char *buf, size_t cap)
{
    static const char *const CODE[] = { "ok", "malformed", "unknown_type", "reserved_type" };
    out o = { buf, cap, 0 };
    if (buf && cap) buf[0] = 0;
    put(&o, "{\n  \"grammar\": %u,\n  \"resource_ids\": [\n", HTA_RID_GRAMMAR);
    for (size_t i = 0; i < sizeof(RID_CASES) / sizeof(RID_CASES[0]); i++) {
        char id[256], why[160];
        unescape(RID_CASES[i], id, sizeof(id));
        hta_rid r;
        int rc = hta_rid_parse(id, &r, why, sizeof(why));
        put(&o, "    {\"id\": ");
        jstr(&o, id);
        put(&o, ", \"result\": \"%s\", \"type\": ", CODE[rc]);
        if (r.type) put(&o, "\"%s\"", hta_rtype_get(r.type)->name); else put(&o, "null");
        put(&o, ", \"reserved_namespace\": %s, \"why\": ", rc == HTA_RID_OK && hta_rid_reserved_namespace(id, r.ns_len) ? "true" : "false");
        jstr(&o, why);
        put(&o, "}%s\n", i + 1 < sizeof(RID_CASES) / sizeof(RID_CASES[0]) ? "," : "");
    }
    put(&o, "  ],\n  \"package_ids\": [\n");
    for (size_t i = 0; i < sizeof(PKG_CASES) / sizeof(PKG_CASES[0]); i++) {
        char why[160];
        bool ok = hta_package_id_valid(PKG_CASES[i], why, sizeof(why));
        put(&o, "    {\"id\": ");
        jstr(&o, PKG_CASES[i]);
        put(&o, ", \"valid\": %s, \"why\": ", ok ? "true" : "false");
        jstr(&o, why);
        put(&o, "}%s\n", i + 1 < sizeof(PKG_CASES) / sizeof(PKG_CASES[0]) ? "," : "");
    }
    put(&o, "  ],\n  \"member_paths\": [\n");
    for (size_t i = 0; i < sizeof(PATH_CASES) / sizeof(PATH_CASES[0]); i++) {
        char p[256], why[160];
        unescape(PATH_CASES[i], p, sizeof(p));
        bool ok = hta_asset_member_valid(p, why, sizeof(why));
        put(&o, "    {\"path\": ");
        jstr(&o, p);
        put(&o, ", \"valid\": %s, \"why\": ", ok ? "true" : "false");
        jstr(&o, why);
        put(&o, "}%s\n", i + 1 < sizeof(PATH_CASES) / sizeof(PATH_CASES[0]) ? "," : "");
    }
    put(&o, "  ],\n  \"local_ids\": [\n");
    for (size_t i = 0; i < sizeof(LOCAL_CASES) / sizeof(LOCAL_CASES[0]); i++) {
        char p[256], why[160];
        unescape(LOCAL_CASES[i], p, sizeof(p));
        bool ok = hta_prefab_local_valid(p, "local id", why, sizeof(why));
        put(&o, "    {\"id\": ");
        jstr(&o, p);
        put(&o, ", \"valid\": %s, \"why\": ", ok ? "true" : "false");
        jstr(&o, why);
        put(&o, "}%s\n", i + 1 < sizeof(LOCAL_CASES) / sizeof(LOCAL_CASES[0]) ? "," : "");
    }
    put(&o, "  ]\n}\n");
    return o.len;
}
