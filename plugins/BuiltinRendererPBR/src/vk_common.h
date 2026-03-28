#ifndef VK_COMMON_H
#define VK_COMMON_H

#include "qs_gpu.h"
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
   SHARED VULKAN HELPERS  (plugin-internal)
   ================================================================ */

static inline uint32_t vk_find_memory_type(VkPhysicalDevice pd,
                                            uint32_t type_bits,
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

static inline bool vk_upload_buffer(Qs_GpuContext *gpu,
                                     VkDevice device,
                                     VkPhysicalDevice pd,
                                     VkBufferUsageFlags usage,
                                     const void *data,
                                     VkDeviceSize size,
                                     VkBuffer *out_buf,
                                     VkDeviceMemory *out_mem)
{
    VkBuffer staging;
    VkDeviceMemory staging_mem;

    VkBufferCreateInfo staging_ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device, &staging_ci, NULL, &staging) != VK_SUCCESS)
        return false;

    VkMemoryRequirements sreq;
    vkGetBufferMemoryRequirements(device, staging, &sreq);
    uint32_t smi = vk_find_memory_type(pd, sreq.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (smi == UINT32_MAX) { vkDestroyBuffer(device, staging, NULL); return false; }

    VkMemoryAllocateInfo sai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = sreq.size,
        .memoryTypeIndex = smi,
    };
    if (vkAllocateMemory(device, &sai, NULL, &staging_mem) != VK_SUCCESS) {
        vkDestroyBuffer(device, staging, NULL); return false;
    }
    vkBindBufferMemory(device, staging, staging_mem, 0);

    void *mapped;
    vkMapMemory(device, staging_mem, 0, size, 0, &mapped);
    memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(device, staging_mem);

    VkBufferCreateInfo buf_ci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device, &buf_ci, NULL, out_buf) != VK_SUCCESS) {
        vkDestroyBuffer(device, staging, NULL);
        vkFreeMemory(device, staging_mem, NULL);
        return false;
    }

    VkMemoryRequirements breq;
    vkGetBufferMemoryRequirements(device, *out_buf, &breq);
    uint32_t bmi = vk_find_memory_type(pd, breq.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (bmi == UINT32_MAX) {
        vkDestroyBuffer(device, *out_buf, NULL);
        vkDestroyBuffer(device, staging, NULL);
        vkFreeMemory(device, staging_mem, NULL);
        return false;
    }

    VkMemoryAllocateInfo bai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = breq.size,
        .memoryTypeIndex = bmi,
    };
    if (vkAllocateMemory(device, &bai, NULL, out_mem) != VK_SUCCESS) {
        vkDestroyBuffer(device, *out_buf, NULL);
        vkDestroyBuffer(device, staging, NULL);
        vkFreeMemory(device, staging_mem, NULL);
        return false;
    }
    vkBindBufferMemory(device, *out_buf, *out_mem, 0);

    VkCommandBuffer cmd = qs_gpu_begin_transfer(gpu);
    VkBufferCopy copy = { .size = size };
    vkCmdCopyBuffer(cmd, staging, *out_buf, 1, &copy);
    qs_gpu_end_transfer(gpu, cmd);

    vkDestroyBuffer(device, staging, NULL);
    vkFreeMemory(device, staging_mem, NULL);
    return true;
}

/* Forward declarations for inter-module helpers within the plugin. */
struct Qs_Light;
typedef struct Qs_LightGPU Qs_LightGPU;
bool vk_light_is_active(const struct Qs_Light *l);
void vk_light_pack_gpu(const struct Qs_Light *l, Qs_LightGPU *out);

bool vk_forward_init_impl(struct Qs_Engine *engine, struct Qs_Renderer *renderer, void *ctx);
void vk_forward_shutdown_impl(void *ctx);

#endif /* VK_COMMON_H */
