/*
 * plugin_main.c  —  SkyNode render graph plugin entry point.
 *
 * Registers the sky gradient node at QS_EXT_RENDER_GRAPH_NODE with
 * stage = QS_RG_STAGE_PRE_GEOMETRY so the PBR renderer inserts it before
 * the forward geometry pass and wires sky.color_out → pbr.sky_color.
 */

#include "quasar.h"

/* Declared in sky_node.c */
extern const Qs_RgNodeType sky_node_type;

static const Qs_RgNodeTypeExt s_sky_ext = {
    .type  = &sky_node_type,
    .stage = QS_RG_STAGE_PRE_GEOMETRY,
};

static Qs_Extension *s_ext = NULL;

static void on_load(Qs_Engine *engine)
{
    s_ext = qs_engine_ext_register(engine,
                                   QS_EXT_RENDER_GRAPH_NODE,
                                   &s_sky_ext,
                                   NULL);
    QS_LOG_INFO("SkyNode: registered (PRE_GEOMETRY stage)");
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
    .id          = "com.quasar.plugins.sky_node",
    .name        = "Sky Node",
    .version     = "1.0.0",
    .author      = "Quasar Engine",
    .description = "Procedural gradient sky rendered before PBR geometry.",
    .api_version = QS_PLUGIN_API_VERSION,
    .on_load     = on_load,
    .on_unload   = on_unload,
};

QS_PLUGIN_EXPORT const Qs_PluginDesc *qs_plugin_entry(void)
{
    return &s_desc;
}
