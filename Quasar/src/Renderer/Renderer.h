#pragma once

#include <qspch.h>
#include "RendererTypes.h"

namespace Quasar
{
    class Renderer {
        public:
        Renderer() = default;
        ~Renderer() = default;

        b8 init();

        b8 begin_frame();
        void end_frame();

        void shutdown();

        private:
        u8 _frame_number {0};

        VkInstance _instance;
        VkDebugUtilsMessengerEXT _debug_messenger;
        VkPhysicalDevice _physical_device;
        VkDevice _device;
        VkSurfaceKHR _surface;
    };
} // namespace Quasar::Renderer
