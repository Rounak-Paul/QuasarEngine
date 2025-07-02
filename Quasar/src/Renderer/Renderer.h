#pragma once

#include <qspch.h>
#include "VulkanTypes.h"
#include <Core/Window.h>
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanImage.h"

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
        void draw();
        void end_frame();

        void shutdown();

        FrameData& get_current_frame() { return _frames[_frame_number % FRAME_OVERLAP]; };

        private:
        u32 _frame_number {0};
        FrameData _frames[FRAME_OVERLAP];
        DeletionQueue _main_deletion_queue;

        u32 _api_major; // The instance-level api major version.
        u32 _api_minor; // The instance-level api minor version.
        u32 _api_patch; // The instance-level api patch version.
        b8 _validation_enabled = true;
        VkInstance _instance;
        VmaAllocator _allocator;
        VkDebugUtilsMessengerEXT _debug_messenger;
        VulkanDevice _device;
        VkSurfaceKHR _surface;
        VulkanSwapchain _swapchain;

        //draw resources
        VulkanImage _draw_image;
        VkExtent2D _draw_extent;

        b8 initialize_validation_layers();
        void fetch_api_version();
        b8 create_instance(const std::string& name);
        void setup_debug_messenger();
        b8 create_surface(const Window& window);
        b8 create_device();
        b8 create_swapchain(const Window& window);
        b8 create_allocator();
        b8 create_draw_image(const Window& window);
        b8 create_command_buffers();
        b8 create_sync_objects();
    };
} // namespace Quasar::Renderer
