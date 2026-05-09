#include "qs_gpu.h"
#include "qs_log.h"
#include "causality.h"
#include <vulkan/vulkan.h>

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

/* Live VRAM usage counters — updated on every vkAllocateMemory / vkFreeMemory */
static _Atomic size_t g_vram_device_bytes;  /* DEVICE_LOCAL total */
static _Atomic size_t g_vram_host_bytes;    /* HOST_VISIBLE total */
/* Per-purpose breakdown — indexed by Qs_GpuMemTag */
static _Atomic size_t g_gpu_tag_bytes[QS_GPU_MEM_TAG_COUNT];

/*
 * Private struct definitions.  Backends only see forward declarations;
 * all Vulkan is confined to this translation unit.
 */

struct Qs_GpuCmd {
    VkCommandBuffer cmd;
};

struct Qs_GpuBuffer {
    VkBuffer       buffer;
    VkDeviceMemory memory;
    uint64_t       size;        /* user-requested size */
    uint64_t       vram_size;   /* actual allocated VRAM (aligned) */
    bool           device_local;
    Qs_GpuMemTag   gpu_tag;     /* per-purpose tracking category */
};

struct Qs_GpuImage {
    VkImage        image;
    VkDeviceMemory memory;
    uint32_t       width;
    uint32_t       height;
    uint32_t       mip_levels;
    VkFormat       format;
    uint64_t       vram_size;   /* actual allocated VRAM */
    Qs_GpuMemTag   gpu_tag;     /* per-purpose tracking category */
};

struct Qs_GpuImageView {
    VkImageView view;
};

struct Qs_GpuSampler {
    VkSampler sampler;
};

struct Qs_GpuShader {
    VkShaderModule module;
};

struct Qs_GpuPipeline {
    VkPipeline pipeline;
};

struct Qs_GpuPipelineLayout {
    VkPipelineLayout layout;
};

struct Qs_GpuDescriptorSetLayout {
    VkDescriptorSetLayout layout;
};

struct Qs_GpuDescriptorPool {
    VkDescriptorPool pool;
};

struct Qs_GpuDescriptorSet {
    VkDescriptorSet set;
};

/* ================================================================
   INTERNAL HELPERS
   ================================================================ */

/* Internal — defined in engine.c, not part of the public quasar.h API. */
Ca_Instance *qs_engine_ca_instance(Qs_Engine *engine);

static inline Ca_Instance *to_ca(Qs_GpuContext *gpu)
{
    return (Ca_Instance *)gpu;
}

