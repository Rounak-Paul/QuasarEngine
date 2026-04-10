#include "quasar.h"
#include "qs_plugin.h"
#include "qs_asset.h"
#include "gltf_loader.h"

static Qs_Extension *s_importer_ext = NULL;

static void on_load(Qs_Engine *engine)
{
    s_importer_ext = qs_engine_ext_register(engine,
                                            QS_EXT_ASSET_IMPORTER,
                                            gltf_importer_ext(),
                                            NULL);
    QS_LOG_INFO("GltfImporter: registered asset importer for .gltf/.glb");
}

static void on_unload(Qs_Engine *engine)
{
    (void)engine;
    if (s_importer_ext) {
        qs_ext_unregister(s_importer_ext);
        s_importer_ext = NULL;
    }
}

static const Qs_PluginDesc s_desc = {
    .id          = "com.quasar.builtin.importer.gltf",
    .name        = "glTF Importer",
    .version     = "1.0.0",
    .author      = "Quasar Engine",
    .description = "Loads glTF 2.0 and GLB assets into the engine resource system.",
    .api_version = QS_PLUGIN_API_VERSION,
    .on_load     = on_load,
    .on_unload   = on_unload,
};

QS_PLUGIN_EXPORT const Qs_PluginDesc *qs_plugin_entry(void)
{
    return &s_desc;
}
