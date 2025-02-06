#include "Scenespace.h"
#include <vulkan/vulkan.hpp>
#include <Resources/Scene.h>

namespace Quasar
{
vk::DescriptorSet main_scene_descriptor_set;
static vk::ClearColorValue ImVec4ToClearColor(const ImVec4 &v) { return {v.x, v.y, v.z, v.w}; }

Scenespace::Scenespace() : GuiWindow("Scenespace") {

}

void Scenespace::init() {
    
}

void Scenespace::shutdown() {

}

void Scenespace::update(render_packet* packet) {
    Scene* scene = packet->scene;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin(window_name.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    const auto content_region = ImGui::GetContentRegionAvail();
    if (scene->render(content_region.x, content_region.y, ImVec4ToClearColor(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)))) {
        if (main_scene_descriptor_set) {
            ImGui_ImplVulkan_RemoveTexture(main_scene_descriptor_set);
        }
        main_scene_descriptor_set = ImGui_ImplVulkan_AddTexture(QS_RENDERER.get_vkcontext()->_texture_sampler, scene->resolveImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    ImTextureID textureID = (ImTextureID) static_cast<VkDescriptorSet>(main_scene_descriptor_set);
    ImGui::Image(textureID, ImGui::GetContentRegionAvail());
    ImGui::End();
    ImGui::PopStyleVar();
}
} // namespace Quasar
