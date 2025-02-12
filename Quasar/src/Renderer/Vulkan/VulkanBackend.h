#pragma once
#include <qspch.h>
#include <Math/Math.h>

#include "VulkanTypes.h"
#include "VulkanCheckResult.h"

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
        b8 imgui_frame_begin();
        b8 imgui_frame_end();

        b8 vulkan_surface_create(GLFWwindow* window);
        b8 context_create(GLFWwindow* window);
        void context_destroy();

        VulkanPipeline* pipeline_create(class Shader* s, const VulkanPipelineConfig& config = {});
        void pipeline_destroy(VulkanPipeline* pipeline);
        
        b8 shader_create(const struct ShaderConfig& config, class Shader* s);
        void shader_destroy(class Shader* s);

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
