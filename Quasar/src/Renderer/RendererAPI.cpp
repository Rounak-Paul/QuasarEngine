#include "RendererAPI.h"

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
    backend.shutdown();
}
b8 RendererAPI::draw(render_packet* packet)
{
    if (packet->app_suspended) {
        return true;
    }
    // If the begin frame returned successfully, mid-frame operations may continue.
    if (backend.begin_frame(packet->dt)) {

        // End the frame. If this fails, it is likely unrecoverable.
        b8 result = backend.end_frame(packet->dt);

        if (!result) {
            LOG_ERROR("renderer_end_frame failed. Application shutting down...");
            return false;
        }
    }

    return true;
}
void RendererAPI::resize(u32 width, u32 height)
{
    backend.resize(width, height);
}
} // namespace Quasar
