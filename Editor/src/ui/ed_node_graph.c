/*
 * ed_node_graph.c — Render graph node graph canvas.
 *
 * Architecture
 * ============
 *  Uses the Causality Ca_NodeGraph component (ca_node_graph.h) for all
 *  rendering — no raw GPU code needed.
 *
 *  Left pane:  Ca_NodeGraph canvas (drag to pan, drag node to move)
 *  Right pane: Properties panel (settings for selected node)
 *
 *  Both panes use ca_div_set_builder for reactive updates:
 *  - canvas_builder re-runs when ng state changes (pan, node move, select)
 *  - props_builder  re-runs when a node is selected
 */

#include "ed_node_graph.h"
#include "../editor.h"
#include "../ed_style.h"

#include "ca_theme.h"
#include "quasar.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* ================================================================
   CONSTANTS
   ================================================================ */

#define NG_MAX_NODES        32
#define NG_MAX_CONNECTIONS  64

/* ================================================================
   TYPES
   ================================================================ */

typedef struct {
    const Qs_RgNodeType *type;     /* port names + counts (NULL for unknowns) */
    const char          *name;     /* display name */
    bool                 is_fixed;
    Qs_RgNodeStage       stage;
    float                init_x, init_y;   /* initial canvas position */
} NgNodeInfo;

typedef struct {
    int src_node, src_pin;
    int dst_node, dst_pin;
} NgConn;

/* ================================================================
   MODULE STATE
   ================================================================ */

static Editor       *s_editor;
static Ca_NodeGraph  s_ng;

static NgNodeInfo    s_nodes[NG_MAX_NODES];
static int           s_node_count;
static NgConn        s_conns[NG_MAX_CONNECTIONS];
static int           s_conn_count;

static Ca_Div       *s_canvas_host;
static Ca_Div       *s_props_div;

static float         s_split_ratio = 0.68f;

/* ================================================================
   COLOUR HELPERS
   ================================================================ */

static uint32_t node_header_color(const NgNodeInfo *n)
{
    if (n->is_fixed)
        return ca_color(0.18f, 0.19f, 0.28f, 1.0f);
    switch (n->stage) {
        case QS_RG_STAGE_PRE_GEOMETRY:  return ca_color(0.14f, 0.28f, 0.22f, 1.0f);
        case QS_RG_STAGE_POST_GEOMETRY: return ca_color(0.24f, 0.16f, 0.34f, 1.0f);
        case QS_RG_STAGE_POST_TONEMAP:  return ca_color(0.34f, 0.20f, 0.14f, 1.0f);
        default:                        return ca_color(0.20f, 0.20f, 0.25f, 1.0f);
    }
}

static uint32_t wire_color(int src_node_idx)
{
    if (src_node_idx < 0 || src_node_idx >= s_node_count)
        return ca_color(0.50f, 0.50f, 0.50f, 0.85f);
    const NgNodeInfo *n = &s_nodes[src_node_idx];
    if (n->is_fixed)
        return ca_color(0.40f, 0.42f, 0.55f, 0.85f);
    switch (n->stage) {
        case QS_RG_STAGE_PRE_GEOMETRY:  return ca_color(0.30f, 0.70f, 0.55f, 0.85f);
        case QS_RG_STAGE_POST_GEOMETRY: return ca_color(0.65f, 0.40f, 0.90f, 0.85f);
        case QS_RG_STAGE_POST_TONEMAP:  return ca_color(0.90f, 0.55f, 0.30f, 0.85f);
        default:                        return ca_color(0.50f, 0.50f, 0.50f, 0.85f);
    }
}

static uint32_t pin_kind_color(Qs_RgResourceKind k)
{
    switch (k) {
        case QS_RG_TEXTURE:     return ca_color(0.85f, 0.45f, 0.90f, 1.0f);
        case QS_RG_BUFFER:      return ca_color(0.40f, 0.75f, 0.95f, 1.0f);
        case QS_RG_PARAM_FLOAT: return ca_color(0.90f, 0.75f, 0.30f, 1.0f);
        case QS_RG_PARAM_UINT:  return ca_color(0.60f, 0.90f, 0.50f, 1.0f);
        case QS_RG_PARAM_VEC4:  return ca_color(0.90f, 0.55f, 0.40f, 1.0f);
        default:                return ca_color(0.70f, 0.70f, 0.75f, 1.0f);
    }
}

