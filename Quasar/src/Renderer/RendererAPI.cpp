#include "RendererAPI.h"
#include <Core/SystemManager.h>

namespace Quasar
{
b8 RendererAPI::init(void* config) {
    renderer_system_config* cfg = (renderer_system_config*)config;

    // state.resizing = false;
    // state.frames_since_resize = 0;

    // Initialize the backend.
    if(!backend.init(cfg->application_name, cfg->window)) {
        LOG_FATAL("Failed to initialize renderer backend!")
        return false;
    }

    return true;
}
void RendererAPI::shutdown() {
    vkDeviceWaitIdle(backend._context._device.logical_device);
    backend.shutdown();
}
b8 RendererAPI::draw(render_packet* packet)
{
    if (packet->app_suspended) {
        return true;
    }

    if(backend.imgui_frame_begin() && backend.frame_begin()) {
        // auto dockspace_id = DockSpaceOverViewport();
        auto gui_render_data = QS_GUI_SYSTEM.get_render_data();
        for (u32 i=0; i<MAX_GUI_WINDOWS; i++) {

            if (gui_render_data[i]) {
                gui_render_data[i]->update(packet);
            }
        }
        backend.frame_end();
        // Render data to GUI after creating the frame
        for (u32 i=0; i<MAX_GUI_WINDOWS; i++) {
            if (gui_render_data[i]) {
                gui_render_data[i]->render();
            }
        }
        backend.imgui_frame_end();
    }

    backend._context._frame_index = (backend._context._frame_index + 1) % MAX_FRAMES_IN_FLIGHT;

    return true;
}
void RendererAPI::resize(u32 width, u32 height)
{
    backend.resize(width, height);
}

} // namespace Quasar
