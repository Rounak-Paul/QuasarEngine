/*
 * qs_asset.c — Asset system implementation.
 *
 * Provides the central resource manager for the Quasar engine.
 * Assets are loaded by path, deduplicated, reference-counted, and
 * imported in the background via the job system.
 *
 * Threading model:
 *   - Import work (file I/O, parsing, decoding) runs on worker threads.
 *   - GPU upload (qs_texture_create, qs_mesh_create, etc.) runs on the
 *     main thread during the asset system's update tick.
 *   - State transitions use atomics for safe polling from any thread.
 */

#include "qs_asset.h"
#include "qs_mesh.h"
#include "qs_material.h"
#include "qs_texture.h"
#include "qs_scene.h"
#include "qs_renderer.h"
#include "qs_ext.h"
#include "qs_job.h"
#include "qs_log.h"
#include "qs_system.h"
#include "qs_math.h"
#include "quasar.h"

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <math.h>

/* ================================================================
   LIMITS
   ================================================================ */

#define QS_MAX_ASSETS  4096

/* ================================================================
   INTERNAL TYPES
   ================================================================ */

/// Model data: meshes + materials + textures + node graph.
typedef struct Qs_ModelData {
    Qs_NamedMesh     *meshes;
    uint32_t          mesh_count;
    Qs_NamedMaterial *materials;
    uint32_t          material_count;
    Qs_Texture      **textures;
    uint32_t          texture_count;
    Qs_ImportNode    *nodes;
    uint32_t          node_count;
    uint32_t         *mesh_material_map;  ///< mesh[i] → material index (UINT32_MAX=none).
} Qs_ModelData;

struct Qs_Asset {
    char              path[512];
    _Atomic(int)      state;
    Qs_AssetType      type;
    _Atomic(int)      ref_count;

    /* Staging: filled by importer on worker thread */
    Qs_ImportResult   staging;

    /* Final GPU resources (filled on main thread) */
    union {
        Qs_Texture   *texture;
        Qs_Mesh      *mesh;
        Qs_Material  *material;
        Qs_ModelData  model;
    } data;
};

typedef struct Qs_AssetSystem {
    Qs_Asset   *assets[QS_MAX_ASSETS];
    uint32_t    count;
    Qs_Engine  *engine;
} Qs_AssetSystem;

static Qs_AssetSystem *g_asset_system;

/* ================================================================
   IMPORTER LOOKUP
   ================================================================ */

static const Qs_AssetImporterExt *find_importer(Qs_Engine *engine,
                                                 const char *ext,
                                                 void **out_data)
{
    uint32_t count = qs_engine_ext_count(engine, QS_EXT_ASSET_IMPORTER);
    for (uint32_t i = 0; i < count; i++) {
        const Qs_AssetImporterExt *imp = (const Qs_AssetImporterExt *)
            qs_engine_ext_interface(engine, QS_EXT_ASSET_IMPORTER, i);
        if (!imp || !imp->extensions) continue;

        const char *p = imp->extensions;
        while (*p) {
            while (*p == ',' || *p == ' ') p++;
            if (!*p) break;
            char token[16];
            int ti = 0;
            while (*p && *p != ',' && ti < (int)sizeof(token) - 1)
                token[ti++] = *p++;
            token[ti] = '\0';
            if (strcmp(token, ext) == 0) {
                if (out_data)
                    *out_data = qs_engine_ext_data(engine, QS_EXT_ASSET_IMPORTER, i);
                return imp;
            }
        }
    }
    return NULL;
}

/* ================================================================
   IMPORT JOB
   ================================================================ */

typedef struct ImportJobData {
    Qs_Asset              *asset;
    const Qs_AssetImporterExt *importer;
    void                  *importer_data;
} ImportJobData;

