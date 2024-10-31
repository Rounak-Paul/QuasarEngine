#include "Renderer.h"

namespace Quasar
{
b8 Renderer::init(void* config) {
    renderer_system_config* cfg = (renderer_system_config*)config;

    // state.resizing = false;
    // state.frames_since_resize = 0;

    // Initialize the backend.
    backend.init(cfg->application_name, cfg->window);

    return TRUE;
}
void Renderer::shutdown() {
    backend.shutdown();
}
} // namespace Quasar
