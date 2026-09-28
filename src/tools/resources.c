/* megamod-resources: the resource identity and dependency contract (X4,
 * docs/RESOURCES.md), and a validator for real packages.
 *
 *   megamod-resources [--json]         the contract: ID grammar, resource
 *                                      types, reference fields, package
 *                                      schema, limits, world key members
 *   megamod-resources --markdown       the type and reference tables
 *                                      docs/RESOURCES.md carries
 *   megamod-resources --conformance    the grammar corpus with this
 *                                      engine's verdicts (Open Asset Lab
 *                                      must give the same)
 *   megamod-resources --bundle DIR --world NAME
 *                                      load maps/NAME.oalmap from DIR with
 *                                      every package it requires, exactly as
 *                                      a match does; print what it provides,
 *                                      what it needs, what every reference
 *                                      resolved to (X5: asset references too,
 *                                      with their provider and table index),
 *                                      its asset table and its world key (JSON);
 *                                      X8: its world state -- runtime, spatial,
 *                                      logical and host-only counts, snapshot
 *                                      bytes, and every object's runtime index
 *                                      and replication channel and index.
 *                                      Exit 1 with the loader's message when
 *                                      the world is refused.
 *
 * Everything printed comes from the tables and code the loader uses. */
#include "app/content.h"
#include "app/fs.h"
#include "asset/external_map.h"
#include "asset/resource.h"
#include "asset/world_repl.h"
#include "gfx/gfx.h"
#include "net/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *me)
{
    fprintf(stderr, "usage: %s [--json | --markdown | --conformance | --bundle DIR --world NAME]\n", me);
}

static void json_str(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') printf("\\%c", c);
        else if (c < 0x20 || c >= 0x7F) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}

