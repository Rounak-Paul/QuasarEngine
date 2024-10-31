#include "Renderer.h"

namespace Quasar
{
b8 Renderer::init(void* config) {
    renderer_system_config* cfg = (renderer_system_config*)config;
    main_window = cfg->window;

    // state.resizing = false;
    // state.frames_since_resize = 0;

    // renderer_backend_config renderer_config = {};
    // renderer_config.application_name = cfg->application_name;

    // renderer_config.width = QS_APP_STATE.width;
    // renderer_config.height = QS_APP_STATE.height;

    // Initialize the backend.
    VkExtent2D extent = main_window->get_extent();
    backend = Vulkan::Backend(cfg->application_name, extent.width, extent.height);

    return TRUE;
}
void Renderer::shutdown() {
    
}
} // namespace Quasar
