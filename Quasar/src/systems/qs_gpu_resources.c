/* qs_gpu_resources.c — Texture, Mesh, Material, and Light systems (consolidated). */

/*
 * qs_texture_system.c — Engine texture system.
 *
 * Manages GPU texture resources (images, image views, samplers) as a
 * first-class engine system.  No backend indirection — Causality always
 * uses Vulkan.
 */

#include "qs_texture.h"
#include "qs_system.h"
#include "qs_gpu.h"
#include "qs_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QS_MAX_TEXTURES 512

struct Qs_Texture {
    char             name[64];
    bool             in_use;
    Qs_GpuContext   *gpu;
    Qs_GpuImage     *image;
    Qs_GpuImageView *view;
    Qs_GpuSampler   *sampler;
    uint32_t         width;
    uint32_t         height;
    uint32_t         mip_levels;
};

typedef struct {
    Qs_GpuContext *gpu;
    Qs_Texture     textures[QS_MAX_TEXTURES];
    uint32_t       count;
} TextureSystemData;

static TextureSystemData *g_texture_sys;

/* ================================================================
   FORMAT HELPERS
   ================================================================ */

static Qs_GpuImageFormat texture_to_gpu_format(Qs_TextureFormat fmt)
{
    switch (fmt) {
    case QS_TEXTURE_FORMAT_RGBA8_SRGB:    return QS_GPU_FORMAT_RGBA8_SRGB;
    case QS_TEXTURE_FORMAT_RG8_UNORM:     return QS_GPU_FORMAT_RG8_UNORM;
    case QS_TEXTURE_FORMAT_R8_UNORM:      return QS_GPU_FORMAT_R8_UNORM;
    case QS_TEXTURE_FORMAT_RGBA16_SFLOAT: return QS_GPU_FORMAT_RGBA16_SFLOAT;
    default:                              return QS_GPU_FORMAT_RGBA8_UNORM;
    }
}

static uint32_t bytes_per_pixel(Qs_TextureFormat fmt)
{
    switch (fmt) {
    case QS_TEXTURE_FORMAT_RG8_UNORM:     return 2;
    case QS_TEXTURE_FORMAT_R8_UNORM:      return 1;
    case QS_TEXTURE_FORMAT_RGBA16_SFLOAT: return 8;
    default:                              return 4;
    }
}

static uint32_t compute_mip_levels(uint32_t w, uint32_t h)
{
    uint32_t levels = 1, dim = w > h ? w : h;
    while (dim > 1) { dim >>= 1; levels++; }
    return levels;
}

/* ================================================================
   SYSTEM LIFECYCLE
   ================================================================ */

static void texture_destroy_one(Qs_Texture *t)
{
    if (!t || !t->in_use) return;
    if (t->sampler) { qs_gpu_destroy_sampler   (t->gpu, t->sampler); t->sampler = NULL; }
    if (t->view)    { qs_gpu_destroy_image_view(t->gpu, t->view);    t->view    = NULL; }
    if (t->image)   { qs_gpu_destroy_image     (t->gpu, t->image);   t->image   = NULL; }
    t->in_use = false;
}

static bool texture_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    TextureSystemData *data = (TextureSystemData *)qs_system_data(sys);
    data->gpu     = qs_engine_gpu(engine);
    g_texture_sys = data;
    QS_LOG_INFO("Texture system initialised");
    return true;
}

static void texture_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    TextureSystemData *data = (TextureSystemData *)qs_system_data(sys);
    for (uint32_t i = 0; i < QS_MAX_TEXTURES; i++)
        texture_destroy_one(&data->textures[i]);
    g_texture_sys = NULL;
    QS_LOG_INFO("Texture system shut down");
}

Qs_SystemDesc qs_texture_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Texture",
        .data_size = sizeof(TextureSystemData),
        .init      = texture_sys_init,
        .shutdown  = texture_sys_shutdown,
        .update    = NULL,
    };
}

/* ================================================================
   PUBLIC API
   ================================================================ */