/* ================================================================
   SKY PARAM SLIDER CALLBACKS
   ================================================================ */

/* Find the Qs_RgNodeTypeExt for a plugin node by its type name. */
static const Qs_RgNodeTypeExt *ng_find_ext_by_type_name(const char *type_name)
{
    Qs_Engine *engine = editor_engine(s_editor);
    if (!engine || !type_name) return NULL;
    uint32_t n = qs_engine_ext_count(engine, QS_EXT_RENDER_GRAPH_NODE);
    for (uint32_t e = 0; e < n; e++) {
        const Qs_RgNodeTypeExt *ext =
            qs_engine_ext_interface(engine, QS_EXT_RENDER_GRAPH_NODE, e);
        if (ext && ext->type && strcmp(ext->type->name, type_name) == 0)
            return ext;
    }
    return NULL;
}

static void on_sky_param_change(Ca_Slider *sl, void *ud)
{
    uint32_t idx = (uint32_t)(uintptr_t)ud;
    Qs_Engine *engine = editor_engine(s_editor);
    if (!engine) return;
    const Qs_RgNodeTypeExt *ext = ng_find_ext_by_type_name("sky");
    if (ext && ext->set_param)
        ext->set_param(engine, idx, ca_slider_get(sl));
}

/* ================================================================
   PROPERTIES PANEL BUILDER
   ================================================================ */

static void on_bloom_strength_change(Ca_Slider *sl, void *ud)
{
    (void)ud;
    Qs_Renderer *r = editor_scene_renderer(s_editor);
    if (!r) return;
    Qs_PostProcessSettings pp = *qs_renderer_post_process(r);
    pp.bloom_strength = ca_slider_get(sl);
    qs_renderer_set_post_process(r, &pp);
}

static void on_bloom_threshold_change(Ca_Slider *sl, void *ud)
{
    (void)ud;
    Qs_Renderer *r = editor_scene_renderer(s_editor);
    if (!r) return;
    Qs_PostProcessSettings pp = *qs_renderer_post_process(r);
    pp.bloom_threshold = ca_slider_get(sl);
    qs_renderer_set_post_process(r, &pp);
}

static void on_vignette_change(Ca_Slider *sl, void *ud)
{
    (void)ud;
    Qs_Renderer *r = editor_scene_renderer(s_editor);
    if (!r) return;
    Qs_PostProcessSettings pp = *qs_renderer_post_process(r);
    pp.vignette_strength = ca_slider_get(sl);
    qs_renderer_set_post_process(r, &pp);
}

static void on_msaa_select(Ca_Select *sel, void *ud)
{
    (void)ud;
    static const uint32_t k[4] = {1, 2, 4, 8};
    int idx = ca_select_get(sel);
    if (idx < 0 || idx >= 4) return;
    Qs_Renderer *r = editor_scene_renderer(s_editor);
    if (!r) return;
    Qs_PostProcessSettings pp = *qs_renderer_post_process(r);
    pp.msaa_sample_count = k[idx];
    qs_renderer_set_post_process(r, &pp);
}

