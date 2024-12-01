#include "RendererAPI.h"
#include <Math/Math.h>

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
        Math::Mat4 projection = Math::Mat4::perspective(Math::deg_to_rad(45.0f), 1280 / 720.0f, 0.1f, 1000.0f);
        static f32 z = -1.0f;
        z -= 0.005f;
        Math::Mat4 view = Math::Mat4::translation(Math::Vec3{0.f, 0.f, z});
        backend.update_global_state(projection, view, Math::Vec3{0.f, 0.f, 0.f}, Math::Vec4{1.f, 1.f, 1.f, 1.f}, 0);

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