Qs_Texture *qs_texture_create(Qs_Engine *engine, const Qs_TextureDesc *desc)
{
    (void)engine;
    if (!g_texture_sys || !desc || desc->width == 0 || desc->height == 0) return NULL;

    Qs_Texture *t = NULL;
    for (uint32_t i = 0; i < QS_MAX_TEXTURES; i++) {
        if (!g_texture_sys->textures[i].in_use) { t = &g_texture_sys->textures[i]; break; }
    }
    if (!t) {
        QS_LOG_ERROR("Texture system: texture limit reached (%d)", QS_MAX_TEXTURES);
        return NULL;
    }

    memset(t, 0, sizeof(*t));
    t->in_use  = true;
    t->gpu     = g_texture_sys->gpu;
    t->width   = desc->width;
    t->height  = desc->height;

    if (desc->name) snprintf(t->name, sizeof(t->name), "%s", desc->name);
    else            snprintf(t->name, sizeof(t->name), "texture_%u", g_texture_sys->count);

    t->mip_levels = desc->generate_mips ? compute_mip_levels(desc->width, desc->height) : 1;

    Qs_GpuImageFormat fmt   = texture_to_gpu_format(desc->format);
    Qs_GpuImageUsage  usage = (Qs_GpuImageUsage)(QS_GPU_IMAGE_TRANSFER_DST | QS_GPU_IMAGE_SAMPLED);
    if (t->mip_levels > 1)
        usage = (Qs_GpuImageUsage)(usage | QS_GPU_IMAGE_TRANSFER_SRC);

    t->image = qs_gpu_create_image(t->gpu, &(Qs_GpuImageDesc){
        .width      = desc->width,
        .height     = desc->height,
        .mip_levels = t->mip_levels,
        .format     = fmt,
        .usage      = usage,
    });
    if (!t->image) {
        QS_LOG_ERROR("Texture system: failed to create image for '%s'", t->name);
        t->in_use = false;
        return NULL;
    }

    if (desc->pixels) {
        const uint64_t data_size = (uint64_t)desc->width * desc->height
                                   * bytes_per_pixel(desc->format);
        if (!qs_gpu_upload_image(t->gpu, t->image, desc->pixels, data_size, desc->generate_mips)) {
            QS_LOG_ERROR("Texture system: failed to upload pixels for '%s'", t->name);
            texture_destroy_one(t);
            return NULL;
        }
    }

    t->view = qs_gpu_create_image_view_for(t->gpu, t->image, QS_GPU_IMAGE_ASPECT_COLOR);
    if (!t->view) {
        QS_LOG_ERROR("Texture system: failed to create image view for '%s'", t->name);
        texture_destroy_one(t);
        return NULL;
    }

    t->sampler = qs_gpu_create_sampler(t->gpu, &(Qs_GpuSamplerDesc){
        .min_filter = (Qs_GpuFilter)desc->min_filter,
        .mag_filter = (Qs_GpuFilter)desc->mag_filter,
        .wrap_u     = (Qs_GpuWrap)desc->wrap_u,
        .wrap_v     = (Qs_GpuWrap)desc->wrap_v,
        .mip_levels = t->mip_levels,
        .anisotropy = true,
    });
    if (!t->sampler) {
        QS_LOG_ERROR("Texture system: failed to create sampler for '%s'", t->name);
        texture_destroy_one(t);
        return NULL;
    }

    g_texture_sys->count++;
    QS_LOG_INFO("Texture system: '%s' created (%ux%u, %u mips)",
                t->name, t->width, t->height, t->mip_levels);
    return t;
}

void qs_texture_destroy(Qs_Texture *texture)
{
    if (!texture || !texture->in_use) return;
    QS_LOG_INFO("Texture system: '%s' destroyed", texture->name);
    texture_destroy_one(texture);
    if (g_texture_sys && g_texture_sys->count > 0)
        g_texture_sys->count--;
}

const char      *qs_texture_name      (const Qs_Texture *t) { return t ? t->name       : NULL; }
Qs_GpuImageView *qs_texture_image_view(const Qs_Texture *t) { return t ? t->view       : NULL; }
Qs_GpuSampler   *qs_texture_sampler   (const Qs_Texture *t) { return t ? t->sampler    : NULL; }
uint32_t         qs_texture_mip_levels(const Qs_Texture *t) { return t ? t->mip_levels : 0; }

void qs_texture_extents(const Qs_Texture *t, uint32_t *out_w, uint32_t *out_h)
{
    if (out_w) *out_w = t ? t->width  : 0;
    if (out_h) *out_h = t ? t->height : 0;
}

uint32_t qs_texture_count(void)
{
    return g_texture_sys ? g_texture_sys->count : 0;
}

Qs_Texture *qs_texture_at(uint32_t index)
{
    if (!g_texture_sys) return NULL;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < QS_MAX_TEXTURES; i++) {
        if (g_texture_sys->textures[i].in_use) {
            if (seen == index) return &g_texture_sys->textures[i];
            seen++;
        }
    }
    return NULL;
}

