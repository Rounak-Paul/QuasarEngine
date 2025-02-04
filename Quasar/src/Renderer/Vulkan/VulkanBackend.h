#pragma once
#include <qspch.h>
#include <Math/Math.h>
#include <vulkan/vulkan.hpp>

#include "VulkanContext.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

namespace Quasar::Renderer
{
    // TODO: temp
    struct ImGuiWindow {
        const char *Name{""};
        bool Visible{true};
    };

    struct WindowsState {
        ImGuiWindow SceneControls{"Scene controls"};
        ImGuiWindow Scene{"Scene"};
        ImGuiWindow ImGuiDemo{"Dear ImGui Demo", true};
    };
    // TODO: end temp
    
    class Backend {
        public:
        Backend() {};
        ~Backend() = default;

        b8 init(String& app_name, Window* main_window);
        void shutdown();
        void resize(u32 width, u32 height);

        void update();
        b8 render(u32 width, u32 height, const vk::ClearColorValue &bg_color);

        b8 multithreading_enabled = false;

        std::unique_ptr<VulkanContext> context;
        VkSurfaceKHR surface;

        // ImGui
        WindowsState Windows;
        vk::DescriptorSet MainSceneDescriptorSet;
        b8 create_vulkan_surface(GLFWwindow* window);
        void SetupVulkanWindow(ImGui_ImplVulkanH_Window *wd, vk::SurfaceKHR surface, int width, int height);
        void CleanupVulkanWindow();
        void FrameRender(ImGui_ImplVulkanH_Window *wd, ImDrawData *draw_data);
        void FramePresent(ImGui_ImplVulkanH_Window *wd);
        inline static void CheckVk(VkResult err) {
            if (err != 0) throw std::runtime_error(std::format("Vulkan error: {}", int(err)));
        }
    };
} // namespace Vulkan