static VkFormat gpu_format_to_vk(Qs_GpuImageFormat fmt)
{
    switch (fmt) {
    case QS_GPU_FORMAT_RGBA8_UNORM:   return VK_FORMAT_R8G8B8A8_UNORM;
    case QS_GPU_FORMAT_RGBA8_SRGB:    return VK_FORMAT_R8G8B8A8_SRGB;
    case QS_GPU_FORMAT_RG8_UNORM:     return VK_FORMAT_R8G8_UNORM;
    case QS_GPU_FORMAT_R8_UNORM:      return VK_FORMAT_R8_UNORM;
    case QS_GPU_FORMAT_RGBA16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case QS_GPU_FORMAT_BGRA8_UNORM:   return VK_FORMAT_B8G8R8A8_UNORM;
    case QS_GPU_FORMAT_D32_SFLOAT:    return VK_FORMAT_D32_SFLOAT;
    case QS_GPU_FORMAT_D32_SFLOAT_S8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case QS_GPU_FORMAT_D24_UNORM_S8:  return VK_FORMAT_D24_UNORM_S8_UINT;
    case QS_GPU_FORMAT_DEPTH_AUTO: {
        /* Caller should resolve DEPTH_AUTO before calling this */
        return VK_FORMAT_D32_SFLOAT;
    }
    case QS_GPU_FORMAT_NONE: return VK_FORMAT_UNDEFINED;
    default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

static VkFormat pick_depth_format(VkPhysicalDevice pd)
{
    VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (uint32_t i = 0; i < 3; i++) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(pd, candidates[i], &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return candidates[i];
    }
    return VK_FORMAT_D32_SFLOAT;
}

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

static VkShaderStageFlagBits gpu_stage_to_vk_single(Qs_GpuShaderStage stage)
{
    switch (stage) {
    case QS_GPU_SHADER_VERTEX:   return VK_SHADER_STAGE_VERTEX_BIT;
    case QS_GPU_SHADER_FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;
    case QS_GPU_SHADER_COMPUTE:  return VK_SHADER_STAGE_COMPUTE_BIT;
    default: return VK_SHADER_STAGE_VERTEX_BIT;
    }
}

static VkShaderStageFlags gpu_stages_to_vk(Qs_GpuShaderStage stages)
{
    VkShaderStageFlags flags = 0;
    if (stages & QS_GPU_SHADER_VERTEX)   flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (stages & QS_GPU_SHADER_FRAGMENT) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (stages & QS_GPU_SHADER_COMPUTE)  flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    return flags;
}

static VkDescriptorType gpu_descriptor_type_to_vk(Qs_GpuDescriptorType t)
{
    switch (t) {
    case QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case QS_GPU_DESCRIPTOR_UNIFORM_BUFFER:         return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case QS_GPU_DESCRIPTOR_STORAGE_BUFFER:         return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case QS_GPU_DESCRIPTOR_STORAGE_IMAGE:          return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    default: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
}

static VkFormat gpu_vertex_format_to_vk(Qs_GpuVertexFormat fmt)
{
    switch (fmt) {
    case QS_GPU_VERTEX_FORMAT_FLOAT:  return VK_FORMAT_R32_SFLOAT;
    case QS_GPU_VERTEX_FORMAT_FLOAT2: return VK_FORMAT_R32G32_SFLOAT;
    case QS_GPU_VERTEX_FORMAT_FLOAT3: return VK_FORMAT_R32G32B32_SFLOAT;
    case QS_GPU_VERTEX_FORMAT_FLOAT4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    default: return VK_FORMAT_R32G32B32_SFLOAT;
    }
}

static VkPipelineStageFlags layout_to_src_stage(Qs_GpuImageLayout layout)
{
    switch (layout) {
    case QS_GPU_IMAGE_LAYOUT_TRANSFER_SRC:     return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case QS_GPU_IMAGE_LAYOUT_TRANSFER_DST:     return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case QS_GPU_IMAGE_LAYOUT_SHADER_READ:      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT: return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    default: return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
}

static VkPipelineStageFlags layout_to_dst_stage(Qs_GpuImageLayout layout)
{
    switch (layout) {
    case QS_GPU_IMAGE_LAYOUT_TRANSFER_SRC:     return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case QS_GPU_IMAGE_LAYOUT_TRANSFER_DST:     return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case QS_GPU_IMAGE_LAYOUT_SHADER_READ:      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT: return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    default: return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
}

static VkAccessFlags layout_to_access(Qs_GpuImageLayout layout)
{
    switch (layout) {
    case QS_GPU_IMAGE_LAYOUT_TRANSFER_SRC:     return VK_ACCESS_TRANSFER_READ_BIT;
    case QS_GPU_IMAGE_LAYOUT_TRANSFER_DST:     return VK_ACCESS_TRANSFER_WRITE_BIT;
    case QS_GPU_IMAGE_LAYOUT_SHADER_READ:      return VK_ACCESS_SHADER_READ_BIT;
    case QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT: return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    default: return 0;
    }
}

static VkImageLayout layout_to_vk(Qs_GpuImageLayout layout)
{
    switch (layout) {
    case QS_GPU_IMAGE_LAYOUT_UNDEFINED:         return VK_IMAGE_LAYOUT_UNDEFINED;
    case QS_GPU_IMAGE_LAYOUT_TRANSFER_SRC:      return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case QS_GPU_IMAGE_LAYOUT_TRANSFER_DST:      return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case QS_GPU_IMAGE_LAYOUT_SHADER_READ:       return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT:  return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT:  return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    default: return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

static VkImageAspectFlags aspect_to_vk(Qs_GpuImageAspect aspect)
{
    return (aspect == QS_GPU_IMAGE_ASPECT_DEPTH)
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;
}

static VkSampleCountFlagBits sample_count_to_vk(uint32_t samples)
{
    switch (samples) {
    case 2:  return VK_SAMPLE_COUNT_2_BIT;
    case 4:  return VK_SAMPLE_COUNT_4_BIT;
    case 8:  return VK_SAMPLE_COUNT_8_BIT;
    case 16: return VK_SAMPLE_COUNT_16_BIT;
    default: return VK_SAMPLE_COUNT_1_BIT;
    }
}

static Qs_GpuBuffer *create_buffer_raw(VkDevice device, VkPhysicalDevice pd,
                                        VkDeviceSize size, VkBufferUsageFlags usage,
                                        VkMemoryPropertyFlags mem_flags,
                                        Qs_GpuMemTag gpu_tag)
{
    VkBufferCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer vk_buf;
    if (vkCreateBuffer(device, &ci, NULL, &vk_buf) != VK_SUCCESS) return NULL;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, vk_buf, &req);
    uint32_t mi = find_memory_type(pd, req.memoryTypeBits, mem_flags);
    if (mi == UINT32_MAX) { vkDestroyBuffer(device, vk_buf, NULL); return NULL; }

    VkMemoryAllocateInfo ai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = req.size,
        .memoryTypeIndex = mi,
    };
    VkDeviceMemory vk_mem;
    if (vkAllocateMemory(device, &ai, NULL, &vk_mem) != VK_SUCCESS) {
        vkDestroyBuffer(device, vk_buf, NULL); return NULL;
    }
    vkBindBufferMemory(device, vk_buf, vk_mem, 0);

    bool is_device_local = (mem_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
    if (is_device_local)
        atomic_fetch_add(&g_vram_device_bytes, (size_t)req.size);
    else
        atomic_fetch_add(&g_vram_host_bytes,   (size_t)req.size);
    atomic_fetch_add(&g_gpu_tag_bytes[gpu_tag], (size_t)req.size);

    Qs_GpuBuffer *buf = qs_calloc(1, sizeof(Qs_GpuBuffer), QS_MEM_GPU);
    if (!buf) {
        vkDestroyBuffer(device, vk_buf, NULL); vkFreeMemory(device, vk_mem, NULL);
        if (is_device_local) atomic_fetch_sub(&g_vram_device_bytes, (size_t)req.size);
        else                 atomic_fetch_sub(&g_vram_host_bytes,   (size_t)req.size);
        atomic_fetch_sub(&g_gpu_tag_bytes[gpu_tag], (size_t)req.size);
        return NULL;
    }
    buf->buffer       = vk_buf;
    buf->memory       = vk_mem;
    buf->size         = (uint64_t)size;
    buf->vram_size    = (uint64_t)req.size;
    buf->device_local = is_device_local;
    buf->gpu_tag      = gpu_tag;
    return buf;
}

/* A stack-allocated Qs_GpuCmd wrapping the transfer cmd */
static Qs_GpuCmd *begin_transfer_internal(Ca_Instance *ca, Qs_GpuCmd *out_cmd)
{
    out_cmd->cmd = ca_gpu_begin_transfer(ca);
    return out_cmd;
}

static void end_transfer_internal(Ca_Instance *ca, const Qs_GpuCmd *cmd)
{
    ca_gpu_end_transfer(ca, cmd->cmd);
}

/* ================================================================
   MIPMAP GENERATION  (internal)
   ================================================================ */

static void generate_mipmaps_internal(Qs_GpuCmd *cmd, Qs_GpuImage *image)
{
    VkCommandBuffer vk_cmd = cmd->cmd;
    int32_t mip_w = (int32_t)image->width;
    int32_t mip_h = (int32_t)image->height;

    for (uint32_t i = 1; i < image->mip_levels; i++) {
        /* Transition mip[i-1]: TRANSFER_DST → TRANSFER_SRC */
        VkImageMemoryBarrier b1 = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image->image,
            .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1 },
        };
        vkCmdPipelineBarrier(vk_cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 1, &b1);

        int32_t next_w = mip_w > 1 ? mip_w / 2 : 1;
        int32_t next_h = mip_h > 1 ? mip_h / 2 : 1;

        VkImageBlit blit = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 },
            .srcOffsets     = { {0, 0, 0}, {mip_w, mip_h, 1} },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 },
            .dstOffsets     = { {0, 0, 0}, {next_w, next_h, 1} },
        };
        vkCmdBlitImage(vk_cmd,
            image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        /* Transition mip[i-1]: TRANSFER_SRC → SHADER_READ */
        VkImageMemoryBarrier b2 = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image->image,
            .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1 },
        };
        vkCmdPipelineBarrier(vk_cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &b2);

        mip_w = next_w;
        mip_h = next_h;
    }

    /* Transition final mip: TRANSFER_DST → SHADER_READ */
    VkImageMemoryBarrier b_last = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image->image,
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, image->mip_levels - 1, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(vk_cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &b_last);
}

/* ================================================================
   VIEWPORT CALLBACK TRAMPOLINE
   ================================================================ */

typedef struct {
    Qs_Viewport            *viewport;
    Qs_ViewportRenderFn     user_render_fn;
    void                   *user_render_data;
    Qs_ViewportResizeFn     user_resize_fn;
    void                   *user_resize_data;
} ViewportCallbackState;

#define MAX_VIEWPORT_STATES 16
static ViewportCallbackState s_vp_states[MAX_VIEWPORT_STATES];
static uint32_t              s_vp_state_count = 0;

static ViewportCallbackState *get_or_alloc_vp_state(Qs_Viewport *vp)
{
    for (uint32_t i = 0; i < s_vp_state_count; i++) {
        if (s_vp_states[i].viewport == vp)
            return &s_vp_states[i];
    }
    if (s_vp_state_count >= MAX_VIEWPORT_STATES) {
        QS_LOG_ERROR("Viewport state pool full (%d slots)", MAX_VIEWPORT_STATES);
        return NULL;
    }
    uint32_t idx = s_vp_state_count++;
    memset(&s_vp_states[idx], 0, sizeof(s_vp_states[idx]));
    s_vp_states[idx].viewport = vp;
    return &s_vp_states[idx];
}