/* ================================================================
   MESH SYSTEM  (was qs_mesh_system.c)
   ================================================================ */

#include "qs_mesh.h"
#include "qs_system.h"
#include "qs_gpu.h"
#include "qs_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QS_MAX_MESHES 512

struct Qs_Mesh {
    char           name[64];
    bool           in_use;
    Qs_GpuContext *gpu;
    Qs_GpuBuffer  *vertex_buffer;
    uint32_t       vertex_count;
    Qs_GpuBuffer  *index_buffer;
    uint32_t       index_count;
    Qs_IndexType   index_type;
};

typedef struct {
    Qs_GpuContext *gpu;
    Qs_Mesh        meshes[QS_MAX_MESHES];
    uint32_t       count;
} MeshSystemData;

static MeshSystemData *g_mesh_sys;

/* ================================================================
   SYSTEM LIFECYCLE
   ================================================================ */

static void mesh_destroy_one(Qs_Mesh *m)
{
    if (!m || !m->in_use) return;
    if (m->index_buffer)  { qs_gpu_destroy_buffer(m->gpu, m->index_buffer);  m->index_buffer  = NULL; }
    if (m->vertex_buffer) { qs_gpu_destroy_buffer(m->gpu, m->vertex_buffer); m->vertex_buffer = NULL; }
    m->in_use = false;
}

static bool mesh_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    MeshSystemData *data = (MeshSystemData *)qs_system_data(sys);
    data->gpu  = qs_engine_gpu(engine);
    g_mesh_sys = data;
    QS_LOG_INFO("Mesh system initialised");
    return true;
}

static void mesh_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    MeshSystemData *data = (MeshSystemData *)qs_system_data(sys);
    for (uint32_t i = 0; i < QS_MAX_MESHES; i++)
        mesh_destroy_one(&data->meshes[i]);
    g_mesh_sys = NULL;
    QS_LOG_INFO("Mesh system shut down");
}

Qs_SystemDesc qs_mesh_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Mesh",
        .data_size = sizeof(MeshSystemData),
        .init      = mesh_sys_init,
        .shutdown  = mesh_sys_shutdown,
        .update    = NULL,
    };
}

/* ================================================================
   PUBLIC API
   ================================================================ */

Qs_Mesh *qs_mesh_create(Qs_Engine *engine, const Qs_MeshDesc *desc)
{
    (void)engine;
    if (!g_mesh_sys || !desc || !desc->vertices || desc->vertex_count == 0) return NULL;

    Qs_Mesh *m = NULL;
    for (uint32_t i = 0; i < QS_MAX_MESHES; i++) {
        if (!g_mesh_sys->meshes[i].in_use) { m = &g_mesh_sys->meshes[i]; break; }
    }
    if (!m) {
        QS_LOG_ERROR("Mesh system: mesh limit reached (%d)", QS_MAX_MESHES);
        return NULL;
    }

    memset(m, 0, sizeof(*m));
    m->in_use       = true;
    m->gpu          = g_mesh_sys->gpu;
    m->vertex_count = desc->vertex_count;
    m->index_count  = desc->index_count;
    m->index_type   = desc->index_type;

    if (desc->name) snprintf(m->name, sizeof(m->name), "%s", desc->name);
    else            snprintf(m->name, sizeof(m->name), "mesh_%u", g_mesh_sys->count);

    const uint64_t vb_size = (uint64_t)desc->vertex_count * sizeof(Qs_Vertex);
    m->vertex_buffer = qs_gpu_create_buffer_from_data(g_mesh_sys->gpu,
        QS_GPU_BUFFER_VERTEX, desc->vertices, vb_size);
    if (!m->vertex_buffer) {
        QS_LOG_ERROR("Mesh system: failed to create vertex buffer for '%s'", m->name);
        m->in_use = false;
        return NULL;
    }

    if (desc->indices && desc->index_count > 0) {
        const uint64_t stride  = (desc->index_type == QS_INDEX_TYPE_UINT16) ? 2u : 4u;
        const uint64_t ib_size = (uint64_t)desc->index_count * stride;
        m->index_buffer = qs_gpu_create_buffer_from_data(g_mesh_sys->gpu,
            QS_GPU_BUFFER_INDEX, desc->indices, ib_size);
        if (!m->index_buffer) {
            QS_LOG_ERROR("Mesh system: failed to create index buffer for '%s'", m->name);
            mesh_destroy_one(m);
            return NULL;
        }
    }

    g_mesh_sys->count++;
    QS_LOG_INFO("Mesh system: '%s' created (%u verts, %u idx)",
                m->name, m->vertex_count, m->index_count);
    return m;
}

