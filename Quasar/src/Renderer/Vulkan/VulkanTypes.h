#pragma once

#include <qspch.h>
#include <vulkan/vulkan.h>
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanCommmandBuffer.h"

#define MAX_FRAMES_IN_FLIGHT 3
#define IMGUI_UNLIMITED_FRAME_RATE

namespace Quasar
{
typedef struct VulkanPipelineConfig {
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
} VulkanPipelineConfig;

typedef struct VulkanPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets;

    DynamicArray<VulkanBuffer> uniform_buffers;
    DynamicArray<void*> uniform_buffers_mapped;
} VulkanPipeline;

typedef struct VulkanShader {
    DynamicArray<VkShaderModule> shader_modules;
    DynamicArray<VulkanPipeline> pipelines;
    VkDescriptorSetLayout descriptor_set_layout;
} VulkanShader;

typedef struct VulkanContext {

    u8 frame_index = 0;
    
    VkInstance instance;
    VkAllocationCallbacks* allocator;
    VkSurfaceKHR surface;
    VulkanDevice device;

    b8 multithreading_enabled;

    VkDescriptorPool imgui_descriptor_pool;
    VkRenderPass render_pass;
    VkCommandPool command_pool;
    VkSampler texture_sampler;

    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    DynamicArray<VulkanCommandBuffer> command_buffers;

    VkExtent2D extent;
    VkSampleCountFlagBits msaa_samples;
    static const VkFormat image_format = VK_FORMAT_R16G16B16A16_UNORM;

    // Buffers
    VulkanBuffer vertex_buffer;
    VulkanBuffer index_buffer;

    PFN_vkCmdSetPrimitiveTopologyEXT vkCmdSetPrimitiveTopologyEXT;

    // Optional
    VkPipelineCache pipeline_cache;

    u32 find_memory_type(u32 type_filter, u32 prop_flags) const {
        VkPhysicalDeviceMemoryProperties memory_properties;
        vkGetPhysicalDeviceMemoryProperties(device.physical_device, &memory_properties);
        for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
            // Check each memory type to see if its bit is set to 1.
            if (type_filter & (1 << i) && (memory_properties.memoryTypes[i].propertyFlags & prop_flags) == prop_flags) {
                return i;
            }
        }
        LOG_WARN("Unable to find suitable memory type!");
        return -1;
    };
} VulkanContext;
} // namespace Quasar