static void ca_render_trampoline(Ca_Viewport *ca_vp, void *data)
{
    ViewportCallbackState *s = data;
    if (!s->user_render_fn) return;   /* cleared during renderer destroy / reload */
    Qs_GpuCmd       qs_cmd  = { .cmd  = ca_viewport_cmd(ca_vp) };
    Qs_GpuImageView qs_view = { .view = ca_viewport_image_view(ca_vp) };
    Qs_GpuFrame     frame   = {
        .cmd          = &qs_cmd,
        .color_target = &qs_view,
        .width        = ca_viewport_width(ca_vp),
        .height       = ca_viewport_height(ca_vp),
    };
    s->user_render_fn(&frame, (Qs_Viewport *)ca_vp, s->user_render_data);
}

static void ca_resize_trampoline(Ca_Viewport *ca_vp, uint32_t w, uint32_t h, void *data)
{
    ViewportCallbackState *s = data;
    if (!s->user_resize_fn) return;   /* cleared during renderer destroy / reload */
    s->user_resize_fn((Qs_Viewport *)ca_vp, w, h, s->user_resize_data);
}

/* ================================================================
   GPU CONTEXT
   ================================================================ */

Qs_GpuContext *qs_engine_gpu(Qs_Engine *engine)
{
    return (Qs_GpuContext *)qs_engine_ca_instance(engine);
}

uint32_t qs_gpu_max_sample_count(Qs_GpuContext *gpu)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ca_gpu_physical_device(to_ca(gpu)), &props);
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts
                              & props.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_8_BIT)  return 8;
    if (counts & VK_SAMPLE_COUNT_4_BIT)  return 4;
    if (counts & VK_SAMPLE_COUNT_2_BIT)  return 2;
    return 1;
}

/* ================================================================
   BUFFER IMPLEMENTATION
   ================================================================ */

/* Derive the GPU memory tag from a Qs_GpuBufferUsage bitmask (highest-priority wins). */
static Qs_GpuMemTag buf_usage_to_gpu_tag(Qs_GpuBufferUsage usage)
{
    if (usage & QS_GPU_BUFFER_VERTEX)  return QS_GPU_MEM_VERTEX;
    if (usage & QS_GPU_BUFFER_INDEX)   return QS_GPU_MEM_INDEX;
    if (usage & QS_GPU_BUFFER_UNIFORM) return QS_GPU_MEM_UNIFORM;
    if (usage & QS_GPU_BUFFER_STORAGE) return QS_GPU_MEM_STORAGE;
    return QS_GPU_MEM_OTHER;
}

static VkBufferUsageFlags buffer_usage_to_vk(Qs_GpuBufferUsage usage)
{
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (usage & QS_GPU_BUFFER_VERTEX)   flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (usage & QS_GPU_BUFFER_INDEX)    flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (usage & QS_GPU_BUFFER_UNIFORM)  flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (usage & QS_GPU_BUFFER_STORAGE)  flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (usage & QS_GPU_BUFFER_TRANSFER) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    return flags;
}

Qs_GpuBuffer *qs_gpu_create_buffer(Qs_GpuContext *gpu, const Qs_GpuBufferDesc *desc)
{
    Ca_Instance    *ca     = to_ca(gpu);
    VkDevice        device = ca_gpu_device(ca);
    VkPhysicalDevice pd    = ca_gpu_physical_device(ca);

    VkBufferUsageFlags usage = buffer_usage_to_vk(desc->usage);

    VkMemoryPropertyFlags mem_flags = (desc->memory == QS_GPU_MEMORY_HOST_VISIBLE)
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return create_buffer_raw(device, pd, (VkDeviceSize)desc->size, usage, mem_flags,
                             buf_usage_to_gpu_tag(desc->usage));
}

Qs_GpuBuffer *qs_gpu_create_buffer_from_data(Qs_GpuContext *gpu, Qs_GpuBufferUsage usage,
                                              const void *data, uint64_t size)
{
    Ca_Instance    *ca     = to_ca(gpu);
    VkDevice        device = ca_gpu_device(ca);
    VkPhysicalDevice pd    = ca_gpu_physical_device(ca);

    /* Staging buffer — transient, tagged OTHER */
    Qs_GpuBuffer *staging = create_buffer_raw(device, pd, (VkDeviceSize)size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        QS_GPU_MEM_OTHER);
    if (!staging) return NULL;

    void *mapped;
    vkMapMemory(device, staging->memory, 0, (VkDeviceSize)size, 0, &mapped);
    memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(device, staging->memory);

    /* Device-local destination — tagged by actual usage */
    VkBufferUsageFlags vk_usage = buffer_usage_to_vk(usage);

    Qs_GpuBuffer *dst = create_buffer_raw(device, pd, (VkDeviceSize)size,
        vk_usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buf_usage_to_gpu_tag(usage));
    if (!dst) {
        vkDestroyBuffer(device, staging->buffer, NULL);
        vkFreeMemory(device, staging->memory, NULL);
        qs_free(staging);
        return NULL;
    }

    /* Transfer */
    Qs_GpuCmd qs_cmd;
    begin_transfer_internal(ca, &qs_cmd);
    VkBufferCopy region = { .size = (VkDeviceSize)size };
    vkCmdCopyBuffer(qs_cmd.cmd, staging->buffer, dst->buffer, 1, &region);
    end_transfer_internal(ca, &qs_cmd);

    atomic_fetch_sub(&g_vram_host_bytes, (size_t)staging->vram_size);
    atomic_fetch_sub(&g_gpu_tag_bytes[staging->gpu_tag], (size_t)staging->vram_size);
    vkDestroyBuffer(device, staging->buffer, NULL);
    vkFreeMemory(device, staging->memory, NULL);
    qs_free(staging);
    return dst;
}

void qs_gpu_destroy_buffer(Qs_GpuContext *gpu, Qs_GpuBuffer *buffer)
{
    if (!buffer) return;
    VkDevice device = ca_gpu_device(to_ca(gpu));
    vkDeviceWaitIdle(device);
    if (buffer->device_local) atomic_fetch_sub(&g_vram_device_bytes, (size_t)buffer->vram_size);
    else                      atomic_fetch_sub(&g_vram_host_bytes,   (size_t)buffer->vram_size);
    atomic_fetch_sub(&g_gpu_tag_bytes[buffer->gpu_tag], (size_t)buffer->vram_size);
    if (buffer->buffer) vkDestroyBuffer(device, buffer->buffer, NULL);
    if (buffer->memory) vkFreeMemory(device, buffer->memory, NULL);
    qs_free(buffer);
}

void *qs_gpu_map_buffer(Qs_GpuContext *gpu, Qs_GpuBuffer *buffer)
{
    if (!buffer) return NULL;
    void *mapped = NULL;
    if (vkMapMemory(ca_gpu_device(to_ca(gpu)), buffer->memory, 0, (VkDeviceSize)buffer->size, 0, &mapped) != VK_SUCCESS)
        return NULL;
    return mapped;
}

void qs_gpu_unmap_buffer(Qs_GpuContext *gpu, Qs_GpuBuffer *buffer)
{
    if (buffer) vkUnmapMemory(ca_gpu_device(to_ca(gpu)), buffer->memory);
}

