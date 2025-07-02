#pragma once

#include <qspch.h>
#include "VulkanTypes.h"
#include <Core/Window.h>
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"

namespace Quasar
{
    class Renderer {
        public:
        Renderer() = default;
        ~Renderer() = default;

        Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

        b8 init(const std::string& name, const Window& window);

        b8 begin_frame();
        void end_frame();

        void shutdown();

        FrameData& get_current_frame() { return _frames[_frame_number % FRAME_OVERLAP]; };

        private:
        u32 _frame_number {0};
        FrameData _frames[FRAME_OVERLAP];

        u32 _api_major; // The instance-level api major version.
        u32 _api_minor; // The instance-level api minor version.
        u32 _api_patch; // The instance-level api patch version.
        b8 _validation_enabled = true;
        VkInstance _instance;
        VkDebugUtilsMessengerEXT _debug_messenger;
        VulkanDevice _device;
        VkSurfaceKHR _surface;
        VulkanSwapchain _swapchain;
    };
} // namespace Quasar::Renderer
