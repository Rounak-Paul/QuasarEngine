#pragma once

#include <qspch.h>
#include "VulkanTypes.h"

namespace Quasar
{
void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
} // namespace Quasar