void qs_mesh_destroy(Qs_Mesh *mesh)
{
    if (!mesh || !mesh->in_use) return;
    QS_LOG_INFO("Mesh system: '%s' destroyed", mesh->name);
    mesh_destroy_one(mesh);
    if (g_mesh_sys && g_mesh_sys->count > 0)
        g_mesh_sys->count--;
}

const char   *qs_mesh_name        (const Qs_Mesh *m) { return m ? m->name         : NULL; }
uint32_t      qs_mesh_vertex_count(const Qs_Mesh *m) { return m ? m->vertex_count : 0; }
uint32_t      qs_mesh_index_count (const Qs_Mesh *m) { return m ? m->index_count  : 0; }
Qs_GpuBuffer *qs_mesh_vertex_buffer(const Qs_Mesh *m) { return m ? m->vertex_buffer : NULL; }
Qs_GpuBuffer *qs_mesh_index_buffer (const Qs_Mesh *m) { return m ? m->index_buffer  : NULL; }
Qs_IndexType  qs_mesh_index_type   (const Qs_Mesh *m) { return m ? m->index_type : QS_INDEX_TYPE_UINT32; }

void qs_mesh_bind(const Qs_Mesh *mesh, Qs_GpuCmd *cmd)
{
    if (!mesh || !cmd) return;
    qs_cmd_bind_vertex_buffer(cmd, 0, mesh->vertex_buffer, 0);
    if (mesh->index_buffer)
        qs_cmd_bind_index_buffer(cmd, mesh->index_buffer,
                                  mesh->index_type == QS_INDEX_TYPE_UINT16);
}

void qs_mesh_draw(const Qs_Mesh *mesh, Qs_GpuCmd *cmd)
{
    if (!mesh || !cmd) return;
    qs_mesh_bind(mesh, cmd);
    if (mesh->index_count > 0)
        qs_cmd_draw_indexed(cmd, mesh->index_count, 0, 0);
    else
        qs_cmd_draw(cmd, mesh->vertex_count, 0);
}

/* ================================================================
   MATERIAL SYSTEM  (was qs_material_system.c)
   ================================================================ */

#include "qs_material.h"
#include "qs_texture.h"
#include "qs_system.h"
#include "qs_gpu.h"
#include "qs_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QS_MAX_MATERIALS     256
#define QS_PBR_BINDING_COUNT   5

struct Qs_Material {
    char                 name[64];
    bool                 in_use;
    Qs_GpuContext       *gpu;
    Qs_Texture          *base_color_texture;
    Qs_Texture          *metallic_roughness_texture;
    Qs_Texture          *normal_texture;
    Qs_Texture          *occlusion_texture;
    Qs_Texture          *emissive_texture;
    Qs_PBRParams         params;
    Qs_AlphaMode         alpha_mode;
    bool                 double_sided;
    Qs_GpuDescriptorSet *descriptor_set;
};

typedef struct {
    Qs_GpuContext             *gpu;
    Qs_Material                materials[QS_MAX_MATERIALS];
    uint32_t                   count;
    Qs_GpuDescriptorSetLayout *set_layout;
    Qs_GpuDescriptorPool      *desc_pool;
    Qs_Texture                *default_white;
    Qs_Texture                *default_normal;
    Qs_Texture                *default_black;
} MaterialSystemData;

static MaterialSystemData *g_material_sys;

/* ================================================================
   DESCRIPTOR LAYOUT + POOL
   ================================================================ */

static bool create_descriptor_layout(void)
{
    Qs_GpuDescriptorBinding bindings[QS_PBR_BINDING_COUNT];
    for (uint32_t i = 0; i < QS_PBR_BINDING_COUNT; i++) {
        bindings[i] = (Qs_GpuDescriptorBinding){
            .binding = i,
            .type    = QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
            .count   = 1,
            .stages  = QS_GPU_SHADER_FRAGMENT,
        };
    }
    g_material_sys->set_layout = qs_gpu_create_descriptor_set_layout(
        g_material_sys->gpu, bindings, QS_PBR_BINDING_COUNT);
    return g_material_sys->set_layout != NULL;
}

