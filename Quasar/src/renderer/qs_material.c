#include "qs_material.h"
#include "qs_texture.h"
#include "qs_log.h"
#include "qs_system.h"
#include "causality.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
   LIMITS
   ================================================================ */

#define QS_MAX_MATERIALS       256
#define QS_PBR_BINDING_COUNT   5

/* ================================================================
   INTERNAL TYPES
   ================================================================ */

struct Qs_Material {
    char              name[64];
    bool              in_use;

    VkDevice          device;
    Ca_Instance      *ca_instance;

    /* Texture references (not owned) */
    Qs_Texture       *base_color_texture;
    Qs_Texture       *metallic_roughness_texture;
    Qs_Texture       *normal_texture;
    Qs_Texture       *occlusion_texture;
    Qs_Texture       *emissive_texture;

    /* Parameters */
    Qs_PBRParams      params;
    Qs_AlphaMode      alpha_mode;
    bool              double_sided;

    /* Descriptor set for texture bindings */
    VkDescriptorSet   descriptor_set;
};

typedef struct Qs_MaterialSystemData {
    Ca_Instance          *ca_instance;
    VkDevice              device;
    VkPhysicalDevice      physical_device;
    Qs_Material           materials[QS_MAX_MATERIALS];
    uint32_t              count;

    /* Shared descriptor resources */
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool      desc_pool;

    /* 1x1 white, 1x1 default normal, 1x1 black fallback textures */
    Qs_Texture           *default_white;
    Qs_Texture           *default_normal;
    Qs_Texture           *default_black;
} Qs_MaterialSystemData;

static Qs_MaterialSystemData *g_material_system;

/* ================================================================
   DESCRIPTOR LAYOUT
   ================================================================ */

static bool create_descriptor_layout(VkDevice device)
{
    VkDescriptorSetLayoutBinding bindings[QS_PBR_BINDING_COUNT];
    for (uint32_t i = 0; i < QS_PBR_BINDING_COUNT; i++) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding         = i,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = QS_PBR_BINDING_COUNT,
        .pBindings    = bindings,
    };
    return vkCreateDescriptorSetLayout(device, &ci, NULL,
                                       &g_material_system->set_layout) == VK_SUCCESS;
}

static bool create_descriptor_pool(VkDevice device)
{
    VkDescriptorPoolSize pool_size = {
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = QS_MAX_MATERIALS * QS_PBR_BINDING_COUNT,
    };
    VkDescriptorPoolCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets       = QS_MAX_MATERIALS,
        .poolSizeCount = 1,
        .pPoolSizes    = &pool_size,
    };
    return vkCreateDescriptorPool(device, &ci, NULL,
                                  &g_material_system->desc_pool) == VK_SUCCESS;
}

/* ================================================================
   DEFAULT FALLBACK TEXTURES
   ================================================================ */

static Qs_Texture *create_1x1_texture(Qs_Engine *engine, const char *name,
                                       uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t pixels[4] = { r, g, b, a };
    return qs_texture_create(engine, &(Qs_TextureDesc){
        .name   = name,
        .width  = 1,
        .height = 1,
        .pixels = pixels,
    });
}

/* ================================================================
   DESCRIPTOR SET UPDATE
   ================================================================ */