static void props_builder(Ca_Div *div, void *ud)
{
    (void)div; (void)ud;

    Qs_Renderer *r = editor_scene_renderer(s_editor);
    const Qs_PostProcessSettings *pp = r ? qs_renderer_post_process(r) : NULL;
    const NgNodeInfo *node = NULL;

    if (s_ng.selected_node >= 0 && s_ng.selected_node < s_node_count)
        node = &s_nodes[s_ng.selected_node];

    /* Header */
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL,
                                .style = "ng-props-hdr", .id = "ng-phdiv" });
    ca_text(&(Ca_TextDesc){ .text = node ? node->name : "Node Settings", .style = "ng-props-title",
                             .id = "ng-ptitle" });
    ca_div_end();
    ca_hr(&(Ca_HrDesc){ .style = "st-separator", .id = "ng-hdr-sep" });

    /* Content */
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL,
                                .style = "ng-props-body", .id = "ng-pbody" });

    if (!node) {
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL,
                                    .style = "ng-props-empty", .id = "ng-pe" });
        ca_text(&(Ca_TextDesc){ .text = "Select a node to\nsee its settings.",
                                .style = "ng-props-hint", .id = "ng-ph" });
        ca_div_end();
        ca_div_end();
        return;
    }

    if (node->is_fixed) {
        if (strcmp(node->name, "shadow_csm") == 0) {
            ca_text(&(Ca_TextDesc){ .text = "SHADOW", .style = "ng-props-section" });
            ca_text(&(Ca_TextDesc){ .text = "Cascaded Shadow Maps\n3 cascades, 2048x2048",
                                    .style = "ng-info-text", .id = "ng-info-sh" });
        } else if (strcmp(node->name, "forward_pbr") == 0) {
            static const char *k_labels[4] = {"Off (1x)", "2x", "4x", "8x"};
            static const uint32_t k_counts[4] = {1, 2, 4, 8};
            uint32_t dev_max = r ? qs_renderer_max_msaa_samples(r) : 1;
            int n_opts = 1;
            for (int i = 1; i < 4; i++) { if (k_counts[i] <= dev_max) n_opts = i + 1; }
            uint32_t cur = pp ? pp->msaa_sample_count : 4;
            int sel_idx = 0;
            for (int i = 0; i < n_opts; i++) { if (k_counts[i] == cur) sel_idx = i; }

            ca_text(&(Ca_TextDesc){ .text = "ANTI-ALIASING", .style = "ng-props-section" });
            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL,
                                        .style = "ng-prop-row", .id = "ng-msaa-row" });
            ca_text(&(Ca_TextDesc){ .text = "MSAA", .style = "ng-prop-label" });
            ca_div_begin(&(Ca_DivDesc){ .style = "pm-spacer", .id = "ng-msaa-sp" }); ca_div_end();
            ca_select(&(Ca_SelectDesc){
                .options = k_labels, .option_count = n_opts, .selected = sel_idx,
                .on_change = on_msaa_select, .style = "ng-prop-select", .id = "ng-msaa-sel",
            });
            ca_div_end();
        } else if (strcmp(node->name, "tonemap") == 0) {
            ca_text(&(Ca_TextDesc){ .text = "TONEMAP", .style = "ng-props-section" });
            ca_text(&(Ca_TextDesc){ .text = "ACES Filmic Tonemapping\n+ Gamma Correction (2.2)",
                                    .style = "ng-info-text", .id = "ng-info-tm" });
        }
    } else {
        if (strstr(node->name, "bloom") != NULL) {
            ca_text(&(Ca_TextDesc){ .text = "BLOOM", .style = "ng-props-section" });

            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL,
                                        .style = "ng-prop-row", .id = "ng-bs-row" });
            ca_text(&(Ca_TextDesc){ .text = "Strength", .style = "ng-prop-label" });
            ca_slider(&(Ca_SliderDesc){
                .min=0.0f, .max=1.0f, .value = pp ? pp->bloom_strength : 0.04f,
                .on_change = on_bloom_strength_change,
                .style = "ng-prop-slider", .id = "ng-bs-sl",
            });
            ca_div_end();

            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL,
                                        .style = "ng-prop-row", .id = "ng-bt-row" });
            ca_text(&(Ca_TextDesc){ .text = "Threshold", .style = "ng-prop-label" });
            ca_slider(&(Ca_SliderDesc){
                .min=0.0f, .max=2.0f, .value = pp ? pp->bloom_threshold : 0.4f,
                .on_change = on_bloom_threshold_change,
                .style = "ng-prop-slider", .id = "ng-bt-sl",
            });
            ca_div_end();

        } else if (strstr(node->name, "vignette") != NULL) {
            ca_text(&(Ca_TextDesc){ .text = "VIGNETTE", .style = "ng-props-section" });

            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL,
                                        .style = "ng-prop-row", .id = "ng-vg-row" });
            ca_text(&(Ca_TextDesc){ .text = "Strength", .style = "ng-prop-label" });
            ca_slider(&(Ca_SliderDesc){
                .min=0.0f, .max=1.0f, .value = pp ? pp->vignette_strength : 0.35f,
                .on_change = on_vignette_change,
                .style = "ng-prop-slider", .id = "ng-vg-sl",
            });
            ca_div_end();

        } else if (strstr(node->name, "sky") != NULL) {
            ca_text(&(Ca_TextDesc){ .text = "SKY", .style = "ng-props-section" });

            Qs_Engine *engine = editor_engine(s_editor);
            const Qs_RgNodeTypeExt *ext = ng_find_ext_by_type_name("sky");

            if (ext && ext->params && ext->get_param && ext->set_param && engine) {
                /* Auto-generate a slider for every exposed parameter. */
                for (uint32_t i = 0; i < ext->param_count; i++) {
                    /* Insert a sub-header before the Horizon group */
                    if (i == 0)
                        ca_text(&(Ca_TextDesc){ .text = "ZENITH",  .style = "ng-props-subhdr", .id = "ng-sky-zh" });
                    else if (i == 3)
                        ca_text(&(Ca_TextDesc){ .text = "HORIZON", .style = "ng-props-subhdr", .id = "ng-sky-hh" });
                    else if (i == 6)
                        ca_text(&(Ca_TextDesc){ .text = "GRADIENT", .style = "ng-props-subhdr", .id = "ng-sky-gh" });

                    const Qs_RgNodeParam *p = &ext->params[i];
                    char row_id[32], sl_id[32];
                    snprintf(row_id, sizeof(row_id), "ng-sky-r%u", i);
                    snprintf(sl_id,  sizeof(sl_id),  "ng-sky-s%u", i);

                    ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL,
                                               .style = "ng-prop-row", .id = row_id });
                    ca_text(&(Ca_TextDesc){ .text = p->name, .style = "ng-prop-label" });
                    ca_slider(&(Ca_SliderDesc){
                        .min         = p->min_val,
                        .max         = p->max_val,
                        .value       = ext->get_param(engine, i),
                        .on_change   = on_sky_param_change,
                        .change_data = (void *)(uintptr_t)i,
                        .style       = "ng-prop-slider",
                        .id          = sl_id,
                    });
                    ca_div_end();
                }
            } else {
                ca_text(&(Ca_TextDesc){ .text = "Atmospheric sky background.\nRendered before PBR pass.",
                                        .style = "ng-info-text", .id = "ng-info-sky" });
            }
        } else {
            ca_text(&(Ca_TextDesc){ .text = "No settings available.",
                                    .style = "ng-info-text", .id = "ng-info-none" });
        }
    }

    ca_div_end(); /* ng-props-body */
}

