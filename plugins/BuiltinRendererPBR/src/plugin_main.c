#include "qs_plugin.h"
#include "qs_renderer.h"
#include "qs_texture.h"
#include "qs_mesh.h"
#include "qs_material.h"
#include "qs_light.h"

/* Backends defined in their respective vk_*.c files */
extern const Qs_RendererBackend vk_renderer_backend;
extern const Qs_TextureBackend  vk_texture_backend;
extern const Qs_MeshBackend     vk_mesh_backend;
extern const Qs_MaterialBackend vk_material_backend;
extern const Qs_LightBackend    vk_light_backend;

static void on_load(Qs_Engine *engine)
{
    (void)engine;
    qs_renderer_backend_register(&vk_renderer_backend);
    qs_texture_backend_register(&vk_texture_backend);
    qs_mesh_backend_register(&vk_mesh_backend);
    qs_material_backend_register(&vk_material_backend);
    qs_light_backend_register(&vk_light_backend);
}

static void on_unload(Qs_Engine *engine)
{
    (void)engine;
    qs_renderer_backend_register(NULL);
    qs_texture_backend_register(NULL);
    qs_mesh_backend_register(NULL);
    qs_material_backend_register(NULL);
    qs_light_backend_register(NULL);
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
