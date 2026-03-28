/*
 * qs_material_system.c — Engine material system.
 *
 * Manages PBR materials: descriptor sets, default fallback textures,
 * and per-material GPU state.  No backend indirection — Causality always
 * uses Vulkan.
 *
 * Descriptor layout (set=1 in the forward pipeline):
 *   binding 0  base_color           COMBINED_IMAGE_SAMPLER
 *   binding 1  metallic_roughness   COMBINED_IMAGE_SAMPLER
 *   binding 2  normal               COMBINED_IMAGE_SAMPLER
 *   binding 3  occlusion            COMBINED_IMAGE_SAMPLER
 *   binding 4  emissive             COMBINED_IMAGE_SAMPLER
 */

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

    m->params.base_color_factor[0] = desc->base_color_factor[0] != 0.0f ? desc->base_color_factor[0] : 1.0f;
    m->params.base_color_factor[1] = desc->base_color_factor[1] != 0.0f ? desc->base_color_factor[1] : 1.0f;
    m->params.base_color_factor[2] = desc->base_color_factor[2] != 0.0f ? desc->base_color_factor[2] : 1.0f;
    m->params.base_color_factor[3] = desc->base_color_factor[3] != 0.0f ? desc->base_color_factor[3] : 1.0f;
    m->params.metallic_factor      = desc->metallic_factor;
    m->params.roughness_factor     = desc->roughness_factor   != 0.0f ? desc->roughness_factor   : 1.0f;
    m->params.normal_scale         = desc->normal_scale       != 0.0f ? desc->normal_scale       : 1.0f;
    m->params.occlusion_strength   = desc->occlusion_strength != 0.0f ? desc->occlusion_strength : 1.0f;
    m->params.alpha_cutoff         = desc->alpha_cutoff       != 0.0f ? desc->alpha_cutoff       : 0.5f;
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