/* ================================================================
   IMAGE IMPLEMENTATION
   ================================================================ */

Qs_GpuImage *qs_gpu_create_image(Qs_GpuContext *gpu, const Qs_GpuImageDesc *desc)
{
    Ca_Instance    *ca     = to_ca(gpu);
    VkDevice        device = ca_gpu_device(ca);
    VkPhysicalDevice pd    = ca_gpu_physical_device(ca);

    VkFormat vk_fmt = (desc->format == QS_GPU_FORMAT_DEPTH_AUTO)
        ? pick_depth_format(pd)
        : gpu_format_to_vk(desc->format);

    VkImageUsageFlags usage = 0;
    if (desc->usage & QS_GPU_IMAGE_SAMPLED)          usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc->usage & QS_GPU_IMAGE_TRANSFER_SRC)     usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (desc->usage & QS_GPU_IMAGE_TRANSFER_DST)     usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc->usage & QS_GPU_IMAGE_COLOR_ATTACHMENT) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc->usage & QS_GPU_IMAGE_DEPTH_ATTACHMENT) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    uint32_t mip_levels = desc->mip_levels > 0 ? desc->mip_levels : 1;

    VkImageCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = vk_fmt,
        .extent        = { desc->width, desc->height, 1 },
        .mipLevels     = mip_levels,
        .arrayLayers   = 1,
        .samples       = sample_count_to_vk(desc->sample_count),
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = usage,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage vk_image;
    if (vkCreateImage(device, &ci, NULL, &vk_image) != VK_SUCCESS) return NULL;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, vk_image, &req);
    uint32_t mi = find_memory_type(pd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mi == UINT32_MAX) { vkDestroyImage(device, vk_image, NULL); return NULL; }

    VkMemoryAllocateInfo ai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = req.size,
        .memoryTypeIndex = mi,
    };
    VkDeviceMemory vk_mem;
    if (vkAllocateMemory(device, &ai, NULL, &vk_mem) != VK_SUCCESS) {
        vkDestroyImage(device, vk_image, NULL); return NULL;
    }
    /* Derive image tag from usage flags */
    Qs_GpuMemTag img_tag;
    if (desc->usage & QS_GPU_IMAGE_DEPTH_ATTACHMENT)  img_tag = QS_GPU_MEM_RT_DEPTH;
    else if (desc->usage & QS_GPU_IMAGE_COLOR_ATTACHMENT) img_tag = QS_GPU_MEM_RT_COLOR;
    else img_tag = QS_GPU_MEM_TEXTURE;

    vkBindImageMemory(device, vk_image, vk_mem, 0);
    atomic_fetch_add(&g_vram_device_bytes, (size_t)req.size);
    atomic_fetch_add(&g_gpu_tag_bytes[img_tag], (size_t)req.size);

    Qs_GpuImage *img = qs_calloc(1, sizeof(Qs_GpuImage), QS_MEM_GPU);
    if (!img) {
        vkDestroyImage(device, vk_image, NULL);
        vkFreeMemory(device, vk_mem, NULL);
        atomic_fetch_sub(&g_vram_device_bytes, (size_t)req.size);
        atomic_fetch_sub(&g_gpu_tag_bytes[img_tag], (size_t)req.size);
        return NULL;
    }
    img->image      = vk_image;
    img->memory     = vk_mem;
    img->width      = desc->width;
    img->height     = desc->height;
    img->mip_levels = mip_levels;
    img->format     = vk_fmt;
    img->vram_size  = (uint64_t)req.size;
    img->gpu_tag    = img_tag;
    return img;
}

void qs_gpu_destroy_image(Qs_GpuContext *gpu, Qs_GpuImage *image)
{
    if (!image) return;
    VkDevice device = ca_gpu_device(to_ca(gpu));
    vkDeviceWaitIdle(device);
    atomic_fetch_sub(&g_vram_device_bytes, (size_t)image->vram_size);
    atomic_fetch_sub(&g_gpu_tag_bytes[image->gpu_tag], (size_t)image->vram_size);
    if (image->image)  vkDestroyImage(device, image->image, NULL);
    if (image->memory) vkFreeMemory(device, image->memory, NULL);
    qs_free(image);
}

bool qs_gpu_upload_image(Qs_GpuContext *gpu, Qs_GpuImage *image,
                         const void *pixels, uint64_t size, bool generate_mips)
{
    if (!image || !pixels) return false;
    Ca_Instance    *ca     = to_ca(gpu);
    VkDevice        device = ca_gpu_device(ca);
    VkPhysicalDevice pd    = ca_gpu_physical_device(ca);

    /* Staging — transient, tagged OTHER */
    Qs_GpuBuffer *staging = create_buffer_raw(device, pd, (VkDeviceSize)size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        QS_GPU_MEM_OTHER);
    if (!staging) return false;

    void *mapped;
    vkMapMemory(device, staging->memory, 0, (VkDeviceSize)size, 0, &mapped);
    memcpy(mapped, pixels, (size_t)size);
    vkUnmapMemory(device, staging->memory);

    Qs_GpuCmd qs_cmd;
    begin_transfer_internal(ca, &qs_cmd);

    /* UNDEFINED → TRANSFER_DST for all mips */
    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = 0,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image->image,
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, image->mip_levels, 0, 1 },
    };
    vkCmdPipelineBarrier(qs_cmd.cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    VkBufferImageCopy region = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent      = { image->width, image->height, 1 },
    };
    vkCmdCopyBufferToImage(qs_cmd.cmd, staging->buffer, image->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if (generate_mips && image->mip_levels > 1) {
        generate_mipmaps_internal(&qs_cmd, image);
    } else {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.subresourceRange.levelCount = image->mip_levels;
        vkCmdPipelineBarrier(qs_cmd.cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &barrier);
    }

    end_transfer_internal(ca, &qs_cmd);

    atomic_fetch_sub(&g_vram_host_bytes, (size_t)staging->vram_size);
    atomic_fetch_sub(&g_gpu_tag_bytes[staging->gpu_tag], (size_t)staging->vram_size);
    vkDestroyBuffer(device, staging->buffer, NULL);
    vkFreeMemory(device, staging->memory, NULL);
    qs_free(staging);
    return true;
}

