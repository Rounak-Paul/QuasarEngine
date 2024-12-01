#pragma once
#include <qspch.h>
#include <Math/Math.h>

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

typedef struct global_uniform_object {
    Math::Mat4 projection;   // 64 bytes
    Math::Mat4 view;         // 64 bytes
    Math::Mat4 m_reserved0;  // 64 bytes, reserved for future use
    Math::Mat4 m_reserved1;  // 64 bytes, reserved for future use
} global_uniform_object;

typedef struct vulkan_buffer {
    u64 total_size;
    VkBuffer handle;
    VkBufferUsageFlagBits usage;
    b8 is_locked;
    VkDeviceMemory memory;
    i32 memory_index;
    u32 memory_property_flags;
} vulkan_buffer;

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

typedef struct vulkan_framebuffer {
    VkFramebuffer handle;
    u32 attachment_count;
    VkImageView* attachments;
    vulkan_renderpass* renderpass;
} vulkan_framebuffer;

typedef struct vulkan_swapchain {
    VkSurfaceFormatKHR image_format;
    u8 max_frames_in_flight;
    VkSwapchainKHR handle;
    u32 image_count;
    VkImage* images;
    VkImageView* views;
    vulkan_image depth_attachment;

    // framebuffers used for on-screen rendering.
    DynamicArray<vulkan_framebuffer> framebuffers;
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

typedef struct vulkan_fence {
    VkFence handle;
    b8 is_signaled;
} vulkan_fence;

typedef struct vulkan_shader_stage {
    VkShaderModuleCreateInfo create_info;
    VkShaderModule handle;
    VkPipelineShaderStageCreateInfo shader_stage_create_info;
} vulkan_shader_stage;
typedef struct vulkan_pipeline {
    VkPipeline handle;
    VkPipelineLayout pipeline_layout;
} vulkan_pipeline;

#define OBJECT_SHADER_STAGE_COUNT 2

typedef struct vulkan_object_shader {
    // vertex, fragment
    vulkan_shader_stage stages[OBJECT_SHADER_STAGE_COUNT];
    VkDescriptorPool global_descriptor_pool;
    VkDescriptorSetLayout global_descriptor_set_layout;

    // One descriptor set per frame - max 3 for triple-buffering.
    VkDescriptorSet global_descriptor_sets[3];

    // Global uniform object.
    global_uniform_object global_ubo;

    // Global uniform buffer.
    vulkan_buffer global_uniform_buffer;

    vulkan_pipeline pipeline;
} vulkan_object_shader;

typedef struct vulkan_context {
    u32 framebuffer_width;
    u32 framebuffer_height;

    u64 framebuffer_size_generation; // Current generation of framebuffer size. If it does not match framebuffer_size_last_generation, a new one should be generated.
    u64 framebuffer_size_last_generation; // The generation of the framebuffer when it was last created. Set to framebuffer_size_generation when updated.

    VkInstance instance;
    VkAllocationCallbacks* allocator = nullptr;
    #ifdef QS_DEBUG
    VkDebugUtilsMessengerEXT debug_messenger;
    #endif

    VkSurfaceKHR surface;
    vulkan_device device;

    vulkan_swapchain swapchain;
    vulkan_renderpass main_renderpass;

    vulkan_buffer object_vertex_buffer;
    vulkan_buffer object_index_buffer;

    DynamicArray<vulkan_command_buffer> graphics_command_buffers;


    DynamicArray<VkSemaphore> image_available_semaphores;
    DynamicArray<VkSemaphore> queue_complete_semaphores;
    u32 in_flight_fence_count;
    DynamicArray<vulkan_fence> in_flight_fences;
    // Holds pointers to fences which exist and are owned elsewhere.
    DynamicArray<vulkan_fence*> images_in_flight;

    u32 image_index;
    u32 current_frame;

    b8 recreating_swapchain;

    vulkan_object_shader object_shader;

    u64 geometry_vertex_offset;
    u64 geometry_index_offset;

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