/* ================================================================
   CANVAS BUILDER
   ================================================================ */

static void on_node_selected(Ca_NodeGraph *ng, int idx, void *ud)
{
    (void)ng; (void)idx; (void)ud;
    if (s_props_div) ca_div_invalidate(s_props_div);
}

static void canvas_builder(Ca_Div *div, void *ud)
{
    (void)ud;

    ca_node_graph_begin(&s_ng, div, &(Ca_NodeGraphDesc){
        .style          = "ng-canvas",
        .on_node_select = on_node_selected,
    });

    /* Pre-populate pin counts into ng state so wire routing is correct on
     * frame 1 (before ca_ng_node_end has had a chance to record them). */
    for (int i = 0; i < s_node_count; i++) {
        const NgNodeInfo *info = &s_nodes[i];
        if (!info->type) continue;
        /* Find or prime the slot by matching key */
        for (int j = 0; j < s_ng.node_count; j++) {
            if (strncmp(s_ng.nodes[j].key, info->name, CA_NG_KEY_LEN) == 0) {
                s_ng.nodes[j].input_count  = (int)info->type->input_count;
                s_ng.nodes[j].output_count = (int)info->type->output_count;
                break;
            }
        }
    }

    /* Wires — emitted first so they paint under all nodes. */
    for (int c = 0; c < s_conn_count; c++) {
        const NgConn *conn = &s_conns[c];
        if (conn->src_node < 0 || conn->dst_node < 0) continue;
        ca_ng_wire(&s_ng, &(Ca_NgWireDesc){
            .src_node = s_nodes[conn->src_node].name,
            .src_pin  = conn->src_pin,
            .dst_node = s_nodes[conn->dst_node].name,
            .dst_pin  = conn->dst_pin,
            .color    = wire_color(conn->src_node),
        });
    }

    for (int i = 0; i < s_node_count; i++) {
        const NgNodeInfo *info = &s_nodes[i];

        ca_ng_node_begin(&s_ng, &(Ca_NgNodeDesc){
            .key          = info->name,
            .title        = info->name,
            .x            = info->init_x,
            .y            = info->init_y,
            .header_color = node_header_color(info),
        });

        if (info->type) {
            for (uint32_t p = 0; p < info->type->input_count; p++) {
                ca_ng_input_pin(&s_ng, &(Ca_NgPinDesc){
                    .label = info->type->inputs[p].name,
                    .color = pin_kind_color(info->type->inputs[p].kind),
                });
            }
            for (uint32_t p = 0; p < info->type->output_count; p++) {
                ca_ng_output_pin(&s_ng, &(Ca_NgPinDesc){
                    .label = info->type->outputs[p].name,
                    .color = pin_kind_color(info->type->outputs[p].kind),
                });
            }
        }

        ca_ng_node_end(&s_ng);
    }

    ca_node_graph_end(&s_ng);
}

