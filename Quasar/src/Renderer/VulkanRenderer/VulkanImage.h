#pragma once
#include <qspch.h>

namespace Quasar::Vulkan
{
    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout current_layout, VkImageLayout new_layout);
    void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);
} // namespace Quasar::Vulkan
