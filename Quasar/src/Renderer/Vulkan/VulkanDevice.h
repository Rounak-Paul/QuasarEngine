#pragma once
#include <qspch.h>

#include"VulkanTypes.h"

namespace Quasar::Renderer {
    b8 vulkan_device_create(VulkanContext* context);
    void vulkan_device_destroy(VulkanContext* context);
    void vulkan_device_query_swapchain_support(VkPhysicalDevice physical_device, VkSurfaceKHR surface, vulkan_swapchain_support_info* out_support_info);
    b8 vulkan_device_detect_depth_format(VulkanDevice* device);
}