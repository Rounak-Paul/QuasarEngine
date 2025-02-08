#pragma once
#include <qspch.h>
#include <Math/Math.h>
#include <vulkan/vulkan.h>

#include "VulkanContext.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include "VulkanCheckResult.h"

namespace Quasar
{
    class Backend {
        public:
        Backend() {};
        ~Backend() = default;

        b8 init(String& app_name, Window* main_window);
        void shutdown();
        void resize(u32 width, u32 height);

        b8 frame_begin();
        b8 frame_end();
        b8 imgui_frame_begin();
        b8 imgui_frame_end();


        b8 _multithreading_enabled = false;

        VulkanContext _context;

        private:
        // ImGui
        void vulkan_window_setup(ImGui_ImplVulkanH_Window *wd, VkSurfaceKHR surface, int width, int height);
        void vulkan_window_cleanup();
        void frame_render(ImGui_ImplVulkanH_Window *wd, ImDrawData *draw_data);
        void frame_present(ImGui_ImplVulkanH_Window *wd);
        inline static void check_vk_imgui(VkResult err) {
            VK_CALL(err);
        }
    };
} // namespace Vulkan
