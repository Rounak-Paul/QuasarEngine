#pragma once

#include <qspch.h>
#include "VulkanTypes.h"

namespace Quasar
{
b8 load_shader_module(const std::string& file_path, VkDevice device, VkShaderModule *outShaderModule);
} // namespace Quasar
