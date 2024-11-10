#pragma once
#include <qspch.h>

namespace Quasar::Renderer {

#define VK_CHECK(expr)                  \
{                                      \
    assert(expr == VK_SUCCESS);        \
} 

const std::vector<const char*> validationLayers = { 
    "VK_LAYER_KHRONOS_validation" 
    // ,"VK_LAYER_LUNARG_api_dump" // For all vulkan calls
};

typedef struct vulkan_swapchain_support_info {
        VkSurfaceCapabilitiesKHR capabilities;
        u32 format_count;
        std::vector<VkSurfaceFormatKHR> formats;
        u32 present_mode_count;
        std::vector<VkPresentModeKHR> present_modes;
    } vulkan_swapchain_support_info;

typedef struct VulkanDevice {
    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    vulkan_swapchain_support_info swapchain_support;
    i32 graphics_queue_index;
    i32 present_queue_index;
    i32 transfer_queue_index;
    b8 supports_device_local_host_visible;

    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue transfer_queue;

    VkCommandPool graphics_command_pool;

    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceMemoryProperties memory;

    VkFormat depth_format;
    u8 depth_channel_count;
} VulkanDevice;

typedef struct VulkanContext {
    VkInstance instance;
    VkAllocationCallbacks* allocator = nullptr;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkSurfaceKHR surface;
    VulkanDevice device;
} VkContext;

}