static bool create_descriptor_pool(void)
{
    Qs_GpuDescriptorPoolSize pool_size = {
        .type  = QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
        .count = QS_MAX_MATERIALS * QS_PBR_BINDING_COUNT,
    };
    g_material_sys->desc_pool = qs_gpu_create_descriptor_pool(
        g_material_sys->gpu, &(Qs_GpuDescriptorPoolDesc){
            .sizes      = &pool_size,
            .size_count = 1,
            .max_sets   = QS_MAX_MATERIALS,
        });
    return g_material_sys->desc_pool != NULL;
}

/* ================================================================
   DEFAULT FALLBACK TEXTURES
   ================================================================ */

static Qs_Texture *create_1x1_texture(Qs_Engine *engine, const char *name,
                                       uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const uint8_t pixels[4] = { r, g, b, a };
    return qs_texture_create(engine, &(Qs_TextureDesc){
        .name   = name,
        .width  = 1,
        .height = 1,
        .pixels = pixels,
    });
}

/* ================================================================
   DESCRIPTOR SET WRITE
   ================================================================ */

static void write_descriptor_set(Qs_Material *m)
{
    struct { Qs_Texture *tex; Qs_Texture *fallback; }
        slots[QS_PBR_BINDING_COUNT] = {
            { m->base_color_texture,         g_material_sys->default_white  },
            { m->metallic_roughness_texture, g_material_sys->default_white  },
            { m->normal_texture,             g_material_sys->default_normal },
            { m->occlusion_texture,          g_material_sys->default_white  },
            { m->emissive_texture,           g_material_sys->default_black  },
        };

    for (uint32_t i = 0; i < QS_PBR_BINDING_COUNT; i++) {
        Qs_Texture *tex = slots[i].tex ? slots[i].tex : slots[i].fallback;
        qs_gpu_write_image_descriptor(m->gpu, m->descriptor_set, i,
                                       qs_texture_sampler(tex),
                                       qs_texture_image_view(tex));
    }
}

/* ================================================================
   SYSTEM LIFECYCLE
   ================================================================ */

static void material_destroy_one(Qs_Material *m)
{
    if (!m || !m->in_use) return;
    if (m->descriptor_set) {
        qs_gpu_free_descriptor_set(g_material_sys->gpu,
                                    g_material_sys->desc_pool,
                                    m->descriptor_set);
        m->descriptor_set = NULL;
    }
    m->in_use = false;
}

static bool material_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    MaterialSystemData *data = (MaterialSystemData *)qs_system_data(sys);
    data->gpu      = qs_engine_gpu(engine);
    g_material_sys = data;

    if (!create_descriptor_layout()) {
        QS_LOG_ERROR("Material system: failed to create descriptor set layout");
        g_material_sys = NULL;
        return false;
    }
    if (!create_descriptor_pool()) {
        QS_LOG_ERROR("Material system: failed to create descriptor pool");
        qs_gpu_destroy_descriptor_set_layout(data->gpu, data->set_layout);
        g_material_sys = NULL;
        return false;
    }

    data->default_white  = create_1x1_texture(engine, "_default_white",  255, 255, 255, 255);
    data->default_normal = create_1x1_texture(engine, "_default_normal", 128, 128, 255, 255);
    data->default_black  = create_1x1_texture(engine, "_default_black",    0,   0,   0, 255);
    if (!data->default_white || !data->default_normal || !data->default_black) {
        QS_LOG_ERROR("Material system: failed to create default fallback textures");
        g_material_sys = NULL;
        return false;
    }

    QS_LOG_INFO("Material system initialised");
    return true;
}

static void material_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    MaterialSystemData *data = (MaterialSystemData *)qs_system_data(sys);

    for (uint32_t i = 0; i < QS_MAX_MATERIALS; i++) {
        if (data->materials[i].in_use)
            material_destroy_one(&data->materials[i]);
    }

    if (data->default_white)  qs_texture_destroy(data->default_white);
    if (data->default_normal) qs_texture_destroy(data->default_normal);
    if (data->default_black)  qs_texture_destroy(data->default_black);

    if (data->desc_pool)  qs_gpu_destroy_descriptor_pool(data->gpu, data->desc_pool);
    if (data->set_layout) qs_gpu_destroy_descriptor_set_layout(data->gpu, data->set_layout);

    g_material_sys = NULL;
    QS_LOG_INFO("Material system shut down");
}