static void import_job_fn(void *raw)
{
    ImportJobData *jd = (ImportJobData *)raw;
    Qs_Asset *a = jd->asset;

    memset(&a->staging, 0, sizeof(a->staging));
    bool ok = jd->importer->import(jd->importer_data, a->path, &a->staging);

    if (ok) {
        atomic_store(&a->state, (int)QS_ASSET_UPLOADING);
    } else {
        atomic_store(&a->state, (int)QS_ASSET_ERROR);
        QS_LOG_ERROR("Asset import failed: %s", a->path);
    }

    free(jd);
}

/* ================================================================
   GPU UPLOAD (main thread)
   ================================================================ */

static void upload_model(Qs_Asset *a, Qs_Engine *engine)
{
    Qs_ImportResult *r = &a->staging;
    Qs_ModelData *m = &a->data.model;
    memset(m, 0, sizeof(*m));

    /* --- Textures --- */
    if (r->texture_count > 0) {
        m->textures = (Qs_Texture **)calloc(r->texture_count, sizeof(Qs_Texture *));
        m->texture_count = r->texture_count;
        for (uint32_t i = 0; i < r->texture_count; i++) {
            Qs_ImportTexture *it = &r->textures[i];
            if (!it->pixels) continue;
            Qs_TextureDesc td = {
                .name          = it->name,
                .width         = it->width,
                .height        = it->height,
                .format        = (Qs_TextureFormat)it->format,
                .pixels        = it->pixels,
                .generate_mips = it->generate_mips,
                .min_filter    = (Qs_TextureFilter)it->min_filter,
                .mag_filter    = (Qs_TextureFilter)it->mag_filter,
                .wrap_u        = (Qs_TextureWrap)it->wrap_u,
                .wrap_v        = (Qs_TextureWrap)it->wrap_v,
            };
            m->textures[i] = qs_texture_create(engine, &td);
        }
    }

    /* --- Materials --- */
    if (r->material_count > 0) {
        m->materials = (Qs_NamedMaterial *)calloc(r->material_count, sizeof(Qs_NamedMaterial));
        m->material_count = r->material_count;
        for (uint32_t i = 0; i < r->material_count; i++) {
            Qs_ImportMaterial *im = &r->materials[i];
            Qs_MaterialDesc md = qs_material_desc_defaults();
            md.name = im->name;
            md.base_color_factor[0] = im->base_color[0];
            md.base_color_factor[1] = im->base_color[1];
            md.base_color_factor[2] = im->base_color[2];
            md.base_color_factor[3] = im->base_color[3];
            md.metallic_factor      = im->metallic;
            md.roughness_factor     = im->roughness;
            md.normal_scale         = im->normal_scale;
            md.occlusion_strength   = im->occlusion_strength;
            md.emissive_factor[0]   = im->emissive[0];
            md.emissive_factor[1]   = im->emissive[1];
            md.emissive_factor[2]   = im->emissive[2];
            md.alpha_mode           = (Qs_AlphaMode)im->alpha_mode;
            md.alpha_cutoff         = im->alpha_cutoff;
            md.double_sided         = im->double_sided;

            /* Resolve texture indices */
            #define TEX(idx) ((idx) < m->texture_count ? m->textures[(idx)] : NULL)
            md.base_color_texture         = TEX(im->base_color_tex);
            md.metallic_roughness_texture = TEX(im->metallic_roughness_tex);
            md.normal_texture             = TEX(im->normal_tex);
            md.occlusion_texture          = TEX(im->occlusion_tex);
            md.emissive_texture           = TEX(im->emissive_tex);
            #undef TEX

            Qs_Material *mat = qs_material_create(engine, &md);
            if (mat) {
                snprintf(m->materials[i].name, sizeof(m->materials[i].name),
                         "%s", im->name);
                m->materials[i].material = mat;
            }
        }
    }

    /* --- Meshes + material map --- */
    if (r->mesh_count > 0) {
        m->meshes = (Qs_NamedMesh *)calloc(r->mesh_count, sizeof(Qs_NamedMesh));
        m->mesh_material_map = (uint32_t *)malloc(r->mesh_count * sizeof(uint32_t));
        m->mesh_count = r->mesh_count;
        for (uint32_t i = 0; i < r->mesh_count; i++) {
            Qs_ImportMesh *imesh = &r->meshes[i];
            m->mesh_material_map[i] = imesh->material_index;

            if (!imesh->vertices) continue;
            Qs_MeshDesc md = {
                .name         = imesh->name,
                .vertices     = (const Qs_Vertex *)imesh->vertices,
                .vertex_count = imesh->vertex_count,
                .indices      = imesh->indices,
                .index_count  = imesh->index_count,
                .index_type   = QS_INDEX_TYPE_UINT32,
            };
            Qs_Mesh *mesh = qs_mesh_create(engine, &md);
            if (mesh) {
                snprintf(m->meshes[i].name, sizeof(m->meshes[i].name),
                         "%s", imesh->name);
                m->meshes[i].mesh = mesh;
            }
        }
    }

    /* --- Node graph (keep for instantiation) --- */
    if (r->node_count > 0) {
        m->nodes = (Qs_ImportNode *)malloc(r->node_count * sizeof(Qs_ImportNode));
        memcpy(m->nodes, r->nodes, r->node_count * sizeof(Qs_ImportNode));
        m->node_count = r->node_count;
    }
}

