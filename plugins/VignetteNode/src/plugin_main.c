/*
 * plugin_main.c  —  VignetteNode render graph plugin entry point.
 *
 * Registers the radial vignette node at QS_EXT_RENDER_GRAPH_NODE with
 * stage = QS_RG_STAGE_POST_TONEMAP so the PBR renderer chains it after
 * the tonemap pass and renders the final output into the swapchain.
 */

#include "quasar.h"

extern const Qs_RgNodeType vignette_node_type;

static const Qs_RgNodeTypeExt s_vignette_ext = {
    .type  = &vignette_node_type,
    .stage = QS_RG_STAGE_POST_TONEMAP,
};

static Qs_Extension *s_ext = NULL;

static void on_load(Qs_Engine *engine)
{
    s_ext = qs_engine_ext_register(engine,
                                   QS_EXT_RENDER_GRAPH_NODE,
                                   &s_vignette_ext,
                                   NULL);
    QS_LOG_INFO("VignetteNode: registered (POST_TONEMAP stage)");
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
    .id          = "com.quasar.plugins.vignette_node",
    .name        = "Vignette Node",
    .version     = "1.0.0",
    .author      = "Quasar Engine",
    .description = "Radial vignette applied after tone-mapping as the final swapchain pass.",
    .api_version = QS_PLUGIN_API_VERSION,
    .on_load     = on_load,
    .on_unload   = on_unload,
};

QS_PLUGIN_EXPORT const Qs_PluginDesc *qs_plugin_entry(void)
{
    return &s_desc;
}