static void on_split_resize(float ratio, void *ud)
{
    (void)ud;
    s_split_ratio = ratio;
}

/* ================================================================
   GRAPH SETUP  (nodes + connections)
   ================================================================ */

static int add_node(const Qs_RgNodeType *type, const char *name, bool is_fixed,
                     Qs_RgNodeStage stage, float cx, float cy)
{
    if (s_node_count >= NG_MAX_NODES) return -1;
    int i = s_node_count++;
    s_nodes[i] = (NgNodeInfo){
        .type     = type,
        .name     = name,
        .is_fixed = is_fixed,
        .stage    = stage,
        .init_x   = cx,
        .init_y   = cy,
    };
    return i;
}

static void add_conn(int src, int sp, int dst, int dp)
{
    if (s_conn_count >= NG_MAX_CONNECTIONS) return;
    s_conns[s_conn_count++] = (NgConn){src, sp, dst, dp};
}

static void setup_graph(void)
{
    s_node_count = 0;
    s_conn_count = 0;

    /* ---- Fixed nodes ---- */
    int shadow_i = add_node(&qs_rg_shadow_node_type, "shadow_csm",
                             true, 0, 50.0f, 250.0f);
    int pbr_i    = add_node(&qs_rg_pbr_node_type, "forward_pbr",
                             true, 0, 370.0f, 140.0f);
    int tm_i     = add_node(&qs_rg_tonemap_node_type, "tonemap",
                             true, 0, 680.0f, 140.0f);

    /* shadow → pbr (output 0 → input 1) */
    add_conn(shadow_i, 0, pbr_i, 1);

    /* ---- Dynamic plugin nodes ---- */
    Qs_Engine *engine = editor_engine(s_editor);
    if (!engine) goto no_plugins;

    {
        int pre_count      = 0;
        int post_geo_count = 0;
        int post_tm_count  = 0;

        uint32_t ext_count = qs_engine_ext_count(engine, QS_EXT_RENDER_GRAPH_NODE);
        for (uint32_t e = 0; e < ext_count; e++) {
            const Qs_RgNodeTypeExt *ext =
                qs_engine_ext_interface(engine, QS_EXT_RENDER_GRAPH_NODE, e);
            if (!ext || !ext->type) continue;

            int ni;
            switch (ext->stage) {
                case QS_RG_STAGE_PRE_GEOMETRY:
                    ni = add_node(ext->type, ext->type->name, false, ext->stage,
                                   50.0f, 50.0f + pre_count * 200.0f);
                    if (ni >= 0) add_conn(ni, 0, pbr_i, 0);
                    pre_count++;
                    break;

                case QS_RG_STAGE_POST_GEOMETRY:
                    ni = add_node(ext->type, ext->type->name, false, ext->stage,
                                   370.0f, 450.0f + post_geo_count * 200.0f);
                    if (ni >= 0) {
                        add_conn(pbr_i, 0, ni, 0);
                        add_conn(ni, 0, tm_i, 1);
                    }
                    post_geo_count++;
                    break;

                case QS_RG_STAGE_POST_TONEMAP:
                    ni = add_node(ext->type, ext->type->name, false, ext->stage,
                                   680.0f, 450.0f + post_tm_count * 200.0f);
                    if (ni >= 0) add_conn(tm_i, 0, ni, 0);
                    post_tm_count++;
                    break;

                default:
                    break;
            }
        }

        /* No POST_GEOMETRY node: connect pbr directly to tonemap */
        if (post_geo_count == 0)
            add_conn(pbr_i, 0, tm_i, 0);
    }

no_plugins:;
}