static void write_descriptor_set(Qs_Material *m)
{
    /* For each PBR slot, use the assigned texture or a fallback */
    struct { Qs_Texture *tex; Qs_Texture *fallback; } slots[QS_PBR_BINDING_COUNT] = {
        { m->base_color_texture,          g_material_system->default_white  },
        { m->metallic_roughness_texture,  g_material_system->default_white  },
        { m->normal_texture,              g_material_system->default_normal },
        { m->occlusion_texture,           g_material_system->default_white  },
        { m->emissive_texture,            g_material_system->default_black  },
    };

    VkDescriptorImageInfo img_infos[QS_PBR_BINDING_COUNT];
    VkWriteDescriptorSet  writes[QS_PBR_BINDING_COUNT];

    for (uint32_t i = 0; i < QS_PBR_BINDING_COUNT; i++) {
        Qs_Texture *tex = slots[i].tex ? slots[i].tex : slots[i].fallback;
        img_infos[i] = (VkDescriptorImageInfo){
            .sampler     = qs_texture_sampler(tex),
            .imageView   = qs_texture_image_view(tex),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        writes[i] = (VkWriteDescriptorSet){
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = m->descriptor_set,
            .dstBinding      = i,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo      = &img_infos[i],
        };
    }

    vkUpdateDescriptorSets(m->device, QS_PBR_BINDING_COUNT, writes, 0, NULL);
}

/* ================================================================
   MATERIAL LIFECYCLE
   ================================================================ */

Qs_Material *qs_material_create(Qs_Engine *engine, const Qs_MaterialDesc *desc)
{
    (void)engine;
    if (!g_material_system || !desc) return NULL;

    Qs_Material *m = NULL;
    for (uint32_t i = 0; i < QS_MAX_MATERIALS; i++) {
        if (!g_material_system->materials[i].in_use) {
            m = &g_material_system->materials[i];
            break;
        }
    }
    if (!m) {
        QS_LOG_ERROR("Material limit reached (%d)", QS_MAX_MATERIALS);
        return NULL;
    }

    memset(m, 0, sizeof(*m));
    m->in_use      = true;
    m->device      = g_material_system->device;
    m->ca_instance = g_material_system->ca_instance;

    if (desc->name)
        snprintf(m->name, sizeof(m->name), "%s", desc->name);
    else
        snprintf(m->name, sizeof(m->name), "material_%u", g_material_system->count);

    /* Texture references */
    m->base_color_texture          = desc->base_color_texture;
    m->metallic_roughness_texture  = desc->metallic_roughness_texture;
    m->normal_texture              = desc->normal_texture;
    m->occlusion_texture           = desc->occlusion_texture;
    m->emissive_texture            = desc->emissive_texture;

    m->alpha_mode    = desc->alpha_mode;
    m->double_sided  = desc->double_sided;

    /* Fill PBR params struct */
    m->params.base_color_factor[0] = desc->base_color_factor[0] != 0.0f ? desc->base_color_factor[0] : 1.0f;
    m->params.base_color_factor[1] = desc->base_color_factor[1] != 0.0f ? desc->base_color_factor[1] : 1.0f;
    m->params.base_color_factor[2] = desc->base_color_factor[2] != 0.0f ? desc->base_color_factor[2] : 1.0f;
    m->params.base_color_factor[3] = desc->base_color_factor[3] != 0.0f ? desc->base_color_factor[3] : 1.0f;

    m->params.metallic_factor   = desc->metallic_factor  != 0.0f ? desc->metallic_factor  : 1.0f;
    m->params.roughness_factor  = desc->roughness_factor != 0.0f ? desc->roughness_factor : 1.0f;
    m->params.normal_scale      = desc->normal_scale     != 0.0f ? desc->normal_scale     : 1.0f;
    m->params.occlusion_strength= desc->occlusion_strength != 0.0f ? desc->occlusion_strength : 1.0f;
    m->params.alpha_cutoff      = desc->alpha_cutoff     != 0.0f ? desc->alpha_cutoff     : 0.5f;

    m->params.emissive_factor[0] = desc->emissive_factor[0];
    m->params.emissive_factor[1] = desc->emissive_factor[1];
    m->params.emissive_factor[2] = desc->emissive_factor[2];

    m->params.alpha_mode                  = (uint32_t)desc->alpha_mode;
    m->params.has_base_color_tex          = desc->base_color_texture         ? 1 : 0;
    m->params.has_metallic_roughness_tex  = desc->metallic_roughness_texture ? 1 : 0;
    m->params.has_normal_tex              = desc->normal_texture             ? 1 : 0;
    m->params.has_occlusion_tex           = desc->occlusion_texture          ? 1 : 0;
    m->params.has_emissive_tex            = desc->emissive_texture           ? 1 : 0;
    m->params.double_sided               = desc->double_sided ? 1 : 0;

    /* Allocate descriptor set */
    VkDescriptorSetAllocateInfo ds_ai = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = g_material_system->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &g_material_system->set_layout,
    };
    if (vkAllocateDescriptorSets(m->device, &ds_ai, &m->descriptor_set) != VK_SUCCESS) {
        QS_LOG_ERROR("Failed to allocate descriptor set for material '%s'", m->name);
        m->in_use = false;
        return NULL;
    }

    write_descriptor_set(m);

    g_material_system->count++;
    QS_LOG_INFO("Material '%s' created (alpha=%d, double_sided=%d)",
                m->name, m->alpha_mode, m->double_sided);
    return m;
}

