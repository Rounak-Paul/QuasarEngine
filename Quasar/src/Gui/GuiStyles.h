#pragma once
#include <qspch.h>

namespace Quasar
{
void customize_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImVec4 background_color_0 = ImVec4(0.00f, 0.00f, 0.00f, 0.2f);
    ImVec4 background_color_1 = ImVec4(0.00f, 0.00f, 0.00f, 0.7f);

    ImVec4 btn_color = ImVec4(0.00f, 0.25f, 0.25f, 1.00f);
    ImVec4 btn_hover_color = ImVec4(0.00f, 0.50f, 0.50f, 1.00f);

    ImVec4 color_a = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

    // Transparent background
    colors[ImGuiCol_WindowBg]               = background_color_0;
    colors[ImGuiCol_ChildBg]                = background_color_1;
    colors[ImGuiCol_PopupBg]                = background_color_1;
    colors[ImGuiCol_FrameBg]                = background_color_0;
    colors[ImGuiCol_FrameBgHovered]         = background_color_0;
    colors[ImGuiCol_FrameBgActive]          = background_color_0;
    colors[ImGuiCol_TitleBg]                = background_color_0;
    colors[ImGuiCol_TitleBgActive]          = background_color_0;
    colors[ImGuiCol_TitleBgCollapsed]       = background_color_0;
    colors[ImGuiCol_MenuBarBg]              = background_color_0;
    colors[ImGuiCol_ScrollbarBg]            = background_color_0;
    colors[ImGuiCol_ScrollbarGrab]          = background_color_0;
    colors[ImGuiCol_ScrollbarGrabHovered]   = background_color_0;
    colors[ImGuiCol_ScrollbarGrabActive]    = background_color_0;
    colors[ImGuiCol_Header]                 = background_color_0;
    colors[ImGuiCol_HeaderHovered]          = background_color_0;
    colors[ImGuiCol_HeaderActive]           = background_color_0;
    colors[ImGuiCol_ResizeGrip]             = background_color_0;
    colors[ImGuiCol_ResizeGripHovered]      = background_color_0;
    colors[ImGuiCol_ResizeGripActive]       = background_color_0;
    colors[ImGuiCol_PlotLines]              = ImVec4(0.00f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = color_a;
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.00f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = color_a;
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.00f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_DragDropTarget]         = background_color_0;
    colors[ImGuiCol_NavHighlight]           = background_color_0;
    colors[ImGuiCol_NavWindowingHighlight]  = background_color_0;
    colors[ImGuiCol_NavWindowingDimBg]      = background_color_0;
    colors[ImGuiCol_ModalWindowDimBg]       = background_color_0;

    // Cyan neon theme
    colors[ImGuiCol_Text]                   = color_a;  // Bright cyan
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.00f, 0.50f, 0.50f, 1.00f);  // Dim cyan
    // colors[ImGuiCol_Border]                 = color_a;  // Bright cyan
    colors[ImGuiCol_CheckMark]              = color_a;  // Bright cyan
    colors[ImGuiCol_SliderGrab]             = color_a;  // Bright cyan
    colors[ImGuiCol_SliderGrabActive]       = color_a;  // Bright cyan
    colors[ImGuiCol_Button]                 = btn_color;
    colors[ImGuiCol_ButtonHovered]          = btn_hover_color;
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.00f, 0.50f, 0.50f, 1.00f);  // Dim cyan
    colors[ImGuiCol_Separator]              = color_a;  // Bright cyan
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.00f, 0.75f, 0.75f, 1.00f);  // Bright cyan
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.00f, 0.50f, 0.50f, 1.00f);  // Dim cyan
    colors[ImGuiCol_DragDropTarget]         = color_a;  // Bright cyan
    colors[ImGuiCol_NavHighlight]           = color_a;  // Bright cyan

    // Tab Colors
    colors[ImGuiCol_Tab]                    = ImVec4(0.00f, 0.25f, 0.25f, 1.00f);  // Bright cyan
    colors[ImGuiCol_TabHovered]             = ImVec4(0.00f, 0.75f, 0.75f, 1.00f);  // Bright cyan
    colors[ImGuiCol_TabActive]              = ImVec4(0.00f, 0.50f, 0.50f, 1.00f);  // Dim cyan
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.00f, 0.25f, 0.25f, 1.00f);  // Bright cyan
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.00f, 0.50f, 0.50f, 1.00f);  // Dim cyan

    // General style settings
    style.WindowPadding = ImVec2(8, 8);
    style.WindowRounding = 0.0f;
    style.FramePadding = ImVec2(5, 5);
    style.FrameRounding = 4.0f;
    style.ItemSpacing = ImVec2(12, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.IndentSpacing = 25.0f;
    style.ScrollbarSize = 15.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabMinSize = 5.0f;
    style.GrabRounding = 3.0f;

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
    }
}
} // namespace Quasar

