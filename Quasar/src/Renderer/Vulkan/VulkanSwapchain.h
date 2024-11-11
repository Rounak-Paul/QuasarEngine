#pragma once
#include <qspch.h>

#include"VulkanTypes.h"

namespace Quasar::Renderer { 
    b8 vulkan_swapchain_create(
        VulkanContext* context,
        u32 width,
        u32 height,
        VulkanSwapchain* out_swapchain);

    void vulkan_swapchain_recreate(
        VulkanContext* context,
        u32 width,
        u32 height,
        VulkanSwapchain* swapchain);

    void vulkan_swapchain_destroy(
        VulkanContext* context,
        VulkanSwapchain* swapchain);

    b8 vulkan_swapchain_acquire_next_image_index(
        VulkanContext* context,
        VulkanSwapchain* swapchain,
        u64 timeout_ns,
        VkSemaphore image_available_semaphore,
        VkFence fence,
        u32* out_image_index);

    void vulkan_swapchain_present(
        VulkanContext* context,
        VulkanSwapchain* swapchain,
        VkQueue present_queue,
        VkSemaphore render_complete_semaphore,
        u32 present_image_index);
}