Qs_SystemDesc qs_material_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Material",
        .data_size = sizeof(MaterialSystemData),
        .init      = material_sys_init,
        .shutdown  = material_sys_shutdown,
        .update    = NULL,
    };
}

/* ================================================================
   PUBLIC API
   ================================================================ */

Qs_Material *qs_material_create(Qs_Engine *engine, const Qs_MaterialDesc *desc)
{
    (void)engine;
    if (!g_material_sys || !desc) return NULL;

    Qs_Material *m = NULL;
    for (uint32_t i = 0; i < QS_MAX_MATERIALS; i++) {
        if (!g_material_sys->materials[i].in_use) { m = &g_material_sys->materials[i]; break; }
    }
    if (!m) {
        QS_LOG_ERROR("Material system: material limit reached (%d)", QS_MAX_MATERIALS);
        return NULL;
    }

    memset(m, 0, sizeof(*m));
    m->in_use = true;
    m->gpu    = g_material_sys->gpu;

    if (desc->name) snprintf(m->name, sizeof(m->name), "%s", desc->name);
    else            snprintf(m->name, sizeof(m->name), "material_%u", g_material_sys->count);

    m->base_color_texture         = desc->base_color_texture;
    m->metallic_roughness_texture = desc->metallic_roughness_texture;
    m->normal_texture             = desc->normal_texture;
    m->occlusion_texture          = desc->occlusion_texture;
    m->emissive_texture           = desc->emissive_texture;
    m->alpha_mode                 = desc->alpha_mode;
    m->double_sided               = desc->double_sided;

    m->params.base_color_factor[0] = desc->base_color_factor[0];
    m->params.base_color_factor[1] = desc->base_color_factor[1];
    m->params.base_color_factor[2] = desc->base_color_factor[2];
    m->params.base_color_factor[3] = desc->base_color_factor[3];
    m->params.metallic_factor      = desc->metallic_factor;
    m->params.roughness_factor     = desc->roughness_factor;
    m->params.normal_scale         = desc->normal_scale;
    m->params.occlusion_strength   = desc->occlusion_strength;
    m->params.alpha_cutoff         = desc->alpha_cutoff;
    m->params.emissive_factor[0]   = desc->emissive_factor[0];
    m->params.emissive_factor[1]   = desc->emissive_factor[1];
    m->params.emissive_factor[2]   = desc->emissive_factor[2];

    m->params.alpha_mode                 = (uint32_t)desc->alpha_mode;
    m->params.has_base_color_tex         = desc->base_color_texture         ? 1 : 0;
    m->params.has_metallic_roughness_tex = desc->metallic_roughness_texture ? 1 : 0;
    m->params.has_normal_tex             = desc->normal_texture             ? 1 : 0;
    m->params.has_occlusion_tex          = desc->occlusion_texture          ? 1 : 0;
    m->params.has_emissive_tex           = desc->emissive_texture           ? 1 : 0;
    m->params.double_sided               = desc->double_sided ? 1 : 0;

    m->descriptor_set = qs_gpu_alloc_descriptor_set(
        g_material_sys->gpu, g_material_sys->desc_pool, g_material_sys->set_layout);
    if (!m->descriptor_set) {
        QS_LOG_ERROR("Material system: failed to allocate descriptor set for '%s'", m->name);
        m->in_use = false;
        return NULL;
    }

    write_descriptor_set(m);

    g_material_sys->count++;
    QS_LOG_INFO("Material system: '%s' created", m->name);
    return m;
}

void qs_material_destroy(Qs_Material *material)
{
    if (!material || !material->in_use) return;
    QS_LOG_INFO("Material system: '%s' destroyed", material->name);
    material_destroy_one(material);
    if (g_material_sys && g_material_sys->count > 0)
        g_material_sys->count--;
}

const char *qs_material_name(const Qs_Material *m) { return m ? m->name          : NULL; }

Qs_GpuDescriptorSet *qs_material_descriptor_set(const Qs_Material *m)
{
    return m ? m->descriptor_set : NULL;
}

const Qs_PBRParams *qs_material_params(const Qs_Material *m)
{
    return m ? &m->params : NULL;
}

Qs_AlphaMode qs_material_alpha_mode(const Qs_Material *m)
{
    return m ? m->alpha_mode : QS_ALPHA_MODE_OPAQUE;
}

bool qs_material_double_sided(const Qs_Material *m)
{
    return m ? m->double_sided : false;
}

Qs_GpuDescriptorSetLayout *qs_material_set_layout(void)
{
    return g_material_sys ? g_material_sys->set_layout : NULL;
}

