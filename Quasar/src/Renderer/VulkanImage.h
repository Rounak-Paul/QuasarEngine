#pragma once

#include <qspch.h>
#include "VulkanTypes.h"
#include "VulkanDevice.h"

namespace Quasar
{
void transition_image(VulkanDevice& device, VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
} // namespace Quasar