Qs_GpuImageView *qs_gpu_create_image_view(Qs_GpuContext *gpu,
                                           const Qs_GpuImageViewDesc *desc)
{
    VkDevice device = ca_gpu_device(to_ca(gpu));

    VkFormat vk_fmt = (desc->format == QS_GPU_FORMAT_DEPTH_AUTO)
        ? desc->image->format
        : gpu_format_to_vk(desc->format);

    VkImageViewCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = desc->image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = vk_fmt,
        .subresourceRange = {
            .aspectMask     = aspect_to_vk(desc->aspect),
            .baseMipLevel   = desc->base_mip,
            .levelCount     = desc->mip_levels,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    VkImageView vk_view;
    if (vkCreateImageView(device, &ci, NULL, &vk_view) != VK_SUCCESS) return NULL;

    Qs_GpuImageView *view = qs_calloc(1, sizeof(Qs_GpuImageView), QS_MEM_GPU);
    if (!view) { vkDestroyImageView(device, vk_view, NULL); return NULL; }
    view->view = vk_view;
    return view;
}

void qs_gpu_destroy_image_view(Qs_GpuContext *gpu, Qs_GpuImageView *view)
{
    if (!view) return;
    vkDestroyImageView(ca_gpu_device(to_ca(gpu)), view->view, NULL);
    qs_free(view);
}

Qs_GpuImageView *qs_gpu_create_image_view_for(Qs_GpuContext *gpu, Qs_GpuImage *image,
                                               Qs_GpuImageAspect aspect)
{
    VkDevice device = ca_gpu_device(to_ca(gpu));
    VkImageViewCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = image->format,
        .subresourceRange = {
            .aspectMask     = aspect_to_vk(aspect),
            .baseMipLevel   = 0,
            .levelCount     = image->mip_levels,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    VkImageView vk_view;
    if (vkCreateImageView(device, &ci, NULL, &vk_view) != VK_SUCCESS) return NULL;
    Qs_GpuImageView *view = qs_calloc(1, sizeof(Qs_GpuImageView), QS_MEM_GPU);
    if (!view) { vkDestroyImageView(device, vk_view, NULL); return NULL; }
    view->view = vk_view;
    return view;
}

Qs_GpuSampler *qs_gpu_create_sampler(Qs_GpuContext *gpu, const Qs_GpuSamplerDesc *desc)
{
    VkDevice device = ca_gpu_device(to_ca(gpu));

    static const VkFilter filters[] = { VK_FILTER_NEAREST, VK_FILTER_LINEAR };
    static const VkSamplerAddressMode wraps[] = {
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };

    uint32_t mip_levels = desc->mip_levels > 0 ? desc->mip_levels : 1;

    uint32_t mag = (uint32_t)desc->mag_filter;
    uint32_t min = (uint32_t)desc->min_filter;
    uint32_t wu  = (uint32_t)desc->wrap_u;
    uint32_t wv  = (uint32_t)desc->wrap_v;
    if (mag >= sizeof(filters)/sizeof(filters[0])) mag = 0;
    if (min >= sizeof(filters)/sizeof(filters[0])) min = 0;
    if (wu  >= sizeof(wraps)/sizeof(wraps[0]))     wu  = 0;
    if (wv  >= sizeof(wraps)/sizeof(wraps[0]))     wv  = 0;

    VkSamplerCreateInfo ci = {
        .sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter        = filters[mag],
        .minFilter        = filters[min],
        .mipmapMode       = (mip_levels > 1) ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                              : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU     = wraps[wu],
        .addressModeV     = wraps[wv],
        .addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = desc->anisotropy ? VK_TRUE : VK_FALSE,
        .maxAnisotropy    = desc->anisotropy ? 16.0f : 1.0f,
        .minLod           = 0.0f,
        .maxLod           = (float)mip_levels,
    };

    VkSampler vk_sampler;
    if (vkCreateSampler(device, &ci, NULL, &vk_sampler) != VK_SUCCESS) return NULL;

    Qs_GpuSampler *sampler = qs_calloc(1, sizeof(Qs_GpuSampler), QS_MEM_GPU);
    if (!sampler) { vkDestroySampler(device, vk_sampler, NULL); return NULL; }
    sampler->sampler = vk_sampler;
    return sampler;
}

void qs_gpu_destroy_sampler(Qs_GpuContext *gpu, Qs_GpuSampler *sampler)
{
    if (!sampler) return;
    vkDestroySampler(ca_gpu_device(to_ca(gpu)), sampler->sampler, NULL);
    qs_free(sampler);
}

/* ================================================================
   SHADER IMPLEMENTATION
   ================================================================ */

Qs_GpuShader *qs_gpu_compile_shader(Qs_GpuContext *gpu, const char *glsl_source,
                                     Qs_GpuShaderStage stage)
{
    Ca_Instance *ca   = to_ca(gpu);
    VkDevice device   = ca_gpu_device(ca);
    VkShaderModule mod = ca_shader_compile(device, glsl_source,
                                            gpu_stage_to_vk_single(stage));
    if (!mod) return NULL;

    Qs_GpuShader *shader = qs_calloc(1, sizeof(Qs_GpuShader), QS_MEM_GPU);
    if (!shader) { vkDestroyShaderModule(device, mod, NULL); return NULL; }
    shader->module = mod;
    return shader;
}

void qs_gpu_destroy_shader(Qs_GpuContext *gpu, Qs_GpuShader *shader)
{
    if (!shader) return;
    vkDestroyShaderModule(ca_gpu_device(to_ca(gpu)), shader->module, NULL);
    qs_free(shader);
}

/* ================================================================
   DESCRIPTOR IMPLEMENTATION
   ================================================================ */

Qs_GpuDescriptorSetLayout *qs_gpu_create_descriptor_set_layout(
    Qs_GpuContext *gpu, const Qs_GpuDescriptorBinding *bindings, uint32_t count)
{
    VkDevice device = ca_gpu_device(to_ca(gpu));

    VkDescriptorSetLayoutBinding *vk_bindings =
        qs_calloc(count, sizeof(VkDescriptorSetLayoutBinding), QS_MEM_GPU);
    if (!vk_bindings) return NULL;

    for (uint32_t i = 0; i < count; i++) {
        vk_bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding         = bindings[i].binding,
            .descriptorType  = gpu_descriptor_type_to_vk(bindings[i].type),
            .descriptorCount = bindings[i].count,
            .stageFlags      = gpu_stages_to_vk(bindings[i].stages),
        };
    }

    VkDescriptorSetLayoutCreateInfo ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = count,
        .pBindings    = vk_bindings,
    };
    VkDescriptorSetLayout vk_layout;
    VkResult result = vkCreateDescriptorSetLayout(device, &ci, NULL, &vk_layout);
    qs_free(vk_bindings);
    if (result != VK_SUCCESS) return NULL;

    Qs_GpuDescriptorSetLayout *layout = qs_calloc(1, sizeof(Qs_GpuDescriptorSetLayout), QS_MEM_GPU);
    if (!layout) { vkDestroyDescriptorSetLayout(device, vk_layout, NULL); return NULL; }
    layout->layout = vk_layout;
    return layout;
}

void qs_gpu_destroy_descriptor_set_layout(Qs_GpuContext *gpu,
                                           Qs_GpuDescriptorSetLayout *layout)
{
    if (!layout) return;
    vkDestroyDescriptorSetLayout(ca_gpu_device(to_ca(gpu)), layout->layout, NULL);
    qs_free(layout);
}

