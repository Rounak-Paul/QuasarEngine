#pragma once

#include <qspch.h>
#include "VulkanTypes.h"
#include "VulkanDevice.h"

namespace Quasar
{
struct VulkanImage {
    VkImage image;
    VkImageView view;
    VmaAllocation allocation;
    VkExtent3D extent;
    VkFormat format;
};

void transition_image(VulkanDevice& device, VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
void copy_image_to_image(VulkanDevice& device, VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);
} // namespace Quasar
