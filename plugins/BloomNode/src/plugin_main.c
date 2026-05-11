/*
 * plugin_main.c  —  BloomNode render graph plugin entry point.
 *
 * Registers the Kawase bloom node at QS_EXT_RENDER_GRAPH_NODE with
 * stage = QS_RG_STAGE_POST_GEOMETRY so the PBR renderer inserts it after
 * the forward pass and wires:
 *   pbr.hdr_color  → bloom.color_in
 *   bloom.bloom_tex → tonemap.bloom_tex
 */

#include "quasar.h"

extern const Qs_RgNodeType bloom_node_type;

static const Qs_RgNodeTypeExt s_bloom_ext = {
    .type  = &bloom_node_type,
    .stage = QS_RG_STAGE_POST_GEOMETRY,
};

static Qs_Extension *s_ext = NULL;

static void on_load(Qs_Engine *engine)
{
    s_ext = qs_engine_ext_register(engine,
                                   QS_EXT_RENDER_GRAPH_NODE,
                                   &s_bloom_ext,
                                   NULL);
    QS_LOG_INFO("BloomNode: registered (POST_GEOMETRY stage)");
}

static void on_unload(Qs_Engine *engine)
{
    (void)engine;
    if (s_ext) {
        qs_ext_unregister(s_ext);
        s_ext = NULL;
    }
}

static const Qs_PluginDesc s_desc = {
    .id           = "com.quasar.plugins.bloom_node",
    .name         = "Bloom Node",
    .version      = "1.0.0",
    .author       = "Quasar Engine",
    .description  = "Kawase bloom post-processing injected after PBR geometry.",
    .api_version  = QS_PLUGIN_API_VERSION,
    .on_load      = on_load,
    .on_unload    = on_unload,
    .capabilities = QS_PLUGIN_CAP_RENDER_NODE,
};

QS_PLUGIN_EXPORT const Qs_PluginDesc *qs_plugin_entry(void)
{
    return &s_desc;
}