Qs_GpuDescriptorPool *qs_gpu_create_descriptor_pool(Qs_GpuContext *gpu,
                                                      const Qs_GpuDescriptorPoolDesc *desc)
{
    VkDevice device = ca_gpu_device(to_ca(gpu));

    VkDescriptorPoolSize *vk_sizes =
        qs_calloc(desc->size_count, sizeof(VkDescriptorPoolSize), QS_MEM_GPU);
    if (!vk_sizes) return NULL;

    for (uint32_t i = 0; i < desc->size_count; i++) {
        vk_sizes[i] = (VkDescriptorPoolSize){
            .type            = gpu_descriptor_type_to_vk(desc->sizes[i].type),
            .descriptorCount = desc->sizes[i].count,
        };
    }

    VkDescriptorPoolCreateInfo ci = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets       = desc->max_sets,
        .poolSizeCount = desc->size_count,
        .pPoolSizes    = vk_sizes,
    };
    VkDescriptorPool vk_pool;
    VkResult result = vkCreateDescriptorPool(device, &ci, NULL, &vk_pool);
    qs_free(vk_sizes);
    if (result != VK_SUCCESS) return NULL;

    Qs_GpuDescriptorPool *pool = qs_calloc(1, sizeof(Qs_GpuDescriptorPool), QS_MEM_GPU);
    if (!pool) { vkDestroyDescriptorPool(device, vk_pool, NULL); return NULL; }
    pool->pool = vk_pool;
    return pool;
}

void qs_gpu_destroy_descriptor_pool(Qs_GpuContext *gpu, Qs_GpuDescriptorPool *pool)
{
    if (!pool) return;
    vkDestroyDescriptorPool(ca_gpu_device(to_ca(gpu)), pool->pool, NULL);
    qs_free(pool);
}

Qs_GpuDescriptorSet *qs_gpu_alloc_descriptor_set(Qs_GpuContext *gpu,
                                                   Qs_GpuDescriptorPool *pool,
                                                   Qs_GpuDescriptorSetLayout *layout)
{
    VkDevice device = ca_gpu_device(to_ca(gpu));
    VkDescriptorSetAllocateInfo ai = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = pool->pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &layout->layout,
    };
    VkDescriptorSet vk_set;
    if (vkAllocateDescriptorSets(device, &ai, &vk_set) != VK_SUCCESS) return NULL;

    Qs_GpuDescriptorSet *set = qs_calloc(1, sizeof(Qs_GpuDescriptorSet), QS_MEM_GPU);
    if (!set) return NULL;
    set->set = vk_set;
    return set;
}

void qs_gpu_free_descriptor_set(Qs_GpuContext *gpu, Qs_GpuDescriptorPool *pool,
                                 Qs_GpuDescriptorSet *set)
{
    if (!set) return;
    vkFreeDescriptorSets(ca_gpu_device(to_ca(gpu)), pool->pool, 1, &set->set);
    qs_free(set);
}

void qs_gpu_write_image_descriptor(Qs_GpuContext *gpu, Qs_GpuDescriptorSet *set,
                                    uint32_t binding,
                                    Qs_GpuSampler *sampler, Qs_GpuImageView *view)
{
    VkDescriptorImageInfo img_info = {
        .sampler     = sampler->sampler,
        .imageView   = view->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = set->set,
        .dstBinding      = binding,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &img_info,
    };
    vkUpdateDescriptorSets(ca_gpu_device(to_ca(gpu)), 1, &write, 0, NULL);
}

void qs_gpu_write_buffer_descriptor(Qs_GpuContext *gpu, Qs_GpuDescriptorSet *set,
                                     uint32_t binding, Qs_GpuBuffer *buffer,
                                     uint64_t offset, uint64_t range)
{
    VkDescriptorBufferInfo buf_info = {
        .buffer = buffer->buffer,
        .offset = (VkDeviceSize)offset,
        .range  = (VkDeviceSize)(range == 0 ? buffer->size : range),
    };
    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = set->set,
        .dstBinding      = binding,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo     = &buf_info,
    };
    vkUpdateDescriptorSets(ca_gpu_device(to_ca(gpu)), 1, &write, 0, NULL);
}

/* ================================================================
   PIPELINE IMPLEMENTATION
   ================================================================ */

Qs_GpuPipelineLayout *qs_gpu_create_pipeline_layout(Qs_GpuContext *gpu,
                                                      const Qs_GpuPipelineLayoutDesc *desc)
{
    VkDevice device = ca_gpu_device(to_ca(gpu));

    VkDescriptorSetLayout *vk_layouts = NULL;
    if (desc->set_layout_count > 0) {
        vk_layouts = qs_calloc(desc->set_layout_count, sizeof(VkDescriptorSetLayout), QS_MEM_GPU);
        if (!vk_layouts) return NULL;
        for (uint32_t i = 0; i < desc->set_layout_count; i++)
            vk_layouts[i] = desc->set_layouts[i]->layout;
    }

    VkPushConstantRange *vk_pc = NULL;
    if (desc->push_constant_count > 0) {
        vk_pc = qs_calloc(desc->push_constant_count, sizeof(VkPushConstantRange), QS_MEM_GPU);
        if (!vk_pc) { qs_free(vk_layouts); return NULL; }
        for (uint32_t i = 0; i < desc->push_constant_count; i++) {
            vk_pc[i] = (VkPushConstantRange){
                .stageFlags = gpu_stages_to_vk(desc->push_constants[i].stages),
                .offset     = desc->push_constants[i].offset,
                .size       = desc->push_constants[i].size,
            };
        }
    }

    VkPipelineLayoutCreateInfo ci = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = desc->set_layout_count,
        .pSetLayouts            = vk_layouts,
        .pushConstantRangeCount = desc->push_constant_count,
        .pPushConstantRanges    = vk_pc,
    };
    VkPipelineLayout vk_layout;
    VkResult result = vkCreatePipelineLayout(device, &ci, NULL, &vk_layout);
    qs_free(vk_layouts);
    qs_free(vk_pc);
    if (result != VK_SUCCESS) return NULL;

    Qs_GpuPipelineLayout *layout = qs_calloc(1, sizeof(Qs_GpuPipelineLayout), QS_MEM_GPU);
    if (!layout) { vkDestroyPipelineLayout(device, vk_layout, NULL); return NULL; }
    layout->layout = vk_layout;
    return layout;
}

void qs_gpu_destroy_pipeline_layout(Qs_GpuContext *gpu, Qs_GpuPipelineLayout *layout)
{
    if (!layout) return;
    vkDestroyPipelineLayout(ca_gpu_device(to_ca(gpu)), layout->layout, NULL);
    qs_free(layout);
}

