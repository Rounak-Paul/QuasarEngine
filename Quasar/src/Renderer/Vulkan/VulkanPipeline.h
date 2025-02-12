#pragma once

#include <qspch.h>

namespace Quasar
{
    class VulkanBuffer;

    struct VulkanPipelineConfig {
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
        VkBool32 depthTestEnable = VK_TRUE;  // Not used
        VkBool32 depthWriteEnable = VK_TRUE; // Not used
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS; // Not used
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
        VkBool32 enableBlending = VK_FALSE;
    };

    class VulkanPipeline {
    public:
        VulkanPipeline() = default;
        ~VulkanPipeline() = default;

        b8 create(const VulkanContext* context, const VulkanPipelineConfig& config = {});
        void destroy();

        void set_config(const VulkanPipelineConfig& config) { _config = config; }
        const VulkanPipelineConfig& get_config() const { return _config; }

    private:
        const VulkanContext* _context = nullptr;
        VulkanPipelineConfig _config;

        VkPipeline _pipeline = VK_NULL_HANDLE;
        VkPipelineLayout _pipeline_layout = VK_NULL_HANDLE;

        VkDescriptorSetLayout _descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool _descriptor_pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> _descriptor_sets;

        DynamicArray<VulkanBuffer> _uniform_buffers;
        DynamicArray<void*> _uniform_buffers_mapped;

        void descriptor_set_layout_create(VkDevice device);
        void createDescriptorPool(VkDevice device);
        void createDescriptorSets(VkDevice device);
    };
} // namespace Quasar