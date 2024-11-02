#pragma once

#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanTypes.h"

namespace Quasar::Vulkan
{
    const std::vector<const char*> validation_layers = { 
        "VK_LAYER_KHRONOS_validation" 
        // ,"VK_LAYER_LUNARG_api_dump" // For all vulkan calls
    };

    class Backend {
        public:
        Backend() {};
        ~Backend() = default;

        b8 init(String name, Window* window);
        void shutdown();

        b8 multithreading_enabled = false;

        private:
        u8 frame_count;
        String engine_name;
        Window* main_window;
        VkAllocationCallbacks* vkallocator = nullptr;
        VkInstance instance;
        VkSurfaceKHR surface;
        VulkanDevice device;
        VulkanSwapchain swapchain;
        FrameData frames[FRAME_OVERLAP];
        
        FrameData& get_current_frame() { return frames[frame_count % FRAME_OVERLAP]; };
        

        friend void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
        static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
        
    };
} // namespace Quasar