Qs_GpuPipeline *qs_gpu_create_graphics_pipeline(Qs_GpuContext *gpu,
                                                  const Qs_GpuGraphicsPipelineDesc *desc)
{
    Ca_Instance    *ca     = to_ca(gpu);
    VkDevice        device = ca_gpu_device(ca);
    VkPhysicalDevice pd    = ca_gpu_physical_device(ca);

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = desc->vertex_shader->module,
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = desc->fragment_shader->module,
            .pName  = "main",
        },
    };

    /* Vertex input */
    uint32_t total_attrs = 0;
    for (uint32_t b = 0; b < desc->vertex_binding_count; b++)
        total_attrs += desc->vertex_bindings[b].attribute_count;

    VkVertexInputBindingDescription *vk_bindings = NULL;
    VkVertexInputAttributeDescription *vk_attrs  = NULL;
    if (desc->vertex_binding_count > 0) {
        vk_bindings = qs_calloc(desc->vertex_binding_count, sizeof(VkVertexInputBindingDescription), QS_MEM_GPU);
        if (!vk_bindings) return NULL;
    }
    if (total_attrs > 0) {
        vk_attrs = qs_calloc(total_attrs, sizeof(VkVertexInputAttributeDescription), QS_MEM_GPU);
        if (!vk_attrs) { qs_free(vk_bindings); return NULL; }
    }

    uint32_t attr_idx = 0;
    for (uint32_t b = 0; b < desc->vertex_binding_count; b++) {
        vk_bindings[b] = (VkVertexInputBindingDescription){
            .binding   = desc->vertex_bindings[b].binding,
            .stride    = desc->vertex_bindings[b].stride,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        for (uint32_t a = 0; a < desc->vertex_bindings[b].attribute_count; a++) {
            const Qs_GpuVertexAttribute *attr = &desc->vertex_bindings[b].attributes[a];
            vk_attrs[attr_idx++] = (VkVertexInputAttributeDescription){
                .location = attr->location,
                .binding  = desc->vertex_bindings[b].binding,
                .format   = gpu_vertex_format_to_vk(attr->format),
                .offset   = attr->offset,
            };
        }
    }

    VkPipelineVertexInputStateCreateInfo vert_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = desc->vertex_binding_count,
        .pVertexBindingDescriptions      = vk_bindings,
        .vertexAttributeDescriptionCount = total_attrs,
        .pVertexAttributeDescriptions    = vk_attrs,
    };

    static const VkPrimitiveTopology topologies[] = {
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topologies[desc->topology],
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates    = dyn_states,
    };

    static const VkCullModeFlags cull_modes[] = {
        VK_CULL_MODE_NONE, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_BIT,
    };
    VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = desc->wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
        .cullMode    = cull_modes[desc->cull_mode],
        .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth   = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = sample_count_to_vk(desc->sample_count),
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = desc->depth_test  ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = desc->depth_write ? VK_TRUE : VK_FALSE,
        .depthCompareOp   = VK_COMPARE_OP_LESS,
    };

    VkPipelineColorBlendAttachmentState blend_att = {
        .blendEnable    = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkFormat color_vk = gpu_format_to_vk(desc->color_format);
    VkFormat depth_vk = (desc->depth_format == QS_GPU_FORMAT_DEPTH_AUTO)
        ? (desc->depth_test ? pick_depth_format(pd) : VK_FORMAT_UNDEFINED)
        : gpu_format_to_vk(desc->depth_format);

    bool has_color = (desc->color_format != QS_GPU_FORMAT_NONE) &&
                     (color_vk != VK_FORMAT_UNDEFINED);

    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = has_color ? 1 : 0,
        .pAttachments    = has_color ? &blend_att : NULL,
    };

    VkPipelineRenderingCreateInfo rendering_ci = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount    = has_color ? 1 : 0,
        .pColorAttachmentFormats = has_color ? &color_vk : NULL,
        .depthAttachmentFormat   = depth_vk,
    };

    VkGraphicsPipelineCreateInfo pipeline_ci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = &rendering_ci,
        .stageCount          = 2,
        .pStages             = stages,
        .pVertexInputState   = &vert_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState   = &multisample,
        .pDepthStencilState  = &depth_stencil,
        .pColorBlendState    = &color_blend,
        .pDynamicState       = &dynamic_state,
        .layout              = desc->layout->layout,
        .renderPass          = VK_NULL_HANDLE,
    };

    VkPipeline vk_pipeline;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE,
                                                1, &pipeline_ci, NULL,
                                                &vk_pipeline);
    qs_free(vk_bindings);
    qs_free(vk_attrs);
    if (result != VK_SUCCESS) return NULL;

    Qs_GpuPipeline *pipeline = qs_calloc(1, sizeof(Qs_GpuPipeline), QS_MEM_GPU);
    if (!pipeline) { vkDestroyPipeline(device, vk_pipeline, NULL); return NULL; }
    pipeline->pipeline = vk_pipeline;
    return pipeline;
}

void qs_gpu_destroy_pipeline(Qs_GpuContext *gpu, Qs_GpuPipeline *pipeline)
{
    if (!pipeline) return;
    vkDestroyPipeline(ca_gpu_device(to_ca(gpu)), pipeline->pipeline, NULL);
    qs_free(pipeline);
}

/* ================================================================
   COMMAND RECORDING IMPLEMENTATION
   ================================================================ */

Qs_GpuCmd *qs_gpu_begin_transfer(Qs_GpuContext *gpu)
{
    Qs_GpuCmd *cmd = qs_calloc(1, sizeof(Qs_GpuCmd), QS_MEM_GPU);
    if (!cmd) return NULL;
    cmd->cmd = ca_gpu_begin_transfer(to_ca(gpu));
    return cmd;
}

void qs_gpu_end_transfer(Qs_GpuContext *gpu, Qs_GpuCmd *cmd)
{
    if (!cmd) return;
    ca_gpu_end_transfer(to_ca(gpu), cmd->cmd);
    qs_free(cmd);
}

void qs_cmd_begin_rendering(Qs_GpuCmd *cmd, const Qs_GpuRenderTarget *target)
{
    VkRenderingAttachmentInfo color_att = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = target->color ? target->color->view : VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = target->load_color ? VK_ATTACHMENT_LOAD_OP_LOAD
                                          : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = { .color = { .float32 = {
            target->clear_color[0], target->clear_color[1],
            target->clear_color[2], target->clear_color[3],
        }}},
    };
    if (target->resolve_target) {
        color_att.resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT;
        color_att.resolveImageView   = target->resolve_target->view;
        color_att.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo depth_att = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = target->depth ? target->depth->view : VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp      = target->load_depth ? VK_ATTACHMENT_LOAD_OP_LOAD
                                          : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = { .depthStencil = { target->clear_depth, 0 } },
    };
    VkRenderingInfo ri = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = { .offset = {0, 0}, .extent = { target->width, target->height } },
        .layerCount           = 1,
        .colorAttachmentCount = target->color ? 1 : 0,
        .pColorAttachments    = target->color ? &color_att : NULL,
        .pDepthAttachment     = target->depth ? &depth_att : NULL,
    };
    vkCmdBeginRendering(cmd->cmd, &ri);
}

void qs_cmd_end_rendering(Qs_GpuCmd *cmd)
{
    vkCmdEndRendering(cmd->cmd);
}

void qs_cmd_set_viewport(Qs_GpuCmd *cmd, uint32_t width, uint32_t height)
{
    VkViewport vp = { .x=0,.y=0,.width=(float)width,.height=(float)height,.minDepth=0.0f,.maxDepth=1.0f };
    vkCmdSetViewport(cmd->cmd, 0, 1, &vp);
    VkRect2D sc = { .offset={0,0}, .extent={width,height} };
    vkCmdSetScissor(cmd->cmd, 0, 1, &sc);
}