void qs_material_destroy(Qs_Material *material)
{
    if (!material || !material->in_use) return;

    vkDeviceWaitIdle(material->device);

    if (material->descriptor_set)
        vkFreeDescriptorSets(material->device, g_material_system->desc_pool,
                             1, &material->descriptor_set);

    QS_LOG_INFO("Material '%s' destroyed", material->name);
    material->in_use = false;

    if (g_material_system && g_material_system->count > 0)
        g_material_system->count--;
}

/* ================================================================
   ACCESSORS
   ================================================================ */

const char *qs_material_name(const Qs_Material *material)
{
    return material ? material->name : NULL;
}

VkDescriptorSet qs_material_descriptor_set(const Qs_Material *material)
{
    return material ? material->descriptor_set : VK_NULL_HANDLE;
}

VkDescriptorSetLayout qs_material_set_layout(void)
{
    return g_material_system ? g_material_system->set_layout : VK_NULL_HANDLE;
}

const Qs_PBRParams *qs_material_params(const Qs_Material *material)
{
    return material ? &material->params : NULL;
}

Qs_AlphaMode qs_material_alpha_mode(const Qs_Material *material)
{
    return material ? material->alpha_mode : QS_ALPHA_MODE_OPAQUE;
}

bool qs_material_double_sided(const Qs_Material *material)
{
    return material ? material->double_sided : false;
}

/* ================================================================
   MATERIAL SYSTEM — engine system callbacks
   ================================================================ */

static bool material_system_init(Qs_System *system, Qs_Engine *engine)
{
    Qs_MaterialSystemData *data = (Qs_MaterialSystemData *)qs_system_data(system);
    if (!data->ca_instance) return false;

    data->device          = ca_gpu_device(data->ca_instance);
    data->physical_device = ca_gpu_physical_device(data->ca_instance);
    if (!data->device || !data->physical_device) return false;

    g_material_system = data;

    if (!create_descriptor_layout(data->device)) {
        QS_LOG_ERROR("Failed to create material descriptor set layout");
        g_material_system = NULL;
        return false;
    }

    if (!create_descriptor_pool(data->device)) {
        QS_LOG_ERROR("Failed to create material descriptor pool");
        vkDestroyDescriptorSetLayout(data->device, data->set_layout, NULL);
        g_material_system = NULL;
        return false;
    }

    /* Create fallback textures */
    data->default_white  = create_1x1_texture(engine, "_default_white",  255, 255, 255, 255);
    data->default_normal = create_1x1_texture(engine, "_default_normal", 128, 128, 255, 255);
    data->default_black  = create_1x1_texture(engine, "_default_black",  0,   0,   0,   255);

    if (!data->default_white || !data->default_normal || !data->default_black) {
        QS_LOG_ERROR("Failed to create default fallback textures");
        g_material_system = NULL;
        return false;
    }

    QS_LOG_INFO("Material system initialized (device %p)", (void *)data->device);
    return true;
}

static void material_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_MaterialSystemData *data = (Qs_MaterialSystemData *)qs_system_data(system);

    /* Destroy all materials first (they hold descriptor sets) */
    for (uint32_t i = 0; i < QS_MAX_MATERIALS; i++) {
        if (data->materials[i].in_use)
            qs_material_destroy(&data->materials[i]);
    }

    /* Destroy fallback textures (they live in the texture system pool) */
    if (data->default_white)  qs_texture_destroy(data->default_white);
    if (data->default_normal) qs_texture_destroy(data->default_normal);
    if (data->default_black)  qs_texture_destroy(data->default_black);

    if (data->desc_pool)
        vkDestroyDescriptorPool(data->device, data->desc_pool, NULL);
    if (data->set_layout)
        vkDestroyDescriptorSetLayout(data->device, data->set_layout, NULL);

    g_material_system = NULL;
    QS_LOG_INFO("Material system shut down");
}

static Ca_Instance *s_pending_material_instance;

static bool material_system_init_wrapper(Qs_System *sys, Qs_Engine *eng)
{
    Qs_MaterialSystemData *d = (Qs_MaterialSystemData *)qs_system_data(sys);
    d->ca_instance = s_pending_material_instance;
    return material_system_init(sys, eng);
}

Qs_SystemDesc qs_material_system_desc(Ca_Instance *ca_instance)
{
    s_pending_material_instance = ca_instance;

    return (Qs_SystemDesc){
        .name      = "Material",
        .data_size = sizeof(Qs_MaterialSystemData),
        .init      = material_system_init_wrapper,
        .shutdown  = material_system_shutdown,
        .update    = NULL,
    };
}
