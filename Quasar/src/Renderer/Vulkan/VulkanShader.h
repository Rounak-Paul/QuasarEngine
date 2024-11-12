#pragma once
#include <qspch.h>

#include"VulkanTypes.h"

namespace Quasar::Renderer {
VkShaderModule vulkan_shader_create(String shader_file);
}