static int inspect(const char *bundle, const char *world)
{
    for (const char *c = world; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) {
            fprintf(stderr, "bad world name\n");
            return 2;
        }
    hta_fs fs;
    hta_fs_init(&fs);
    hta_fs_mount_content(&fs, NULL, bundle);
    char name[160], err[600] = "";
    snprintf(name, sizeof(name), "maps/%s.oalmap", world);
    hta_external_map *m = calloc(1, sizeof(*m));
    if (!m) return 1;
    if (!hta_content_load_world(&fs, name, m, err, sizeof(err))) {
        printf("{\"world\": "); json_str(world); printf(", \"ok\": false, \"error\": "); json_str(err); printf("}\n");
        free(m);
        return 1;
    }
    const hta_external_package *pk = &m->package;
    const hta_world_defs *d = &m->world_defs;
    printf("{\"world\": "); json_str(world);
    printf(", \"ok\": true, \"oalmap_version\": %u, \"world_key\": \"%08x\", \"world_digest\": \"%016llx\",\n",
           m->version, m->key, (unsigned long long)m->digest);
    printf(" \"package\": {\"declared\": %s, \"id\": ", pk->declared ? "true" : "false");
    json_str(pk->id);
    printf(", \"provides\": %u, \"requires\": %u, \"imports\": %u, \"set\": [", pk->provides, pk->requires, pk->imports);
    for (uint32_t i = 0; i < pk->dep_count; i++) {
        printf("%s{\"package\": ", i ? ", " : "");
        json_str(pk->dep[i]);
        printf(", \"direct\": %s, \"digest\": \"%016llx\"}", pk->dep_direct[i] ? "true" : "false",
               (unsigned long long)pk->dep_digest[i]);
    }
    printf("]},\n \"scripts\": [");
    for (uint32_t i = 0; i < d->script_count; i++) {
        printf("%s{\"id\": ", i ? ", " : "");
        json_str(d->script[i].id);
        printf(", \"index\": %u, \"from\": ", i);
        json_str(d->script[i].provider ? pk->dep[d->script[i].provider - 1] : (pk->declared ? pk->id : "(this world)"));
        printf(", \"bytes\": %u}", d->script[i].len);
    }
    /* X6: every prefab instance, what it placed, and what each child became. */
    printf("],\n \"prefab_instances\": [");
    for (uint32_t i = 0; i < d->prefab_instance_count; i++) {
        const hta_wprefab_instance *in = &d->prefab_instance[i];
        printf("%s\n  {\"instance\": ", i ? "," : ""); json_str(in->id);
        printf(", \"prefab\": "); json_str(in->prefab);
        printf(", \"provider\": "); json_str(in->provider && in->provider <= pk->dep_count ? pk->dep[in->provider - 1] : "?");
        printf(", \"position\": [%.3f, %.3f, %.3f], \"yaw_degrees\": %g, \"scale\": %g, \"children\": [",
               in->pos[0], in->pos[1], in->pos[2], (double)in->yaw_deg, (double)in->scale);
        for (uint32_t c = 0; c < in->count; c++) {
            uint32_t at = in->first + c;
            const hta_wdef *e = &d->entity[at];
            char path[64];
            snprintf(path, sizeof(path), "%s/%s", in->id, strstr(e->id, "__") ? strstr(e->id, "__") + 2 : "?");
            printf("%s{\"path\": ", c ? ", " : ""); json_str(path);
            printf(", \"entity\": "); json_str(e->id);
            printf(", \"kind\": \"%s\", \"index\": %u, \"position\": [%.3f, %.3f, %.3f]", hta_wdef_kind_name(e->kind), at,
                   e->pos[0], e->pos[1], e->pos[2]);
            if (e->kind == HTA_WDEF_MOVER) printf(", \"mover_definition\": %u", e->def);
            printf("}");
        }
        printf("]}");
    }
    /* X7: every event binding as compiled -- source, event, conditions and
     * actions by placed ID and entity index, where it came from. */
    printf("],\n \"bindings\": [");
    for (uint32_t i = 0; i < d->binding_count; i++) {
        const hta_wbinding *b = &d->binding[i];
        printf("%s\n  {\"index\": %u, \"id\": ", i ? "," : "", i); json_str(b->id);
        printf(", \"origin\": ");
        if (b->instance) {
            const hta_wprefab_instance *in = &d->prefab_instance[b->instance - 1];
            printf("{\"prefab\": "); json_str(in->prefab); printf(", \"instance\": "); json_str(in->id); printf("}");
        } else printf("\"world\"");
        printf(", \"source\": "); json_str(d->entity[b->source].id);
        printf(", \"source_index\": %u, \"event\": \"%s\", \"conditions\": [", b->source, hta_wevent_get(b->event)->name);
        for (uint32_t k = 0; k < b->cond_count; k++) {
            const hta_wcond *c = &d->cond[b->first_cond + k];
            const hta_wcond_info *ci = hta_wcond_get(c->kind);
            printf("%s{\"condition\": \"%s\", \"entity\": ", k ? ", " : "", ci->name); json_str(d->entity[c->entity].id);
            printf(", \"index\": %u, \"is\": \"%s\"}", c->entity, ci->values[c->value]);
        }
        printf("], \"actions\": [");
        for (uint32_t k = 0; k < b->action_count; k++) {
            const hta_waction *x = &d->action[b->first_action + k];
            printf("%s{\"action\": \"%s\"", k ? ", " : "", hta_waction_get(x->op)->name);
            if (x->target != HTA_WDEF_NO_DEF) { printf(", \"target\": "); json_str(d->entity[x->target].id); printf(", \"index\": %u", x->target); }
            if (x->op == HTA_WACT_DAMAGE) printf(", \"amount\": %g", (double)x->amount);
            if (x->sound && x->sound <= m->assets.sound_count) {
                printf(", \"sound\": "); json_str(m->assets.sound[x->sound - 1].id);
                printf(", \"sound_index\": %u", x->sound - 1u);
                uint16_t at = x->at != HTA_WDEF_NO_DEF ? x->at : b->source;
                printf(", \"at\": "); json_str(d->entity[at].id);
            }
            printf("}");
        }
        printf("]}");
    }
    printf("],\n \"resolved\": [");
    bool first = true;
#define SEP() (first ? (first = false, "") : ",\n   ")
    for (uint32_t i = 0; i < d->count; i++) {
        const hta_wdef *e = &d->entity[i];
        for (uint32_t k = 0; k < e->link_count; k++) {
            const hta_wdef_link *l = &d->link[e->first_link + k];
            printf("%s{\"from\": ", SEP()); json_str(e->id);
            printf(", \"field\": \"%s\", \"to\": ", hta_ref_get(HTA_REF_LINK_TARGET)->field);
            json_str(d->entity[l->target].id);
            printf(", \"index\": %u}", l->target);
        }
        const hta_wmover_def *md = hta_wdef_mover(d, i);
        if (md && md->id[0]) {
            printf("%s{\"from\": ", SEP()); json_str(e->id);
            printf(", \"field\": \"%s\", \"to\": ", hta_ref_get(HTA_REF_MOVER_DEF)->field);
            json_str(md->id);
            printf(", \"index\": %u}", e->def);
        }
        if (e->script) {
            printf("%s{\"from\": ", SEP()); json_str(e->id);
            printf(", \"field\": \"%s\", \"to\": ", hta_ref_get(e->instance ? HTA_REF_PREFAB_SCRIPT : HTA_REF_SCRIPT)->field);
            json_str(d->script[e->script - 1].id);
            printf(", \"index\": %u}", e->script - 1u);
        }
    }
    for (uint32_t i = 0; i < d->prefab_instance_count; i++) {
        const hta_wprefab_instance *in = &d->prefab_instance[i];
        printf("%s{\"from\": ", SEP()); json_str(in->id);
        printf(", \"field\": \"%s\", \"to\": ", hta_ref_get(HTA_REF_PREFAB_INSTANCE)->field);
        json_str(in->prefab);
        printf(", \"provider\": "); json_str(in->provider && in->provider <= pk->dep_count ? pk->dep[in->provider - 1] : "?");
        printf(", \"entities\": [%u, %u]}", in->first, in->first + in->count);
    }
    if (d->ability_script) {
        printf("%s{\"from\": \"ability_script\", \"field\": \"%s\", \"to\": ", SEP(), hta_ref_get(HTA_REF_ABILITY_SCRIPT)->field);
        json_str(d->script[d->ability_script - 1].id);
        printf(", \"index\": %u}", d->ability_script - 1u);
    }
    /* X5: typed asset references, to the table's index and its provider. */
    const hta_asset_table *a = &m->assets;
#define FROM(p) ((p) && (p) <= pk->dep_count ? pk->dep[(p) - 1] : "?")
    for (uint32_t i = 0; i < d->count; i++) {
        const hta_wdef *e = &d->entity[i];
        if ((e->kind != HTA_WDEF_PROP && e->kind != HTA_WDEF_MOVER) || !e->model || e->model > a->model_count) continue;
        printf("%s{\"from\": ", SEP()); json_str(e->id);
        printf(", \"field\": \"%s\", \"to\": ", hta_ref_get(e->instance ? HTA_REF_PREFAB_MODEL : HTA_REF_PROP_MODEL)->field);
        json_str(a->model[e->model - 1].id);
        printf(", \"provider\": "); json_str(FROM(a->model[e->model - 1].provider));
        printf(", \"index\": %u}", e->model - 1u);
    }
    for (uint32_t i = 0; i < d->mover_def_count; i++) {
        const hta_wmover_def *md = &d->mover_def[i];
        if (!md->sound || md->sound > a->sound_count) continue;
        const char *owner = md->id;
        for (uint32_t k = 0; md->generated && k < d->count; k++)
            if (d->entity[k].kind == HTA_WDEF_MOVER && d->entity[k].def == i) owner = d->entity[k].id;
        printf("%s{\"from\": ", SEP()); json_str(owner);
        printf(", \"field\": \"%s\", \"to\": ", hta_ref_get(md->generated ? HTA_REF_PREFAB_SOUND : HTA_REF_MOVER_SOUND)->field);
        json_str(a->sound[md->sound - 1].id);
        printf(", \"provider\": "); json_str(FROM(a->sound[md->sound - 1].provider));
        printf(", \"index\": %u}", md->sound - 1u);
    }
    for (uint32_t i = 0; i < a->material_count; i++) {
        printf("%s{\"from\": ", SEP()); json_str(a->material[i].id);
        printf(", \"field\": \"%s\", \"to\": ", hta_ref_get(HTA_REF_MATERIAL_TEXTURE)->field);
        json_str(a->texture[a->material[i].texture].id);
        printf(", \"provider\": "); json_str(FROM(a->texture[a->material[i].texture].provider));
        printf(", \"index\": %u}", a->material[i].texture);
    }
    for (uint32_t i = 0; i < a->model_count; i++)
        for (uint32_t k = 0; k < a->model[i].slot_count; k++) {
            uint16_t mi = a->model[i].slot[k];
            printf("%s{\"from\": ", SEP()); json_str(a->model[i].id);
            printf(", \"field\": \"%s\", \"slot\": %u, \"to\": ", hta_ref_get(HTA_REF_MODEL_MATERIAL)->field, k);
            json_str(a->material[mi].id);
            printf(", \"provider\": "); json_str(FROM(a->material[mi].provider));
            printf(", \"index\": %u}", mi);
        }
    /* X8: what this world costs on the wire, and every object's identities. */
    {
        static hta_wrep_map rep;
        char rerr[160] = "";
        bool ok = hta_wrep_build(&rep, d, rerr, sizeof(rerr));
        uint32_t drawn = 0;
        for (uint32_t i = 0; i < d->count; i++)
            drawn += d->entity[i].kind == HTA_WDEF_MOVER || (d->entity[i].kind == HTA_WDEF_PROP && d->entity[i].model);
        uint32_t rest = hta_net_world_state_bytes(rep.spatial_count, 0, rep.flag_count);
        uint32_t busy = hta_net_world_state_bytes(rep.spatial_count, rep.spatial_count, rep.flag_count);
        printf("],\n \"world_state\": {\"protocol\": %u, \"format\": %u, \"ok\": %s, \"runtime_objects\": %u, \"spatial\": %u, "
               "\"logical\": %u, \"host_only\": %u, \"snapshot_bytes\": {\"at_rest\": %u, \"all_moving\": %u, \"packet_at_rest\": %u}, "
               "\"gpu_instances\": %u, \"limits\": {\"runtime_objects\": %u, \"spatial\": %u, \"logical\": %u, \"gpu_instances\": %u}",
               HTA_NET_VERSION, HTA_NET_WSTATE_FORMAT, ok ? "true" : "false", d->count, rep.spatial_count, rep.flag_count,
               d->count - rep.spatial_count - rep.flag_count, rest, busy, rest + HTA_NET_HEADER, drawn, HTA_WDEF_MAX_ENTITIES,
               HTA_WREP_MAX_SPATIAL, HTA_WREP_MAX_FLAGS, HTA_GFX_MAX_INSTANCES);
        if (!ok) { printf(", \"error\": "); json_str(rerr); }
        printf(",\n  \"objects\": [");
        for (uint32_t i = 0; i < d->count; i++) {
            const hta_wrep_kind_info *k = hta_wrep_kind(d->entity[i].kind);
            uint8_t ch = k ? k->channel : HTA_WREP_HOST_ONLY;
            printf("%s\n   {\"id\": ", i ? "," : ""); json_str(d->entity[i].id);
            printf(", \"kind\": \"%s\", \"runtime\": %u, \"channel\": \"%s\"", hta_wdef_kind_name(d->entity[i].kind), i,
                   hta_wrep_channel_name(ch));
            if (ch != HTA_WREP_HOST_ONLY && ok) printf(", \"index\": %u", rep.index[i]);
            printf("}");
        }
        printf("]}");
    }
    if (d->has_environment) {
        const hta_wenvironment *e = &d->environment;
        printf(",\n \"visual\": {\"ambient\": [%g, %g, %g], \"clear\": [%g, %g, %g], "
               "\"fog_color\": [%g, %g, %g], \"fog_density\": %g, \"fog_start\": %g, "
               "\"lights_authored\": %u, \"lights_active_max\": %u, \"lights\": [",
               (double)e->ambient[0], (double)e->ambient[1], (double)e->ambient[2],
               (double)e->clear[0], (double)e->clear[1], (double)e->clear[2],
               (double)e->fog_color[0], (double)e->fog_color[1], (double)e->fog_color[2],
               (double)e->fog_density, (double)e->fog_start, d->light_count, HTA_SCENE_MAX_LIGHTS);
        for (uint32_t i = 0; i < d->light_count; i++) {
            const hta_wlight_def *l = &d->light[i];
            printf("%s{\"id\": ", i ? ", " : ""); json_str(l->id);
            printf(", \"type\": \"%s\", \"position\": [%g, %g, %g], \"color\": [%g, %g, %g], "
                   "\"intensity\": %g, \"range\": %g",
                   l->spot ? "spot" : "point", (double)l->position[0], (double)l->position[1], (double)l->position[2],
                   (double)l->color[0], (double)l->color[1], (double)l->color[2],
                   (double)l->intensity, (double)l->range);
            if (l->relay) { printf(", \"relay\": "); json_str(d->entity[l->relay - 1].id); }
            printf("}");
        }
        printf("]}");
    }
    printf(",\n \"assets\": {\"payload_bytes\": %u, \"textures\": [", a->payload_bytes);
    for (uint32_t i = 0; i < a->texture_count; i++) {
        printf("%s{\"id\": ", i ? ", " : ""); json_str(a->texture[i].id);
        printf(", \"index\": %u, \"from\": ", i); json_str(FROM(a->texture[i].provider));
        printf(", \"width\": %u, \"height\": %u}", a->texture[i].width, a->texture[i].height);
    }
    printf("], \"materials\": [");
    for (uint32_t i = 0; i < a->material_count; i++) {
        printf("%s{\"id\": ", i ? ", " : ""); json_str(a->material[i].id);
        printf(", \"index\": %u, \"from\": ", i); json_str(FROM(a->material[i].provider));
        printf(", \"texture\": %u, \"draw\": \"%s\"", a->material[i].texture,
               a->material[i].draw == HTA_ASSET_DRAW_ALPHA ? "alpha" : "opaque");
        if (a->material[i].extended)
            printf(", \"emissive\": %g, \"roughness\": %g",
                   (double)a->material[i].emissive, (double)a->material[i].roughness);
        printf("}");
    }
    printf("], \"models\": [");
    for (uint32_t i = 0; i < a->model_count; i++) {
        const hta_asset_model *md = &a->model[i];
        printf("%s{\"id\": ", i ? ", " : ""); json_str(md->id);
        printf(", \"index\": %u, \"from\": ", i); json_str(FROM(md->provider));
        printf(", \"vertices\": %u, \"triangles\": %u, \"slots\": [", md->mesh.vertex_count, md->mesh.index_count / 3);
        for (uint32_t k = 0; k < md->slot_count; k++) printf("%s%u", k ? ", " : "", md->slot[k]);
        printf("], \"bounds\": [[%.3f, %.3f, %.3f], [%.3f, %.3f, %.3f]]}", md->mesh.bounds_min[0], md->mesh.bounds_min[1],
               md->mesh.bounds_min[2], md->mesh.bounds_max[0], md->mesh.bounds_max[1], md->mesh.bounds_max[2]);
    }
    printf("], \"sounds\": [");
    for (uint32_t i = 0; i < a->sound_count; i++) {
        printf("%s{\"id\": ", i ? ", " : ""); json_str(a->sound[i].id);
        printf(", \"index\": %u, \"from\": ", i); json_str(FROM(a->sound[i].provider));
        printf(", \"rate\": %u, \"channels\": %u, \"frames\": %u}", a->sound[i].rate, a->sound[i].channels, a->sound[i].frames);
    }
    printf("]}}\n");
#undef FROM
    hta_external_map_free(m);
    free(m);
    return 0;
}

int main(int argc, char **argv)
{
    const char *bundle = NULL, *world = NULL;
    bool markdown = false, conformance = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--json")) continue;
        else if (!strcmp(argv[i], "--markdown")) markdown = true;
        else if (!strcmp(argv[i], "--conformance")) conformance = true;
        else if (!strcmp(argv[i], "--bundle") && i + 1 < argc) bundle = argv[++i];
        else if (!strcmp(argv[i], "--world") && i + 1 < argc) world = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    if (bundle || world) {
        if (!bundle || !world) { usage(argv[0]); return 2; }
        return inspect(bundle, world);
    }
    size_t (*print)(char *, size_t) = markdown ? hta_resource_types_markdown :
                                      conformance ? hta_resource_conformance_json : hta_resource_contract_json;
    size_t n = print(NULL, 0);
    char *buf = malloc(n + 1);
    if (!buf) return 1;
    print(buf, n + 1);
    fputs(buf, stdout);
    free(buf);
    return 0;
}
