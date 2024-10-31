#pragma once
#include <qspch.h>

namespace Quasar::Vulkan {

typedef struct swapchain_support_info {
    VkSurfaceCapabilitiesKHR capabilities;
    u32 format_count;
    std::vector<VkSurfaceFormatKHR> formats;
    u32 present_mode_count;
    std::vector<VkPresentModeKHR> present_modes;
} swapchain_support_info;

class VulkanDevice {
    public:
    VulkanDevice() {};
    ~VulkanDevice() = default;

    b8 create(VkInstance instance, VkSurfaceKHR* surface, VkAllocationCallbacks* allocator);
    void destroy(VkAllocationCallbacks* allocator);
    static void query_swapchain_support(
        VkPhysicalDevice physical_device,
        VkSurfaceKHR surface,
        swapchain_support_info* out_support_info);
    b8 detect_depth_format();

    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    swapchain_support_info swapchain_support;
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

    private:
    b8 select_physical_device(VkInstance instance, VkSurfaceKHR* surface, swapchain_support_info* swapchain_support, b8 discreteGPU);
};

}