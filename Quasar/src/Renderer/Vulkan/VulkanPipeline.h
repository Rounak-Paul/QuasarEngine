#pragma once

#include <qspch.h>

namespace Quasar
{
    class VulkanPipeline {
        public:
        VulkanPipeline() {};
        ~VulkanPipeline() = default;

        b8 create(VkDevice device, VkRenderPass render_pass, VkSampleCountFlagBits msaa_samples);
        void destroy(VkDevice device);

        VkPipeline _graphics_pipeline;
        VkPipelineLayout _pipeline_layout;
    };
} // namespace Quasar
