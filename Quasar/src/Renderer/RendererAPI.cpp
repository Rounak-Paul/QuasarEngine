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
        static f32 z = 0.0f;
        z += 0.01f;
        Math::Mat4 view = Math::Mat4::translation(Math::Vec3{0, 0, z}).inverse(); // -30.0f

        backend.update_global_state(projection, view, Math::Vec3{0.f, 0.f, 0.f}, Math::Vec4{1.f, 1.f, 1.f, 1.f}, 0);

        // mat4 model = mat4_translation((vec3){0, 0, 0});
        static f32 angle = 0.01f;
        angle += 0.001f;
        Math::Quat rotation = Math::Quat::from_axis_angle(Math::Vec3{0.f, 1.f, 0.f}, angle);
        Math::Mat4 model = quat_to_rotation_matrix(rotation, Math::Vec3{});
        backend.update_object(model);

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