void qs_material_set_texture(Qs_Material *mat, uint32_t slot, Qs_Texture *tex)
{
    if (!mat || !mat->in_use || !g_material_sys || slot >= QS_PBR_BINDING_COUNT)
        return;

    Qs_Texture **stored[] = {
        &mat->base_color_texture,
        &mat->metallic_roughness_texture,
        &mat->normal_texture,
        &mat->occlusion_texture,
        &mat->emissive_texture,
    };
    *stored[slot] = tex;

    Qs_Texture *fallbacks[] = {
        g_material_sys->default_white,
        g_material_sys->default_white,
        g_material_sys->default_normal,
        g_material_sys->default_white,
        g_material_sys->default_black,
    };
    Qs_Texture *actual = tex ? tex : fallbacks[slot];
    qs_gpu_write_image_descriptor(mat->gpu, mat->descriptor_set, slot,
                                   qs_texture_sampler(actual),
                                   qs_texture_image_view(actual));

    /* Update has_*_tex flags in PBR params */
    uint32_t *has_flags[] = {
        &mat->params.has_base_color_tex,
        &mat->params.has_metallic_roughness_tex,
        &mat->params.has_normal_tex,
        &mat->params.has_occlusion_tex,
        &mat->params.has_emissive_tex,
    };
    *has_flags[slot] = (tex != NULL) ? 1 : 0;
}

Qs_Texture *qs_material_get_texture(const Qs_Material *mat, uint32_t slot)
{
    if (!mat || !mat->in_use || slot >= QS_PBR_BINDING_COUNT) return NULL;
    const Qs_Texture * const stored[] = {
        mat->base_color_texture,
        mat->metallic_roughness_texture,
        mat->normal_texture,
        mat->occlusion_texture,
        mat->emissive_texture,
    };
    return (Qs_Texture *)stored[slot];
}

void qs_material_update_params(Qs_Material *mat, const Qs_PBRParams *params)
{
    if (!mat || !mat->in_use || !params) return;
    /* Preserve has_*_tex flags — they are owned by set_texture, not by params. */
    uint32_t has_base  = mat->params.has_base_color_tex;
    uint32_t has_mr    = mat->params.has_metallic_roughness_tex;
    uint32_t has_norm  = mat->params.has_normal_tex;
    uint32_t has_occ   = mat->params.has_occlusion_tex;
    uint32_t has_emit  = mat->params.has_emissive_tex;
    mat->params = *params;
    mat->params.has_base_color_tex         = has_base;
    mat->params.has_metallic_roughness_tex = has_mr;
    mat->params.has_normal_tex             = has_norm;
    mat->params.has_occlusion_tex          = has_occ;
    mat->params.has_emissive_tex           = has_emit;
}

uint32_t qs_material_count(void)
{
    return g_material_sys ? g_material_sys->count : 0;
}

Qs_Material *qs_material_at(uint32_t index)
{
    if (!g_material_sys) return NULL;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < QS_MAX_MATERIALS; i++) {
        if (g_material_sys->materials[i].in_use) {
            if (seen == index) return &g_material_sys->materials[i];
            seen++;
        }
    }
    return NULL;
}

/* ================================================================
   LIGHT SYSTEM  (was qs_light_system.c)
   ================================================================ */

#include "qs_light.h"
#include "qs_math.h"
#include "qs_system.h"
#include "qs_log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define QS_MAX_LIGHTS 256

struct Qs_Light {
    char         name[64];
    bool         in_use;
    bool         enabled;
    Qs_LightType type;
    float        position[3];
    float        direction[3];
    float        color[3];
    float        intensity;
    float        range;
    float        inner_cone_cos;
    float        outer_cone_cos;
    bool         cast_shadows;
};

typedef struct {
    Qs_Light lights[QS_MAX_LIGHTS];
    uint32_t count;
} LightSystemData;

static LightSystemData *g_light_sys;

/* ================================================================
   SYSTEM LIFECYCLE
   ================================================================ */

static bool light_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    g_light_sys = (LightSystemData *)qs_system_data(sys);
    QS_LOG_INFO("Light system initialised");
    return true;
}

static void light_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)sys;
    (void)engine;
    g_light_sys = NULL;
    QS_LOG_INFO("Light system shut down");
}

Qs_SystemDesc qs_light_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Light",
        .data_size = sizeof(LightSystemData),
        .init      = light_sys_init,
        .shutdown  = light_sys_shutdown,
        .update    = NULL,
    };
}

