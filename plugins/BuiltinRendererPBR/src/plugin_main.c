#include "qs_plugin.h"
#include "qs_renderer.h"

/* Backends defined in their respective vk_*.c files */
extern const Qs_RendererBackend vk_renderer_backend;

static void on_load(Qs_Engine *engine)
{
    (void)engine;
    qs_renderer_backend_register(&vk_renderer_backend);
}

static void on_unload(Qs_Engine *engine)
{
    (void)engine;
    qs_renderer_backend_unregister("VulkanRenderer");
}

QS_PLUGIN_EXPORT const Qs_PluginDesc *qs_plugin_entry(void)
{
    static const Qs_PluginDesc desc = {
        .id          = "com.quasar.builtin.renderer.pbr",
        .name        = "BuiltinRendererPBR",
        .version     = "1.0.0",
        .author      = "QuasarEngine",
        .description = "Vulkan PBR renderer backend (Causality)",
        .api_version = QS_PLUGIN_API_VERSION,
        .on_load     = on_load,
        .on_unload   = on_unload,
    };
    return &desc;
}
