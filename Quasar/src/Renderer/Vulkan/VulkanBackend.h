#pragma once
#include <qspch.h>
#include <Math/Math.h>
#include <vulkan/vulkan.hpp>

#include "VulkanContext.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

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

        b8 multithreading_enabled = false;

        std::unique_ptr<VulkanContext> context;

        // ImGui
        void SetupVulkanWindow(ImGui_ImplVulkanH_Window *wd, VkSurfaceKHR surface, int width, int height);
        void CleanupVulkanWindow();
        void FrameRender(ImGui_ImplVulkanH_Window *wd, ImDrawData *draw_data);
        void FramePresent(ImGui_ImplVulkanH_Window *wd);
        inline static void CheckVk(VkResult err) {
            if (err != 0) throw std::runtime_error(std::format("Vulkan error: {}", int(err)));
        }
    };
} // namespace Vulkan