/* ================================================================
   PUBLIC API
   ================================================================ */

Qs_Light *qs_light_create(Qs_Engine *engine, const Qs_LightDesc *desc)
{
    (void)engine;
    if (!g_light_sys || !desc) return NULL;

    Qs_Light *l = NULL;
    for (uint32_t i = 0; i < QS_MAX_LIGHTS; i++) {
        if (!g_light_sys->lights[i].in_use) { l = &g_light_sys->lights[i]; break; }
    }
    if (!l) {
        QS_LOG_ERROR("Light system: light limit reached (%d)", QS_MAX_LIGHTS);
        return NULL;
    }

    memset(l, 0, sizeof(*l));
    l->in_use  = true;
    l->enabled = true;
    l->type    = desc->type;

    l->position[0]  = desc->position[0];
    l->position[1]  = desc->position[1];
    l->position[2]  = desc->position[2];
    l->direction[0] = desc->direction[0];
    l->direction[1] = desc->direction[1];
    l->direction[2] = desc->direction[2];

    l->color[0] = desc->color[0];
    l->color[1] = desc->color[1];
    l->color[2] = desc->color[2];

    l->intensity    = desc->intensity;
    l->range        = desc->range;
    l->cast_shadows = desc->cast_shadows;

    l->inner_cone_cos = cosf(qs_to_rad(desc->inner_cone_deg));
    l->outer_cone_cos = cosf(qs_to_rad(desc->outer_cone_deg));

    /* Ensure direction is never a zero vector */
    if (l->direction[0] == 0.0f && l->direction[1] == 0.0f && l->direction[2] == 0.0f)
        l->direction[1] = -1.0f;

    if (desc->name) snprintf(l->name, sizeof(l->name), "%s", desc->name);
    else            snprintf(l->name, sizeof(l->name), "light_%u", g_light_sys->count);

    g_light_sys->count++;
    QS_LOG_INFO("Light system: '%s' created (type=%d)", l->name, l->type);
    return l;
}

void qs_light_destroy(Qs_Light *light)
{
    if (!light || !light->in_use) return;
    QS_LOG_INFO("Light system: '%s' destroyed", light->name);
    light->in_use = false;
    if (g_light_sys && g_light_sys->count > 0)
        g_light_sys->count--;
}

const char *qs_light_name     (const Qs_Light *l) { return l ? l->name      : NULL; }
float      *qs_light_position (Qs_Light *l)        { return l ? l->position  : NULL; }
float      *qs_light_direction(Qs_Light *l)        { return l ? l->direction : NULL; }
float      *qs_light_color    (Qs_Light *l)        { return l ? l->color     : NULL; }
float       qs_light_intensity(const Qs_Light *l)  { return l ? l->intensity : 0.0f; }

void qs_light_set_intensity(Qs_Light *l, float v) { if (l) l->intensity = v; }
void qs_light_set_range    (Qs_Light *l, float v) { if (l) l->range     = v; }
void qs_light_set_enabled  (Qs_Light *l, bool  v) { if (l) l->enabled   = v; }
bool qs_light_enabled      (const Qs_Light *l)    { return l ? l->enabled : false; }

void qs_light_set_cone(Qs_Light *l, float inner_deg, float outer_deg)
{
    if (!l) return;
    l->inner_cone_cos = cosf(qs_to_rad(inner_deg));
    l->outer_cone_cos = cosf(qs_to_rad(outer_deg));
}

bool qs_light_is_active(const Qs_Light *l)
{
    return l && l->in_use && l->enabled;
}

void qs_light_pack_gpu(const Qs_Light *l, Qs_LightGPU *out)
{
    if (!l || !out) return;
    out->position[0]    = l->position[0];
    out->position[1]    = l->position[1];
    out->position[2]    = l->position[2];
    out->range          = l->range;
    out->direction[0]   = l->direction[0];
    out->direction[1]   = l->direction[1];
    out->direction[2]   = l->direction[2];
    out->intensity      = l->intensity;
    out->color[0]       = l->color[0];
    out->color[1]       = l->color[1];
    out->color[2]       = l->color[2];
    out->inner_cone_cos = l->inner_cone_cos;
    out->outer_cone_cos = l->outer_cone_cos;
    out->type           = (uint32_t)l->type;
    out->cast_shadows   = l->cast_shadows ? 1 : 0;
    out->_pad           = 0;
}
