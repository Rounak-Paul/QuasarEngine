#pragma once
#include <qspch.h>

namespace Quasar::Renderer {

#define VK_CHECK(expr)                  \
{                                      \
    assert(expr == VK_SUCCESS);        \
} 

#define MAX_FRAMES_IN_FLIGHT 2

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

typedef struct VulkanImage {
    VkImage handle = VK_NULL_HANDLE;                        // Vulkan image handle
    VkImageView view = VK_NULL_HANDLE;                      // Image view for accessing the image in shaders
    VkDeviceMemory memory = VK_NULL_HANDLE;                 // Device memory backing the image (if applicable)
    VkFormat format;                                        // Format of the image
    VkExtent3D extent;                                      // Extent (width, height, depth) of the image
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;       // Current layout of the image
} VulkanImage;

typedef struct VulkanSwapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;                 // The Vulkan swapchain handle
    std::vector<VulkanImage> images;                        // Images in the swapchain
    VkSurfaceFormatKHR format;                              // Chosen surface format for the swapchain
    VkPresentModeKHR present_mode;                          // Chosen presentation mode (e.g., FIFO, MAILBOX)
    VkSurfaceCapabilitiesKHR capabilities;                  // Surface capabilities for current swapchain
    u32 image_count;                                        // Number of images in the swapchain
    u32 current_image_index;                                // The index of the current image to render to
} VulkanSwapchain;

typedef struct VulkanContext {
    VkInstance instance;
    VkAllocationCallbacks* allocator = nullptr;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkSurfaceKHR surface;
    VulkanDevice device;
    VulkanSwapchain swapchain;
    VkRenderPass renderpass;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;
    std::vector<VkFramebuffer> swapchain_framebuffers;
    std::vector<VkCommandBuffer> commandbuffers;
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;
    u8 current_frame = 0;

    // ImGui
    ImGuiContext* imgui_context;
} VulkanContext;

}