/* ================================================================
   PUBLIC API
   ================================================================ */

void ed_node_graph_init(void *editor)
{
    s_editor = (Editor *)editor;
    ca_node_graph_init(&s_ng);
    s_ng.pan_x = 20.0f;
    s_ng.pan_y = 20.0f;
    setup_graph();

    /* Seed pin counts into ng node state so wire routing works on frame 1,
     * before ca_ng_node_end has had a chance to record them. We replicate
     * what ng_find_or_create does: add a slot for each node with the
     * correct canvas position and pin counts. */
    for (int i = 0; i < s_node_count; i++) {
        const NgNodeInfo *info = &s_nodes[i];
        if (!info->type) continue;
        if (s_ng.node_count >= CA_NG_MAX_NODES) break;
        Ca_NgNodeState *st = &s_ng.nodes[s_ng.node_count++];
        snprintf(st->key, CA_NG_KEY_LEN, "%s", info->name);
        st->canvas_x     = info->init_x;
        st->canvas_y     = info->init_y;
        st->input_count  = (int)info->type->input_count;
        st->output_count = (int)info->type->output_count;
        st->valid        = true;
        st->_ng          = &s_ng;
    }
}

void ed_node_graph_build(void)
{
    ca_split_begin(&(Ca_SplitDesc){
        .direction       = CA_HORIZONTAL,
        .ratio           = s_split_ratio,
        .min_ratio       = 0.50f,
        .max_ratio       = 0.75f,
        .bar_size        = 1.0f,
        .bar_color       = ca_color(0.271f, 0.278f, 0.353f, 1.0f), /* ED_COL_SEPARATOR */
        .bar_hover_color = CA_THEME_ACCENT,
        .on_resize       = on_split_resize,
        .style           = "ng-split",
    });
    {
        /* LEFT PANE: canvas host div — builder emits the Ca_NodeGraph */
        s_canvas_host = ca_div_begin(&(Ca_DivDesc){
            .id        = "ng-canvas",
            .direction = CA_VERTICAL,
            .style     = "ng-canvas-host",
        });
        ca_div_set_builder(s_canvas_host, canvas_builder, NULL);
        ca_div_end();

        /* RIGHT PANE: properties panel — builder emits settings widgets */
        s_props_div = ca_div_begin(&(Ca_DivDesc){
            .id        = "ng-props",
            .direction = CA_VERTICAL,
            .style     = "ng-props-panel",
        });
        ca_div_set_builder(s_props_div, props_builder, NULL);
        ca_div_end();
    }
    ca_split_end();
}

void ed_node_graph_shutdown(void)
{
    s_canvas_host = NULL;
    s_props_div   = NULL;
    memset(&s_ng,    0, sizeof(s_ng));
    memset(s_nodes,  0, sizeof(s_nodes));
    memset(s_conns,  0, sizeof(s_conns));
    s_node_count = 0;
    s_conn_count = 0;
    s_editor     = NULL;
}
