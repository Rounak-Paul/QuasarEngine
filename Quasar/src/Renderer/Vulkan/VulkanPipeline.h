#pragma once

#include <qspch.h>

namespace Quasar
{
    struct VulkanPipelineConfig {
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
        VkBool32 depthTestEnable = VK_TRUE; // not used
        VkBool32 depthWriteEnable = VK_TRUE; // not used
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS; // not used
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
    };

    class VulkanPipeline {
        public:
        VulkanPipeline() {};
        ~VulkanPipeline() = default;

        b8 create(VkDevice device, VkRenderPass render_pass, const VulkanPipelineConfig &config);
        void destroy(VkDevice device);

        VkPipeline _graphics_pipeline;
        VkPipelineLayout _pipeline_layout;
    };
} // namespace Quasar
