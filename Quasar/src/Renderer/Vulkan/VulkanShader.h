#pragma once
#include <qspch.h>

#include"VulkanTypes.h"

namespace Quasar::Renderer {
VkShaderModule vulkan_shader_module_create(VulkanContext* context, String shader_file);
void vulkan_shader_module_destroy(VulkanContext* context, VkShaderModule module);
}