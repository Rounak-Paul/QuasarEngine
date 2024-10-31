#include "Renderer.h"

namespace Quasar
{
b8 Renderer::init(void* config) {
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
void Renderer::shutdown() {
    backend.shutdown();
}
} // namespace Quasar
