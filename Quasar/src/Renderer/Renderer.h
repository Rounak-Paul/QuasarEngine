#pragma once

#include <qspch.h>
#include "RendererTypes.h"
#include <Core/Window.h>

namespace Quasar
{
    class Renderer {
        public:
        Renderer() = default;
        ~Renderer() = default;

        b8 init(const std::string& name, const Window& window);

        b8 begin_frame();
        void end_frame();

        void shutdown();

        private:
        u8 _frame_number {0};

        u32 _api_major; // The instance-level api major version.
        u32 _api_minor; // The instance-level api minor version.
        u32 _api_patch; // The instance-level api patch version.
        b8 _validation_enabled = true;
        VkInstance _instance;
        VkDebugUtilsMessengerEXT _debug_messenger;
        VkPhysicalDevice _physical_device;
        VkDevice _device;
        VkSurfaceKHR _surface;
    };
} // namespace Quasar::Renderer
