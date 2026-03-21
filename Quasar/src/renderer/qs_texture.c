#include "qs_texture.h"
#include "qs_log.h"
#include "qs_system.h"
#include "causality.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ================================================================
   LIMITS
   ================================================================ */

#define QS_MAX_TEXTURES 512

/* ================================================================
   INTERNAL TYPES
   ================================================================ */

struct Qs_Texture {
    char              name[64];
    bool              in_use;

    VkDevice          device;
    VkPhysicalDevice  physical_device;
    Ca_Instance      *ca_instance;

    VkImage           image;
    VkDeviceMemory    memory;
    VkImageView       view;
    VkSampler         sampler;

    uint32_t          width;
    uint32_t          height;
    uint32_t          mip_levels;
    VkFormat          vk_format;
};

typedef struct Qs_TextureSystemData {
    Ca_Instance      *ca_instance;
    VkDevice          device;
    VkPhysicalDevice  physical_device;
    Qs_Texture        textures[QS_MAX_TEXTURES];
    uint32_t          count;
} Qs_TextureSystemData;

static Qs_TextureSystemData *g_texture_system;

/* ================================================================
   FORMAT CONVERSION
   ================================================================ */

static VkFormat qs_to_vk_format(Qs_TextureFormat fmt)
{
    switch (fmt) {
    case QS_TEXTURE_FORMAT_RGBA8_SRGB:    return VK_FORMAT_R8G8B8A8_SRGB;
    case QS_TEXTURE_FORMAT_RG8_UNORM:     return VK_FORMAT_R8G8_UNORM;
    case QS_TEXTURE_FORMAT_R8_UNORM:      return VK_FORMAT_R8_UNORM;
    case QS_TEXTURE_FORMAT_RGBA16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
    default:                              return VK_FORMAT_R8G8B8A8_UNORM;
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
    uint32_t levels = 1;
    uint32_t dim = w > h ? w : h;
    while (dim > 1) { dim >>= 1; levels++; }
    return levels;
}

static VkFilter to_vk_filter(Qs_TextureFilter f)
{
    return (f == QS_TEXTURE_FILTER_NEAREST)
        ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

static VkSamplerAddressMode to_vk_wrap(Qs_TextureWrap w)
{
    switch (w) {
    case QS_TEXTURE_WRAP_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case QS_TEXTURE_WRAP_CLAMP_TO_EDGE:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    default:                              return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

/* ================================================================
   VULKAN HELPERS
   ================================================================ */

static uint32_t find_memory_type(VkPhysicalDevice pd, uint32_t type_bits,
                                  VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(pd, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    return UINT32_MAX;
}

/* ================================================================
   MIP GENERATION (blit chain)
   ================================================================ */

static void generate_mipmaps(VkCommandBuffer cmd, VkImage image,
                              uint32_t w, uint32_t h, uint32_t mip_levels)
{
    int32_t mip_w = (int32_t)w;
    int32_t mip_h = (int32_t)h;

    for (uint32_t i = 1; i < mip_levels; i++) {
        /* Transition level i-1 from TRANSFER_DST to TRANSFER_SRC */
        VkImageMemoryBarrier barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1 },
        };
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 1, &barrier);

        int32_t next_w = mip_w > 1 ? mip_w / 2 : 1;
        int32_t next_h = mip_h > 1 ? mip_h / 2 : 1;

        VkImageBlit blit = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 },
            .srcOffsets     = { {0, 0, 0}, {mip_w, mip_h, 1} },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 },
            .dstOffsets     = { {0, 0, 0}, {next_w, next_h, 1} },
        };
        vkCmdBlitImage(cmd,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        /* Transition level i-1 to SHADER_READ_ONLY */
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &barrier);

        mip_w = next_w;
        mip_h = next_h;
    }

    /* Transition the last mip level to SHADER_READ_ONLY */
    VkImageMemoryBarrier last = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, mip_levels - 1, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &last);
}

/* ================================================================
   TEXTURE LIFECYCLE
   ================================================================ */

