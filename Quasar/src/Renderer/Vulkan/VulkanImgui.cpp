#include "VulkanImgui.h"

namespace Quasar::Renderer {
    b8 vulkan_imgui_init(VulkanContext* context)
    {
        context->imgui_context = ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
        ImGui::GetIO().ConfigViewportsNoAutoMerge = true;
        ImGui::GetIO().ConfigViewportsNoTaskBarIcon = true;

        ImGui::GetIO().Fonts->AddFontDefault();
        return true;
    }
}