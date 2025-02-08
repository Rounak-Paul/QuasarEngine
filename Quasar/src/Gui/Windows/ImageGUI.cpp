#include "ImageGUI.h"
#include <vulkan/vulkan.h>
#include <Resources/Scene.h>

namespace Quasar
{
static VkClearColorValue ImVec4ToClearColor(const ImVec4 &v) { return {v.x, v.y, v.z, v.w}; }

ImageGUI::ImageGUI() : GuiWindow("ImageGUI") {

}

void ImageGUI::init() {
    image_scene.create();
}

void ImageGUI::shutdown() {
    image_scene.destroy();
}


void ImageGUI::update(render_packet* packet) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin(window_name.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    const auto content_region = ImGui::GetContentRegionAvail();
    if (image_scene.update(content_region.x, content_region.y, ImVec4ToClearColor(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)))) {
        if (descriptor_set) {
            ImGui_ImplVulkan_RemoveTexture(descriptor_set);
        }
        descriptor_set = ImGui_ImplVulkan_AddTexture(QS_RENDERER.get_vkcontext()->_texture_sampler, image_scene._render_target.get_resolve_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        ImTextureID textureID = (ImTextureID) static_cast<VkDescriptorSet>(descriptor_set);
        ImGui::Image(textureID, ImGui::GetContentRegionAvail());
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
} // namespace Quasar
