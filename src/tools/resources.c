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
 *                                      its asset table and its world key (JSON).
 *                                      Exit 1 with the loader's message when
 *                                      the world is refused.
 *
 * Everything printed comes from the tables and code the loader uses. */
#include "app/content.h"
#include "app/fs.h"
#include "asset/external_map.h"
#include "asset/resource.h"
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
            printf(", \"field\": \"%s\", \"to\": ", hta_ref_get(HTA_REF_SCRIPT)->field);
            json_str(d->script[e->script - 1].id);
            printf(", \"index\": %u}", e->script - 1u);
        }
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
        if (e->kind != HTA_WDEF_PROP || !e->model || e->model > a->model_count) continue;
        printf("%s{\"from\": ", SEP()); json_str(e->id);
        printf(", \"field\": \"%s\", \"to\": ", hta_ref_get(HTA_REF_PROP_MODEL)->field);
        json_str(a->model[e->model - 1].id);
        printf(", \"provider\": "); json_str(FROM(a->model[e->model - 1].provider));
        printf(", \"index\": %u}", e->model - 1u);
    }
    for (uint32_t i = 0; i < d->mover_def_count; i++) {
        const hta_wmover_def *md = &d->mover_def[i];
        if (!md->sound || md->sound > a->sound_count) continue;
        printf("%s{\"from\": ", SEP()); json_str(md->id);
        printf(", \"field\": \"%s\", \"to\": ", hta_ref_get(HTA_REF_MOVER_SOUND)->field);
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
    printf("],\n \"assets\": {\"payload_bytes\": %u, \"textures\": [", a->payload_bytes);
    for (uint32_t i = 0; i < a->texture_count; i++) {
        printf("%s{\"id\": ", i ? ", " : ""); json_str(a->texture[i].id);
        printf(", \"index\": %u, \"from\": ", i); json_str(FROM(a->texture[i].provider));
        printf(", \"width\": %u, \"height\": %u}", a->texture[i].width, a->texture[i].height);
    }
    printf("], \"materials\": [");
    for (uint32_t i = 0; i < a->material_count; i++) {
        printf("%s{\"id\": ", i ? ", " : ""); json_str(a->material[i].id);
        printf(", \"index\": %u, \"from\": ", i); json_str(FROM(a->material[i].provider));
        printf(", \"texture\": %u, \"draw\": \"%s\"}", a->material[i].texture,
               a->material[i].draw == HTA_ASSET_DRAW_ALPHA ? "alpha" : "opaque");
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
