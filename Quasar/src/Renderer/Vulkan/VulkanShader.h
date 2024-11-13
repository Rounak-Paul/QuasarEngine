#pragma once
#include <qspch.h>

#include"VulkanTypes.h"

namespace Quasar::Renderer {
VkShaderModule vulkan_shader_create(VulkanContext* context, String shader_file);
}