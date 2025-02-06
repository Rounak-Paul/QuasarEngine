#pragma once

#include <qspch.h>
#include "VulkanDevice.h"
#include "VulkanPipeline.h"

namespace Quasar {

#define MAX_FRAMES_IN_FLIGHT 3

struct VulkanContext {
    VulkanContext(GLFWwindow* window);
    ~VulkanContext();

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

    VkExtent2D _extent;
    VkSampleCountFlagBits _msaa_samples;
    static const VkFormat _image_format = VK_FORMAT_B8G8R8A8_UNORM;
    
    std::vector<VkCommandBuffer> _command_buffers;

    PFN_vkCmdSetPrimitiveTopologyEXT vkCmdSetPrimitiveTopologyEXT;

    // Optional
    VkPipelineCache _pipeline_cache;

    u32 find_memory_type(u32 type_filter, u32 prop_flags) const;

    private:
    b8 check_validation_layer_support();
    std::vector<const char*> get_required_extensions();
    b8 create_vulkan_surface(GLFWwindow* window);
    void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
};

}