static void finalise_asset(Qs_Asset *a, Qs_Engine *engine)
{
    Qs_AssetState state = (Qs_AssetState)atomic_load(&a->state);
    if (state != QS_ASSET_UPLOADING) return;

    if (a->type == QS_ASSET_TYPE_MODEL) {
        upload_model(a, engine);
    }

    /* Free staging data */
    qs_import_result_free(&a->staging);

    atomic_store(&a->state, (int)QS_ASSET_READY);
    QS_LOG_INFO("Asset ready: %s (%u meshes, %u materials, %u textures)",
                a->path,
                a->type == QS_ASSET_TYPE_MODEL ? a->data.model.mesh_count : 0,
                a->type == QS_ASSET_TYPE_MODEL ? a->data.model.material_count : 0,
                a->type == QS_ASSET_TYPE_MODEL ? a->data.model.texture_count : 0);
}

/* ================================================================
   MODEL RENDERING (no child entities — direct renderable submission)
   ================================================================ */

/// Compute the world matrix for a node by composing parent chain → root.
static void compute_node_world(const Qs_ImportNode *nodes, uint32_t index,
                               const float root_world[16], float out[16])
{
    /* Walk up the parent chain to collect ancestor indices (leaf → root). */
    uint32_t chain[64];
    int depth = 0;
    for (int32_t i = (int32_t)index;
         i >= 0 && depth < 64;
         i = nodes[i].parent_index)
    {
        chain[depth++] = (uint32_t)i;
    }

    /* Start with the root world matrix (entity world × base transform). */
    memcpy(out, root_world, sizeof(float) * 16);

    /* Multiply from root down to leaf. */
    for (int i = depth - 1; i >= 0; i--) {
        const Qs_ImportNode *n = &nodes[chain[i]];
        float local[16], tmp[16];
        qs_m4_from_trs(local, n->position, n->rotation, n->scale);
        qs_m4_mul(out, local, tmp);
        memcpy(out, tmp, sizeof(float) * 16);
    }
}

void qs_asset_model_submit(const Qs_Asset *asset, Qs_Renderer *renderer,
                           const float world[16], Qs_Entity entity)
{
    if (!asset || !renderer) return;
    if ((Qs_AssetState)atomic_load(&asset->state) != QS_ASSET_READY) return;
    if (asset->type != QS_ASSET_TYPE_MODEL) return;

    const Qs_ModelData *m = &asset->data.model;
    if (!m->nodes || m->node_count == 0) return;

    for (uint32_t i = 0; i < m->node_count; i++) {
        const Qs_ImportNode *n = &m->nodes[i];
        if (n->mesh_index >= m->mesh_count) continue;
        if (!m->meshes[n->mesh_index].mesh) continue;

        /* Resolve material */
        Qs_Material *mat = NULL;
        if (m->mesh_material_map) {
            uint32_t mat_idx = m->mesh_material_map[n->mesh_index];
            if (mat_idx < m->material_count)
                mat = m->materials[mat_idx].material;
        }

        /* Compute node world matrix */
        float node_world[16];
        compute_node_world(m->nodes, i, world, node_world);

        Qs_RenderableDesc r = {
            .mesh            = m->meshes[n->mesh_index].mesh,
            .material        = mat,
            .entity          = entity,
            .cast_shadows    = true,
            .receive_shadows = true,
            .bounds          = { .min = {-100,-100,-100}, .max = {100,100,100} },
        };
        memcpy(r.transform, node_world, sizeof(float) * 16);
        qs_renderer_submit_renderable(renderer, &r);
    }
}

