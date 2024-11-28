#pragma once
#include <qspch.h>
#include "VulkanTypes.h"

namespace Quasar::Renderer
{
    class Backend {
        public:
        Backend() {};
        ~Backend() = default;

        b8 init(String& app_name, Window* main_window);
        void shutdown();
        void resize(u32 width, u32 height);

        b8 begin_frame(f32 dt);
        b8 end_frame(f32 dt);

        b8 multithreading_enabled = false;

        private:
        vulkan_context context;

        b8 check_validation_layer_support();
        std::vector<const char*> get_required_extensions();
        void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
        VkResult create_debug_messenger();
        b8 create_vulkan_surface(Window* window);
        void create_command_buffers();
        void regenerate_framebuffers(vulkan_swapchain* swapchain, vulkan_renderpass* renderpass);
    };
} // namespace Vulkan
