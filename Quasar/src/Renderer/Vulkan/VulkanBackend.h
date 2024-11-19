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
        void draw();
        void resize(u32 width, u32 height);

        b8 multithreading_enabled = false;

        private:
        VulkanContext context;

        b8 check_validation_layer_support();
        std::vector<const char*> get_required_extensions();
        void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
        VkResult create_debug_messenger();
        b8 create_vulkan_surface(VulkanContext* context, Window* window);
        void create_graphics_pipeline();

    };
} // namespace Vulkan
