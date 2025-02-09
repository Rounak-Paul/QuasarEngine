#include "Scenespace.h"
#include <vulkan/vulkan.h>
#include <Resources/Scene.h>

namespace Quasar
{
static VkClearColorValue ImVec4ToClearColor(const ImVec4 &v) { return {v.x, v.y, v.z, v.w}; }

Scenespace::Scenespace() : GuiWindow("Scenespace") {

}

void Scenespace::init() {
    descriptor_sets.resize(MAX_FRAMES_IN_FLIGHT);
}

void Scenespace::shutdown() {

}

void Scenespace::update(render_packet* packet) {
    _scene = packet->scene;
    QS_RENDERER.get_vkcontext()->_extent = {(u32)_content_region.x, (u32)_content_region.y};
    scene_updated = _scene->update(_content_region.x, _content_region.y, ImVec4ToClearColor(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)), QS_RENDERER.get_vkcontext()->_frame_index); 
}
void Scenespace::render()
{
    VulkanContext* context = QS_RENDERER.get_vkcontext();
    vkWaitForFences(context->_device.logical_device, 1, &context->inFlightFences[context->_frame_index], VK_TRUE, UINT64_MAX);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin(window_name.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    _content_region = ImGui::GetContentRegionAvail();
    if (scene_updated) {
        auto& descriptor_set = descriptor_sets[context->_frame_index];
        if (descriptor_set != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(descriptor_set);
        }
        descriptor_set = ImGui_ImplVulkan_AddTexture(context->_texture_sampler, _scene->_render_target.get_resolve_image_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        ImTextureID textureID = (ImTextureID) static_cast<VkDescriptorSet>(descriptor_set);
        ImGui::Image(textureID, ImGui::GetContentRegionAvail());
        scene_updated = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
} // namespace Quasar