void qs_cmd_bind_pipeline(Qs_GpuCmd *cmd, Qs_GpuPipeline *pipeline)
{
    vkCmdBindPipeline(cmd->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

void qs_cmd_bind_descriptor_set(Qs_GpuCmd *cmd, Qs_GpuPipelineLayout *layout,
                                 uint32_t set_index, Qs_GpuDescriptorSet *set)
{
    vkCmdBindDescriptorSets(cmd->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout->layout, set_index, 1, &set->set, 0, NULL);
}

void qs_cmd_push_constants(Qs_GpuCmd *cmd, Qs_GpuPipelineLayout *layout,
                            Qs_GpuShaderStage stages, uint32_t offset,
                            uint32_t size, const void *data)
{
    vkCmdPushConstants(cmd->cmd, layout->layout, gpu_stages_to_vk(stages),
                       offset, size, data);
}

void qs_cmd_bind_vertex_buffer(Qs_GpuCmd *cmd, uint32_t binding,
                                Qs_GpuBuffer *buffer, uint64_t offset)
{
    VkDeviceSize off = (VkDeviceSize)offset;
    vkCmdBindVertexBuffers(cmd->cmd, binding, 1, &buffer->buffer, &off);
}

void qs_cmd_bind_index_buffer(Qs_GpuCmd *cmd, Qs_GpuBuffer *buffer, bool use_uint16)
{
    vkCmdBindIndexBuffer(cmd->cmd, buffer->buffer, 0,
                         use_uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void qs_cmd_draw(Qs_GpuCmd *cmd, uint32_t vertex_count, uint32_t first_vertex)
{
    vkCmdDraw(cmd->cmd, vertex_count, 1, first_vertex, 0);
}

void qs_cmd_draw_indexed(Qs_GpuCmd *cmd, uint32_t index_count,
                          uint32_t first_index, int32_t vertex_offset)
{
    vkCmdDrawIndexed(cmd->cmd, index_count, 1, first_index, vertex_offset, 0);
}

void qs_cmd_image_barrier(Qs_GpuCmd *cmd, const Qs_GpuImageBarrier *barrier)
{
    VkImageMemoryBarrier b = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = layout_to_access(barrier->old_layout),
        .dstAccessMask       = layout_to_access(barrier->new_layout),
        .oldLayout           = layout_to_vk(barrier->old_layout),
        .newLayout           = layout_to_vk(barrier->new_layout),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = barrier->image->image,
        .subresourceRange    = {
            .aspectMask     = aspect_to_vk(barrier->aspect),
            .baseMipLevel   = barrier->base_mip,
            .levelCount     = barrier->mip_count,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    vkCmdPipelineBarrier(cmd->cmd,
        layout_to_src_stage(barrier->old_layout),
        layout_to_dst_stage(barrier->new_layout),
        0, 0, NULL, 0, NULL, 1, &b);
}

void qs_cmd_copy_buffer_to_image(Qs_GpuCmd *cmd, Qs_GpuBuffer *src,
                                  Qs_GpuImage *dst, uint32_t width, uint32_t height)
{
    VkBufferImageCopy region = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent      = { width, height, 1 },
    };
    vkCmdCopyBufferToImage(cmd->cmd, src->buffer, dst->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void qs_cmd_copy_image_to_buffer(Qs_GpuCmd *cmd, Qs_GpuImage *src,
                                  Qs_GpuBuffer *dst, uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height)
{
    VkBufferImageCopy region = {
        .bufferOffset     = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset      = { (int32_t)x, (int32_t)y, 0 },
        .imageExtent      = { width, height, 1 },
    };
    vkCmdCopyImageToBuffer(cmd->cmd, src->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->buffer, 1, &region);
}

void qs_cmd_blit_image_mip(Qs_GpuCmd *cmd, Qs_GpuImage *image,
                            uint32_t src_mip, uint32_t src_w, uint32_t src_h,
                            uint32_t dst_mip, uint32_t dst_w, uint32_t dst_h)
{
    VkImageBlit blit = {
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, src_mip, 0, 1 },
        .srcOffsets     = { {0, 0, 0}, {(int32_t)src_w, (int32_t)src_h, 1} },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, dst_mip, 0, 1 },
        .dstOffsets     = { {0, 0, 0}, {(int32_t)dst_w, (int32_t)dst_h, 1} },
    };
    vkCmdBlitImage(cmd->cmd,
        image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_LINEAR);
}

/* ================================================================
   VIEWPORT IMPLEMENTATION
   ================================================================ */

void qs_viewport_set_callbacks(Qs_Viewport *viewport,
                                Qs_ViewportRenderFn on_render, void *render_data,
                                Qs_ViewportResizeFn on_resize, void *resize_data)
{
    if (!on_render && !on_resize) {
        /* Clear our trampoline state without touching the Ca_Viewport.
         * The viewport widget may already have been freed by Causality's
         * window-shutdown sequence before qs_renderer_destroy() runs.
         * Nulling the slot is enough — the null guards in the trampolines
         * prevent any stale call from reaching user code. */
        for (uint32_t i = 0; i < s_vp_state_count; i++) {
            if (s_vp_states[i].viewport == viewport) {
                s_vp_states[i].user_render_fn   = NULL;
                s_vp_states[i].user_render_data = NULL;
                s_vp_states[i].user_resize_fn   = NULL;
                s_vp_states[i].user_resize_data = NULL;
                break;
            }
        }
        return;
    }
    ViewportCallbackState *s = get_or_alloc_vp_state(viewport);
    if (!s) return;
    s->user_render_fn   = on_render;
    s->user_render_data = render_data;
    s->user_resize_fn   = on_resize;
    s->user_resize_data = resize_data;
    ca_viewport_set_callbacks((Ca_Viewport *)viewport,
                               ca_render_trampoline, s,
                               ca_resize_trampoline, s);
}

uint32_t qs_viewport_width(const Qs_Viewport *viewport)
{
    return ca_viewport_width((const Ca_Viewport *)viewport);
}

uint32_t qs_viewport_height(const Qs_Viewport *viewport)
{
    return ca_viewport_height((const Ca_Viewport *)viewport);
}

/* ================================================================
   GPU MEMORY STATS
   ================================================================ */

void qs_gpu_mem_stats(Qs_GpuContext *gpu, Qs_GpuMemStats *out)
{
    if (!out) return;
    out->device_bytes = (size_t)atomic_load(&g_vram_device_bytes);
    out->host_bytes   = (size_t)atomic_load(&g_vram_host_bytes);
    for (int i = 0; i < QS_GPU_MEM_TAG_COUNT; i++)
        out->tag_bytes[i] = (size_t)atomic_load(&g_gpu_tag_bytes[i]);

    /* Total DEVICE_LOCAL heap size — sum all DEVICE_LOCAL memory heaps */
    if (gpu) {
        VkPhysicalDeviceMemoryProperties mem_props;
        vkGetPhysicalDeviceMemoryProperties(ca_gpu_physical_device(to_ca(gpu)), &mem_props);
        size_t total = 0;
        for (uint32_t i = 0; i < mem_props.memoryHeapCount; i++) {
            if (mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                total += (size_t)mem_props.memoryHeaps[i].size;
        }
        out->device_total_bytes = total;
    } else {
        out->device_total_bytes = 0;
    }
}

void qs_gpu_device_name(Qs_GpuContext *gpu, char *buf, size_t len)
{
    if (!buf || len == 0) return;
    if (!gpu) { buf[0] = '\0'; return; }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ca_gpu_physical_device(to_ca(gpu)), &props);
    snprintf(buf, len, "%s", props.deviceName);
}