Qs_Texture *qs_texture_create(Qs_Engine *engine, const Qs_TextureDesc *desc)
{
    (void)engine;
    if (!g_texture_system || !desc || desc->width == 0 || desc->height == 0)
        return NULL;

    Qs_Texture *t = NULL;
    for (uint32_t i = 0; i < QS_MAX_TEXTURES; i++) {
        if (!g_texture_system->textures[i].in_use) {
            t = &g_texture_system->textures[i];
            break;
        }
    }
    if (!t) {
        QS_LOG_ERROR("Texture limit reached (%d)", QS_MAX_TEXTURES);
        return NULL;
    }

    memset(t, 0, sizeof(*t));
    t->in_use          = true;
    t->device          = g_texture_system->device;
    t->physical_device = g_texture_system->physical_device;
    t->ca_instance     = g_texture_system->ca_instance;
    t->width           = desc->width;
    t->height          = desc->height;
    t->vk_format       = qs_to_vk_format(desc->format);

    if (desc->name)
        snprintf(t->name, sizeof(t->name), "%s", desc->name);
    else
        snprintf(t->name, sizeof(t->name), "texture_%u", g_texture_system->count);

    t->mip_levels = desc->generate_mips
        ? compute_mip_levels(desc->width, desc->height) : 1;

    /* --- Create VkImage --- */
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (t->mip_levels > 1)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkImageCreateInfo img_ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = t->vk_format,
        .extent        = { desc->width, desc->height, 1 },
        .mipLevels     = t->mip_levels,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = usage,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(t->device, &img_ci, NULL, &t->image) != VK_SUCCESS) {
        QS_LOG_ERROR("Failed to create VkImage for texture '%s'", t->name);
        t->in_use = false;
        return NULL;
    }

    /* --- Allocate and bind device memory --- */
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(t->device, t->image, &req);
    uint32_t mi = find_memory_type(t->physical_device, req.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mi == UINT32_MAX) {
        QS_LOG_ERROR("No suitable memory type for texture '%s'", t->name);
        vkDestroyImage(t->device, t->image, NULL);
        t->in_use = false;
        return NULL;
    }

    VkMemoryAllocateInfo mem_ai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = req.size,
        .memoryTypeIndex = mi,
    };
    if (vkAllocateMemory(t->device, &mem_ai, NULL, &t->memory) != VK_SUCCESS) {
        vkDestroyImage(t->device, t->image, NULL);
        t->in_use = false;
        return NULL;
    }
    vkBindImageMemory(t->device, t->image, t->memory, 0);

    /* --- Upload pixel data via staging buffer --- */
    if (desc->pixels) {
        VkDeviceSize data_size = (VkDeviceSize)(desc->width * desc->height *
                                                bytes_per_pixel(desc->format));

        VkBuffer staging;
        VkDeviceMemory staging_mem;

        VkBufferCreateInfo buf_ci = {
            .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size        = data_size,
            .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(t->device, &buf_ci, NULL, &staging);

        VkMemoryRequirements buf_req;
        vkGetBufferMemoryRequirements(t->device, staging, &buf_req);
        uint32_t buf_mi = find_memory_type(t->physical_device, buf_req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkMemoryAllocateInfo buf_ai = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize  = buf_req.size,
            .memoryTypeIndex = buf_mi,
        };
        vkAllocateMemory(t->device, &buf_ai, NULL, &staging_mem);
        vkBindBufferMemory(t->device, staging, staging_mem, 0);

        void *mapped;
        vkMapMemory(t->device, staging_mem, 0, data_size, 0, &mapped);
        memcpy(mapped, desc->pixels, (size_t)data_size);
        vkUnmapMemory(t->device, staging_mem);

        VkCommandBuffer cmd = ca_gpu_begin_transfer(t->ca_instance);

        /* Transition all mip levels to TRANSFER_DST */
        VkImageMemoryBarrier barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = 0,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = t->image,
            .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, t->mip_levels, 0, 1 },
        };
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 1, &barrier);

        /* Copy level 0 from staging buffer */
        VkBufferImageCopy region = {
            .bufferOffset     = 0,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent      = { desc->width, desc->height, 1 },
        };
        vkCmdCopyBufferToImage(cmd, staging, t->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        if (t->mip_levels > 1) {
            generate_mipmaps(cmd, t->image, desc->width, desc->height, t->mip_levels);
        } else {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.subresourceRange.levelCount = 1;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &barrier);
        }

        ca_gpu_end_transfer(t->ca_instance, cmd);

        vkDestroyBuffer(t->device, staging, NULL);
        vkFreeMemory(t->device, staging_mem, NULL);
    }

    /* --- Image view --- */
    VkImageViewCreateInfo view_ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = t->image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = t->vk_format,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, t->mip_levels, 0, 1 },
    };
    if (vkCreateImageView(t->device, &view_ci, NULL, &t->view) != VK_SUCCESS) {
        QS_LOG_ERROR("Failed to create image view for texture '%s'", t->name);
        qs_texture_destroy(t);
        return NULL;
    }

    /* --- Sampler --- */
    VkSamplerCreateInfo samp_ci = {
        .sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter        = to_vk_filter(desc->mag_filter),
        .minFilter        = to_vk_filter(desc->min_filter),
        .mipmapMode       = (t->mip_levels > 1)
                             ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                             : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU     = to_vk_wrap(desc->wrap_u),
        .addressModeV     = to_vk_wrap(desc->wrap_v),
        .addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy    = 16.0f,
        .minLod           = 0.0f,
        .maxLod           = (float)t->mip_levels,
    };
    if (vkCreateSampler(t->device, &samp_ci, NULL, &t->sampler) != VK_SUCCESS) {
        QS_LOG_ERROR("Failed to create sampler for texture '%s'", t->name);
        qs_texture_destroy(t);
        return NULL;
    }

    g_texture_system->count++;
    QS_LOG_INFO("Texture '%s' created (%ux%u, %u mips)",
                t->name, t->width, t->height, t->mip_levels);
    return t;
}

