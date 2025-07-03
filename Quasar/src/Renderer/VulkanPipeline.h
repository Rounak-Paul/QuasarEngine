#pragma once

#include <qspch.h>
#include "VulkanTypes.h"

namespace Quasar
{
struct ComputePipelineConfig {
    VkDevice device;
    const char* shader_path;

    VkDescriptorSetLayout* set_layouts;
    u32 set_layout_count;

    VkPushConstantRange* push_constants = nullptr;
    u32 push_constant_count = 0;

    DeletionQueue* deletion_queue = nullptr;
};

b8 load_shader_module(const std::string& file_path, VkDevice device, VkShaderModule *outShaderModule);
b8 create_compute_pipeline(const ComputePipelineConfig& config, ComputeEffect& out_effect);
} // namespace Quasar