/* ================================================================
   PUBLIC API
   ================================================================ */

static Qs_Asset *find_cached(const char *path)
{
    if (!g_asset_system) return NULL;
    for (uint32_t i = 0; i < g_asset_system->count; i++) {
        Qs_Asset *a = g_asset_system->assets[i];
        if (a && strcmp(a->path, path) == 0) return a;
    }
    return NULL;
}

Qs_Asset *qs_asset_load(Qs_Engine *engine, const char *path)
{
    if (!g_asset_system || !path) return NULL;

    /* Dedup */
    Qs_Asset *cached = find_cached(path);
    if (cached) {
        qs_asset_acquire(cached);
        return cached;
    }

    if (g_asset_system->count >= QS_MAX_ASSETS) {
        QS_LOG_ERROR("Asset limit reached (%d)", QS_MAX_ASSETS);
        return NULL;
    }

    /* Create asset */
    Qs_Asset *a = (Qs_Asset *)calloc(1, sizeof(Qs_Asset));
    if (!a) return NULL;

    snprintf(a->path, sizeof(a->path), "%s", path);
    atomic_store(&a->ref_count, 1);
    atomic_store(&a->state, (int)QS_ASSET_UNLOADED);

    /* Determine type from extension */
    const char *dot = strrchr(path, '.');
    if (!dot) {
        QS_LOG_ERROR("Asset has no extension: %s", path);
        free(a);
        return NULL;
    }

    /* Find importer */
    void *imp_data = NULL;
    const Qs_AssetImporterExt *imp = find_importer(engine, dot, &imp_data);
    if (!imp) {
        QS_LOG_ERROR("No importer for '%s': %s", dot, path);
        free(a);
        return NULL;
    }

    /* Determine asset type from extension */
    if (strcmp(dot, ".gltf") == 0 || strcmp(dot, ".glb") == 0 ||
        strcmp(dot, ".fbx") == 0  || strcmp(dot, ".obj") == 0)
        a->type = QS_ASSET_TYPE_MODEL;
    else if (strcmp(dot, ".png") == 0 || strcmp(dot, ".jpg") == 0 ||
             strcmp(dot, ".jpeg") == 0 || strcmp(dot, ".tga") == 0)
        a->type = QS_ASSET_TYPE_TEXTURE;
    else
        a->type = QS_ASSET_TYPE_MODEL;

    g_asset_system->assets[g_asset_system->count++] = a;

    /* Dispatch import job on worker thread */
    atomic_store(&a->state, (int)QS_ASSET_LOADING);

    ImportJobData *jd = (ImportJobData *)malloc(sizeof(ImportJobData));
    jd->asset         = a;
    jd->importer      = imp;
    jd->importer_data = imp_data;

    Qs_JobSystem *jobs = qs_engine_job_system(engine);
    qs_job_dispatch(jobs, &(Qs_JobDesc){ .fn = import_job_fn, .data = jd }, NULL);

    QS_LOG_INFO("Asset loading: %s", path);
    return a;
}

void qs_asset_acquire(Qs_Asset *asset)
{
    if (!asset) return;
    atomic_fetch_add(&asset->ref_count, 1);
}

