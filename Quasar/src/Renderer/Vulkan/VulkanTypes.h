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

typedef struct vulkan_device {
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
} vulkan_device;

typedef struct vulkan_image {
    VkImage handle;
    VkDeviceMemory memory;
    VkImageView view;
    u32 width;
    u32 height;
} vulkan_image;

typedef enum vulkan_render_pass_state {
    READY,
    RECORDING,
    IN_RENDER_PASS,
    RECORDING_ENDED,
    SUBMITTED,
    NOT_ALLOCATED
} vulkan_render_pass_state;
typedef struct vulkan_renderpass {
    VkRenderPass handle;
    f32 x, y, w, h;
    f32 r, g, b, a;
    f32 depth;
    u32 stencil;
    vulkan_render_pass_state state;
} vulkan_renderpass;

typedef struct vulkan_swapchain {
    VkSurfaceFormatKHR image_format;
    u8 max_frames_in_flight;
    VkSwapchainKHR handle;
    u32 image_count;
    VkImage* images;
    VkImageView* views;
    vulkan_image depth_attachment;
} vulkan_swapchain;

typedef enum vulkan_command_buffer_state {
    COMMAND_BUFFER_STATE_READY,
    COMMAND_BUFFER_STATE_RECORDING,
    COMMAND_BUFFER_STATE_IN_RENDER_PASS,
    COMMAND_BUFFER_STATE_RECORDING_ENDED,
    COMMAND_BUFFER_STATE_SUBMITTED,
    COMMAND_BUFFER_STATE_NOT_ALLOCATED
} vulkan_command_buffer_state;
typedef struct vulkan_command_buffer {
    VkCommandBuffer handle;
    // Command buffer state.
    vulkan_command_buffer_state state;
} vulkan_command_buffer;

typedef struct vulkan_context {
    u32 framebuffer_width;
    u32 framebuffer_height;

    VkInstance instance;
    VkAllocationCallbacks* allocator = nullptr;
    #ifdef QS_DEBUG
    VkDebugUtilsMessengerEXT debug_messenger;
    #endif

    VkSurfaceKHR surface;
    vulkan_device device;

    vulkan_swapchain swapchain;
    vulkan_renderpass main_renderpass;

    DynamicArray<vulkan_command_buffer> graphics_command_buffers;

    u32 image_index;
    u32 current_frame;

    b8 recreating_swapchain;

    i32 find_memory_index(u32 type_filter, u32 property_flags) {
        VkPhysicalDeviceMemoryProperties memory_properties;
        vkGetPhysicalDeviceMemoryProperties(device.physical_device, &memory_properties);

        for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
            // Check each memory type to see if its bit is set to 1.
            if (type_filter & (1 << i) && (memory_properties.memoryTypes[i].propertyFlags & property_flags) == property_flags) {
                return i;
            }
        }

        LOG_WARN("Unable to find suitable memory type!");
        return -1;
    }
} vulkan_context;

}