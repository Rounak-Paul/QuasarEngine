#pragma once
#include <qspch.h>
#include <vulkan/vulkan.h>

namespace Quasar {

typedef struct VulkanSwapchainSupportInfo {
    VkSurfaceCapabilitiesKHR capabilities;
    u32 format_count;
    std::vector<VkSurfaceFormatKHR> formats;
    u32 present_mode_count;
    std::vector<VkPresentModeKHR> present_modes;
} VulkanSwapchainSupportInfo;

/** @brief Bitwise flags for device support. @see vulkan_device_support_flag_bits. */
typedef u32 VulkanDeviceSupportFlags;

typedef enum VulkanDeviceSupportFlagBits {
    VULKAN_DEVICE_SUPPORT_FLAG_NONE_BIT = 0x0,
    /** @brief Indicates if the device supports native dynamic topology (i.e. * using Vulkan API >= 1.3). */
    VULKAN_DEVICE_SUPPORT_FLAG_NATIVE_DYNAMIC_TOPOLOGY_BIT = 0x1,
    /** @brief Indicates if this device supports dynamic topology. If not, the renderer will need to generate a separate pipeline per topology type. */
    VULKAN_DEVICE_SUPPORT_FLAG_DYNAMIC_TOPOLOGY_BIT = 0x2,
    VULKAN_DEVICE_SUPPORT_FLAG_LINE_SMOOTH_RASTERISATION_BIT = 0x4
} VulkanDeviceSupportFlagBits;

class VulkanDevice {
    public:
    VulkanDevice() {};
    ~VulkanDevice() = default;
    
    static b8 create(struct VulkanContext* context);

    static void destroy(struct VulkanContext* context);

    static void query_swapchain_support(
        VkPhysicalDevice physical_device,
        VkSurfaceKHR surface,
        VulkanSwapchainSupportInfo* out_support_info);
    
    static b8 detect_depth_format(VulkanDevice* device);

    /** @brief The supported device-level api major version. */
    u32 api_major;
    /** @brief The supported device-level api minor version. */
    u32 api_minor;
    /** @brief The supported device-level api patch version. */
    u32 api_patch;
    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    VulkanSwapchainSupportInfo swapchain_support;
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

    /** @brief Indicates support for various features. */
    VulkanDeviceSupportFlags support_flags;
};

}