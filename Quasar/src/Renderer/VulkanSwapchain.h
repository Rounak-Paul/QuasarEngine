#pragma once

#include <qspch.h>
#include "VulkanTypes.h"
#include "VulkanDevice.h"

namespace Quasar
{
typedef struct VulkanSwapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
	VkSurfaceFormatKHR image_format;

	std::vector<VkImage> images;
	std::vector<VkImageView> views;
	VkExtent2D extent;

    u32 image_index;
} VulkanSwapchain;

void vulkan_swapchain_create(VulkanDevice& device, VkSurfaceKHR surface, u32 width, u32 height, VulkanSwapchain& swapchain);
void vulkan_swapchain_destroy(VulkanDevice& device, VulkanSwapchain& swapchain);
void vulkan_swapchain_recreate(VulkanDevice& device, VkSurfaceKHR surface, u32 width, u32 height, VulkanSwapchain& swapchain);
} // namespace Quasar