static void free_model_data(Qs_ModelData *m)
{
    for (uint32_t i = 0; i < m->mesh_count; i++)
        if (m->meshes[i].mesh) qs_mesh_destroy(m->meshes[i].mesh);
    for (uint32_t i = 0; i < m->material_count; i++)
        if (m->materials[i].material) qs_material_destroy(m->materials[i].material);
    for (uint32_t i = 0; i < m->texture_count; i++)
        if (m->textures[i]) qs_texture_destroy(m->textures[i]);
    free(m->meshes);
    free(m->materials);
    free(m->textures);
    free(m->nodes);
    free(m->mesh_material_map);
}

static void destroy_asset(Qs_Asset *a)
{
    Qs_AssetState state = (Qs_AssetState)atomic_load(&a->state);

    if (state == QS_ASSET_READY) {
        if (a->type == QS_ASSET_TYPE_MODEL)
            free_model_data(&a->data.model);
        else if (a->type == QS_ASSET_TYPE_TEXTURE && a->data.texture)
            qs_texture_destroy(a->data.texture);
        else if (a->type == QS_ASSET_TYPE_MESH && a->data.mesh)
            qs_mesh_destroy(a->data.mesh);
        else if (a->type == QS_ASSET_TYPE_MATERIAL && a->data.material)
            qs_material_destroy(a->data.material);
    }

    /* Remove from system array */
    if (g_asset_system) {
        for (uint32_t i = 0; i < g_asset_system->count; i++) {
            if (g_asset_system->assets[i] == a) {
                g_asset_system->assets[i] =
                    g_asset_system->assets[--g_asset_system->count];
                break;
            }
        }
    }

    free(a);
}

void qs_asset_release(Qs_Asset *asset)
{
    if (!asset) return;
    int prev = atomic_fetch_sub(&asset->ref_count, 1);
    if (prev <= 1) {
        destroy_asset(asset);
    }
}

Qs_AssetState qs_asset_state(const Qs_Asset *asset)
{
    if (!asset) return QS_ASSET_ERROR;
    return (Qs_AssetState)atomic_load(&asset->state);
}

Qs_AssetType qs_asset_type(const Qs_Asset *asset)
{
    return asset ? asset->type : QS_ASSET_TYPE_UNKNOWN;
}

const char *qs_asset_path(const Qs_Asset *asset)
{
    return asset ? asset->path : NULL;
}

Qs_Texture *qs_asset_texture(const Qs_Asset *asset)
{
    if (!asset || qs_asset_state(asset) != QS_ASSET_READY) return NULL;
    if (asset->type != QS_ASSET_TYPE_TEXTURE) return NULL;
    return asset->data.texture;
}

Qs_Mesh *qs_asset_mesh(const Qs_Asset *asset)
{
    if (!asset || qs_asset_state(asset) != QS_ASSET_READY) return NULL;
    if (asset->type != QS_ASSET_TYPE_MESH) return NULL;
    return asset->data.mesh;
}

Qs_Material *qs_asset_material(const Qs_Asset *asset)
{
    if (!asset || qs_asset_state(asset) != QS_ASSET_READY) return NULL;
    if (asset->type != QS_ASSET_TYPE_MATERIAL) return NULL;
    return asset->data.material;
}

uint32_t qs_asset_model_mesh_count(const Qs_Asset *asset)
{
    if (!asset || qs_asset_state(asset) != QS_ASSET_READY) return 0;
    if (asset->type != QS_ASSET_TYPE_MODEL) return 0;
    return asset->data.model.mesh_count;
}

const Qs_NamedMesh *qs_asset_model_mesh(const Qs_Asset *asset, uint32_t index)
{
    if (!asset || qs_asset_state(asset) != QS_ASSET_READY) return NULL;
    if (asset->type != QS_ASSET_TYPE_MODEL) return NULL;
    if (index >= asset->data.model.mesh_count) return NULL;
    return &asset->data.model.meshes[index];
}

uint32_t qs_asset_model_material_count(const Qs_Asset *asset)
{
    if (!asset || qs_asset_state(asset) != QS_ASSET_READY) return 0;
    if (asset->type != QS_ASSET_TYPE_MODEL) return 0;
    return asset->data.model.material_count;
}

