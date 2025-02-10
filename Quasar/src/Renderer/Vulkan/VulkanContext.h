#pragma once

#include <qspch.h>
#include "VulkanDevice.h"
#include "VulkanPipeline.h"
#include "VulkanCommmandBuffer.h"
#include "VulkanBuffer.h"

#define MAX_FRAMES_IN_FLIGHT 3
#define IMGUI_UNLIMITED_FRAME_RATE

namespace Quasar {

struct VulkanContext {
    VulkanContext() {};
    ~VulkanContext() = default;

    b8 create(GLFWwindow* window);
    void destroy();

    u8 _frame_index = 0;
    VkInstance _instance;
    VkAllocationCallbacks* _allocator;
    VkSurfaceKHR _surface;
    VulkanDevice _device;

    b8 _multithreading_enabled;

    VkDescriptorPool _descriptor_pool;
    VkRenderPass _render_pass;
    VulkanPipeline _pipeline;
    VkCommandPool _command_pool;
    VkSampler _texture_sampler;

    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    DynamicArray<VulkanCommandBuffer> _command_buffers;

    VkExtent2D _extent;
    VkSampleCountFlagBits _msaa_samples;
    static const VkFormat _image_format = VK_FORMAT_R16G16B16A16_UNORM;

    // Buffers
    VulkanBuffer vertex_buffer;
    VulkanBuffer index_buffer;

    PFN_vkCmdSetPrimitiveTopologyEXT vkCmdSetPrimitiveTopologyEXT;

    // Optional
    VkPipelineCache _pipeline_cache;

    u32 find_memory_type(u32 type_filter, u32 prop_flags) const;

    private:
    b8 check_validation_layer_support();
    std::vector<const char*> get_required_extensions();
    b8 create_vulkan_surface(GLFWwindow* window);
};

}