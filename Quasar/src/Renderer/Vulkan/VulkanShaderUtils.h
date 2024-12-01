#pragma once
#include <qspch.h>

#include"VulkanTypes.h"

namespace Quasar::Renderer { 
b8 create_shader_module(
    vulkan_context* context,
    const String& name,
    const String& type_str,
    VkShaderStageFlagBits shader_stage_flag,
    u32 stage_index,
    vulkan_shader_stage* shader_stages
);
}