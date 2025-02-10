#pragma once
#include <qspch.h>

namespace Quasar
{
void customize_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // AMOLED Black Theme
    ImVec4 black = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    ImVec4 dark_gray = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    ImVec4 mid_gray = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    ImVec4 accent_color = ImVec4(0.60f, 0.20f, 0.90f, 1.00f); // Purple accent

    // Backgrounds
    colors[ImGuiCol_WindowBg]               = black;
    colors[ImGuiCol_ChildBg]                = black;
    colors[ImGuiCol_PopupBg]                = dark_gray;
    colors[ImGuiCol_FrameBg]                = mid_gray;
    colors[ImGuiCol_FrameBgHovered]         = accent_color;
    colors[ImGuiCol_FrameBgActive]          = accent_color;

    // Titles
    colors[ImGuiCol_TitleBg]                = dark_gray;
    colors[ImGuiCol_TitleBgActive]          = accent_color;
    colors[ImGuiCol_TitleBgCollapsed]       = dark_gray;

    // Menu Bar
    colors[ImGuiCol_MenuBarBg]              = dark_gray;

    // Buttons
    colors[ImGuiCol_Button]                 = dark_gray;
    colors[ImGuiCol_ButtonHovered]          = accent_color;
    colors[ImGuiCol_ButtonActive]           = accent_color;

    // Text
    colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    // Borders
    colors[ImGuiCol_Border]                 = dark_gray;
    colors[ImGuiCol_BorderShadow]           = black;

    // Tabs
    colors[ImGuiCol_Tab]                    = dark_gray;
    colors[ImGuiCol_TabHovered]             = accent_color;
    colors[ImGuiCol_TabActive]              = accent_color;
    colors[ImGuiCol_TabUnfocused]           = dark_gray;
    colors[ImGuiCol_TabUnfocusedActive]     = dark_gray;

    // General Style Settings - Minimal Gaps
    style.WindowPadding = ImVec2(4, 4);
    style.WindowRounding = 0.0f;
    style.FramePadding = ImVec2(4, 4);
    style.FrameRounding = 0.0f;
    style.ItemSpacing = ImVec2(6, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.IndentSpacing = 15.0f;
    style.ScrollbarSize = 12.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabMinSize = 6.0f;
    style.GrabRounding = 0.0f;

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
    }
}
} // namespace Quasar