const Qs_NamedMaterial *qs_asset_model_material(const Qs_Asset *asset, uint32_t index)
{
    if (!asset || qs_asset_state(asset) != QS_ASSET_READY) return NULL;
    if (asset->type != QS_ASSET_TYPE_MODEL) return NULL;
    if (index >= asset->data.model.material_count) return NULL;
    return &asset->data.model.materials[index];
}

uint32_t qs_asset_model_texture_count(const Qs_Asset *asset)
{
    if (!asset || qs_asset_state(asset) != QS_ASSET_READY) return 0;
    if (asset->type != QS_ASSET_TYPE_MODEL) return 0;
    return asset->data.model.texture_count;
}

Qs_Texture *qs_asset_model_texture(const Qs_Asset *asset, uint32_t index)
{
    if (!asset || qs_asset_state(asset) != QS_ASSET_READY) return NULL;
    if (asset->type != QS_ASSET_TYPE_MODEL) return NULL;
    if (index >= asset->data.model.texture_count) return NULL;
    return asset->data.model.textures[index];
}

/* ================================================================
   IMPORT RESULT CLEANUP
   ================================================================ */

void qs_import_result_free(Qs_ImportResult *result)
{
    if (!result) return;
    for (uint32_t i = 0; i < result->texture_count; i++)
        free(result->textures[i].pixels);
    free(result->textures);
    for (uint32_t i = 0; i < result->mesh_count; i++) {
        free(result->meshes[i].vertices);
        free(result->meshes[i].indices);
    }
    free(result->meshes);
    free(result->materials);
    free(result->nodes);
    memset(result, 0, sizeof(*result));
}

/* ================================================================
   SYSTEM CALLBACKS
   ================================================================ */

static bool asset_system_init(Qs_System *system, Qs_Engine *engine)
{
    Qs_AssetSystem *data = (Qs_AssetSystem *)qs_system_data(system);
    g_asset_system = data;
    data->engine = engine;
    QS_LOG_INFO("Asset system initialized");
    return true;
}

static void asset_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_AssetSystem *data = (Qs_AssetSystem *)qs_system_data(system);

    /* Destroy all remaining assets */
    for (uint32_t i = 0; i < data->count; i++) {
        Qs_Asset *a = data->assets[i];
        if (!a) continue;
        Qs_AssetState state = (Qs_AssetState)atomic_load(&a->state);
        if (state == QS_ASSET_READY && a->type == QS_ASSET_TYPE_MODEL)
            free_model_data(&a->data.model);
        else if (state == QS_ASSET_READY && a->type == QS_ASSET_TYPE_TEXTURE && a->data.texture)
            qs_texture_destroy(a->data.texture);
        else if (state == QS_ASSET_READY && a->type == QS_ASSET_TYPE_MESH && a->data.mesh)
            qs_mesh_destroy(a->data.mesh);
        else if (state == QS_ASSET_READY && a->type == QS_ASSET_TYPE_MATERIAL && a->data.material)
            qs_material_destroy(a->data.material);
        free(a);
        data->assets[i] = NULL;
    }
    data->count = 0;
    g_asset_system = NULL;
    QS_LOG_INFO("Asset system shut down");
}

static void asset_system_update(Qs_System *system, Qs_Engine *engine, float dt)
{
    (void)dt;
    Qs_AssetSystem *data = (Qs_AssetSystem *)qs_system_data(system);

    /* Finalise any assets that have finished importing */
    for (uint32_t i = 0; i < data->count; i++) {
        Qs_Asset *a = data->assets[i];
        if (!a) continue;
        if ((Qs_AssetState)atomic_load(&a->state) == QS_ASSET_UPLOADING)
            finalise_asset(a, engine);
    }
}

Qs_SystemDesc qs_asset_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Asset",
        .data_size = sizeof(Qs_AssetSystem),
        .init      = asset_system_init,
        .shutdown  = asset_system_shutdown,
        .update    = asset_system_update,
    };
}
