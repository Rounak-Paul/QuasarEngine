#pragma once
#include <qspch.h>

namespace Quasar
{
void customize_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // OLED Theme - High Contrast
    ImVec4 background_color_0 = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    ImVec4 background_color_1 = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    ImVec4 panel_color = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

    // Accent Colors
    ImVec4 primary_accent = ImVec4(0.00f, 0.60f, 0.50f, 1.00f);
    ImVec4 secondary_accent = ImVec4(0.00f, 0.65f, 0.40f, 1.00f);
    
    // Text Colors
    ImVec4 color_text = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    ImVec4 title_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    ImVec4 text_disabled = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    // Backgrounds
    colors[ImGuiCol_WindowBg]               = background_color_0;
    colors[ImGuiCol_ChildBg]                = background_color_1;
    colors[ImGuiCol_PopupBg]                = background_color_1;
    colors[ImGuiCol_FrameBg]                = panel_color;
    colors[ImGuiCol_FrameBgHovered]         = primary_accent;
    colors[ImGuiCol_FrameBgActive]          = secondary_accent;
    
    // Titles - Bold and Modern
    colors[ImGuiCol_TitleBg]                = primary_accent;
    colors[ImGuiCol_TitleBgActive]          = secondary_accent;
    colors[ImGuiCol_TitleBgCollapsed]       = primary_accent;

    // Menu Bar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.06, 0.06f, 0.06f, 1.00f); // Dark gray
    
    // Buttons
    colors[ImGuiCol_Button]                 = primary_accent;
    colors[ImGuiCol_ButtonHovered]          = secondary_accent;
    colors[ImGuiCol_ButtonActive]           = secondary_accent;

    // Text
    colors[ImGuiCol_Text]                   = color_text;
    colors[ImGuiCol_TextDisabled]           = text_disabled;
    
    // Borders
    colors[ImGuiCol_Border]                 = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    // Tabs
    colors[ImGuiCol_Tab]                    = panel_color;
    colors[ImGuiCol_TabHovered]             = secondary_accent;
    colors[ImGuiCol_TabActive]              = primary_accent;
    colors[ImGuiCol_TabUnfocused]           = panel_color;
    colors[ImGuiCol_TabUnfocusedActive]     = primary_accent;

    // General Style Settings
    style.WindowPadding = ImVec2(10, 10);
    style.WindowRounding = 0.0f;
    style.FramePadding = ImVec2(6, 6);
    style.FrameRounding = 0.0f;
    style.ItemSpacing = ImVec2(14, 10);
    style.ItemInnerSpacing = ImVec2(10, 8);
    style.IndentSpacing = 25.0f;
    style.ScrollbarSize = 16.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabMinSize = 6.0f;
    style.GrabRounding = 0.0f;

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
    }
}
} // namespace Quasar
