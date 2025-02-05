#pragma once

#include <qspch.h>

namespace Quasar
{
    class VulkanPipeline {
        public:
        VulkanPipeline(const vk::UniqueDevice& device, const vk::PhysicalDevice& physical_device, const vk::UniqueRenderPass& render_pass, const vk::SampleCountFlagBits& msaa_samples);
        ~VulkanPipeline() = default;

        vk::UniquePipeline _graphics_pipeline;
    };
} // namespace Quasar