void qs_texture_destroy(Qs_Texture *texture)
{
    if (!texture || !texture->in_use) return;

    vkDeviceWaitIdle(texture->device);

    if (texture->sampler) vkDestroySampler(texture->device, texture->sampler, NULL);
    if (texture->view)    vkDestroyImageView(texture->device, texture->view, NULL);
    if (texture->image)   vkDestroyImage(texture->device, texture->image, NULL);
    if (texture->memory)  vkFreeMemory(texture->device, texture->memory, NULL);

    QS_LOG_INFO("Texture '%s' destroyed", texture->name);
    texture->in_use = false;

    if (g_texture_system && g_texture_system->count > 0)
        g_texture_system->count--;
}

/* ================================================================
   ACCESSORS
   ================================================================ */

const char *qs_texture_name(const Qs_Texture *texture)
{
    return texture ? texture->name : NULL;
}

VkImageView qs_texture_image_view(const Qs_Texture *texture)
{
    return texture ? texture->view : VK_NULL_HANDLE;
}

VkSampler qs_texture_sampler(const Qs_Texture *texture)
{
    return texture ? texture->sampler : VK_NULL_HANDLE;
}

void qs_texture_extents(const Qs_Texture *texture,
                        uint32_t *out_width, uint32_t *out_height)
{
    if (!texture) {
        if (out_width)  *out_width  = 0;
        if (out_height) *out_height = 0;
        return;
    }
    if (out_width)  *out_width  = texture->width;
    if (out_height) *out_height = texture->height;
}

uint32_t qs_texture_mip_levels(const Qs_Texture *texture)
{
    return texture ? texture->mip_levels : 0;
}

/* ================================================================
   TEXTURE SYSTEM — engine system callbacks
   ================================================================ */

static bool texture_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_TextureSystemData *data = (Qs_TextureSystemData *)qs_system_data(system);
    if (!data->ca_instance) return false;

    data->device          = ca_gpu_device(data->ca_instance);
    data->physical_device = ca_gpu_physical_device(data->ca_instance);
    if (!data->device || !data->physical_device) return false;

    g_texture_system = data;

    QS_LOG_INFO("Texture system initialized (device %p)", (void *)data->device);
    return true;
}

static void texture_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_TextureSystemData *data = (Qs_TextureSystemData *)qs_system_data(system);

    for (uint32_t i = 0; i < QS_MAX_TEXTURES; i++) {
        if (data->textures[i].in_use)
            qs_texture_destroy(&data->textures[i]);
    }

    g_texture_system = NULL;
    QS_LOG_INFO("Texture system shut down");
}

static Ca_Instance *s_pending_texture_instance;

static bool texture_system_init_wrapper(Qs_System *sys, Qs_Engine *eng)
{
    Qs_TextureSystemData *d = (Qs_TextureSystemData *)qs_system_data(sys);
    d->ca_instance = s_pending_texture_instance;
    return texture_system_init(sys, eng);
}

Qs_SystemDesc qs_texture_system_desc(Ca_Instance *ca_instance)
{
    s_pending_texture_instance = ca_instance;

    return (Qs_SystemDesc){
        .name      = "Texture",
        .data_size = sizeof(Qs_TextureSystemData),
        .init      = texture_system_init_wrapper,
        .shutdown  = texture_system_shutdown,
        .update    = NULL,
    };
}
