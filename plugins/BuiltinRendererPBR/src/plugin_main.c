#include "quasar.h"
#include "qs_plugin.h"
#include "pbr_internal.h"

#include <stdio.h>

/* Backends defined in their respective pbr_*.c files */
extern const Qs_RendererBackend pbr_renderer_backend;

/* ---- Plugin globals ---- */
static Qs_Engine    *s_engine          = NULL;
static Qs_Extension *s_ext_menu        = NULL;
static Qs_Extension *s_ext_toolbar     = NULL;
static Ca_Window    *s_renderer_win    = NULL;

/* ---- Slider callbacks ---- */

static void on_bloom_change(Ca_Slider *s, void *user_data)
{
    (void)user_data;
    pbr_post_process_settings()->bloom_strength = ca_slider_get(s);
}

static void on_vignette_change(Ca_Slider *s, void *user_data)
{
    (void)user_data;
    pbr_post_process_settings()->vignette_strength = ca_slider_get(s);
}

static void on_msaa_select(Ca_Select *sel, void *user_data)
{
    (void)user_data;
    static const uint32_t k_counts[4] = {1, 2, 4, 8};
    int idx = ca_select_get(sel);
    if (idx >= 0 && idx < 4)
        pbr_post_process_settings()->msaa_sample_count = k_counts[idx];
}

/* ---- Window builder ---- */

static void open_renderer_window(void *user_data)
{
    (void)user_data;
    if (s_renderer_win && ca_window_is_open(s_renderer_win)) return;
    if (!s_engine) return;

    Ca_Instance *inst = ca_window_instance(qs_engine_window(s_engine));
    if (!inst) return;

    PbrPostProcessSettings *pp  = pbr_post_process_settings();
    PbrPassResources       *ps  = pbr_renderer_pass_resources();

    /* MSAA option list (hardware-capped) */
    static const char    *k_labels[4] = {"Off", "2x", "4x", "8x"};
    static const uint32_t k_counts[4] = {1, 2, 4, 8};
    uint32_t dev_max = ps ? ps->dev_max_samples : 1;
    int n_opts = 1;
    for (int i = 1; i < 4; i++) { if (k_counts[i] <= dev_max) n_opts = i + 1; }
    uint32_t cur = pp ? pp->msaa_sample_count : 1;
    int sel_idx = 0;
    for (int i = 0; i < n_opts; i++) { if (k_counts[i] == cur) sel_idx = i; }

    s_renderer_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = "Renderer Settings",
        .width  = 320,
        .height = 220,
    });
    if (!s_renderer_win) return;

    ca_ui_begin(s_renderer_win, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-root",
    });
    {
        /* ---- Page header ---- */
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-page-header" });
        ca_text(&(Ca_TextDesc){ .text = "Renderer Settings", .style = "st-page-title" });
        ca_div_end();

        /* ---- Scrollable body ---- */
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL, .style = "st-page-body" });
        {
            /* Post Processing */
            ca_text(&(Ca_TextDesc){ .text = "POST PROCESSING", .style = "st-section-header" });

            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-form-row" });
            ca_text(&(Ca_TextDesc){ .text = "Bloom",   .style = "rnd-field-label" });
            ca_slider(&(Ca_SliderDesc){
                .min       = 0.0f,
                .max       = 1.0f,
                .value     = pp ? pp->bloom_strength : 0.0f,
                .on_change = on_bloom_change,
                .style     = "rnd-slider",
            });
            ca_div_end();

            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-form-row" });
            ca_text(&(Ca_TextDesc){ .text = "Vignette", .style = "rnd-field-label" });
            ca_slider(&(Ca_SliderDesc){
                .min       = 0.0f,
                .max       = 1.0f,
                .value     = pp ? pp->vignette_strength : 0.0f,
                .on_change = on_vignette_change,
                .style     = "rnd-slider",
            });
            ca_div_end();

            /* Anti-Aliasing */
            ca_text(&(Ca_TextDesc){ .text = "ANTI-ALIASING", .style = "st-section-header" });

            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-form-row" });
            ca_text(&(Ca_TextDesc){ .text = "MSAA", .style = "rnd-field-label" });
            ca_div_begin(&(Ca_DivDesc){ .style = "pm-spacer" }); ca_div_end();
            ca_select(&(Ca_SelectDesc){
                .options      = k_labels,
                .option_count = n_opts,
                .selected     = sel_idx,
                .on_change    = on_msaa_select,
                .style        = "rnd-select",
            });
            ca_div_end();
        }
        ca_div_end();
    }
    ca_ui_end();
}

/* ---- Plugin lifecycle ---- */

/* Forward declarations for extension callbacks defined below */
static void pbr_get_menu_items(void *data, Qs_Engine *engine,
                               Ca_MenuItemDesc *items, int *count);
static void pbr_get_toolbar_items(void *data, Qs_Engine *engine,
                                  Qs_ToolbarItem *items, int *count);

static void on_load(Qs_Engine *engine)
{
    s_engine = engine;
    qs_renderer_backend_register(&pbr_renderer_backend);

    /* Register editor extensions */
    static const Qs_MenuExt menu_ext = {
        .label     = "BuiltinRendererPBR",
        .get_items = pbr_get_menu_items,
    };
    s_ext_menu = qs_engine_ext_register(engine, QS_EXT_EDITOR_MENU,
                                        &menu_ext, NULL);

    static const Qs_ToolbarExt toolbar_ext = {
        .get_items = pbr_get_toolbar_items,
    };
    s_ext_toolbar = qs_engine_ext_register(engine, QS_EXT_EDITOR_TOOLBAR,
                                           &toolbar_ext, NULL);
}

static void on_unload(Qs_Engine *engine)
{
    (void)engine;

    /* Unregister extensions */
    if (s_ext_toolbar) { qs_ext_unregister(s_ext_toolbar); s_ext_toolbar = NULL; }
    if (s_ext_menu)    { qs_ext_unregister(s_ext_menu);    s_ext_menu = NULL; }

    if (s_renderer_win && ca_window_is_open(s_renderer_win))
        ca_window_close(s_renderer_win);
    s_renderer_win    = NULL;
    s_engine         = NULL;
    qs_renderer_backend_unregister("PBRRenderer");
}

/* ---- Editor menu extension ---- */

static void pbr_get_menu_items(void *data, Qs_Engine *engine,
                               Ca_MenuItemDesc *items, int *count)
{
    (void)data;
    (void)engine;
    items[0] = (Ca_MenuItemDesc){ "Renderer Panel...", open_renderer_window, NULL };
    *count = 1;
}

/* ---- Editor toolbar extension ---- */

/* U+F096 — FA square-o: wireframe/outline rendering */
#define PBR_ICON_WIREFRAME  "\xEF\x82\x96"
/* U+F14E — FA compass: surface normals visualisation */
#define PBR_ICON_NORMALS    "\xEF\x85\x8E"

static void on_wireframe_click(Qs_Engine *engine, bool *active)
{
    (void)engine;
    Qs_Renderer *r = pbr_active_renderer();
    if (r) qs_renderer_set_wireframe(r, *active);
}

static void on_normals_click(Qs_Engine *engine, bool *active)
{
    (void)engine;
    Qs_Renderer *r = pbr_active_renderer();
    if (!r) return;
    uint32_t flags = qs_renderer_debug_flags(r);
    if (*active)
        flags |= PBR_DEBUG_SHOW_NORMALS;
    else
        flags &= ~PBR_DEBUG_SHOW_NORMALS;
    qs_renderer_set_debug_flags(r, flags);
}

static void pbr_get_toolbar_items(void *data, Qs_Engine *engine,
                                  Qs_ToolbarItem *items, int *count)
{
    (void)data;
    (void)engine;
    Qs_Renderer *r = pbr_active_renderer();
    int n = 0;

    items[n++] = (Qs_ToolbarItem){
        .icon     = PBR_ICON_WIREFRAME,
        .id       = "pbr-wireframe",
        .tooltip  = "Wireframe",
        .active   = r ? qs_renderer_wireframe(r) : false,
        .on_click = on_wireframe_click,
    };

    items[n++] = (Qs_ToolbarItem){
        .icon     = PBR_ICON_NORMALS,
        .id       = "pbr-normals",
        .tooltip  = "Show Normals",
        .active   = r ? (qs_renderer_debug_flags(r) & PBR_DEBUG_SHOW_NORMALS) != 0 : false,
        .on_click = on_normals_click,
    };

    *count = n;
}

QS_PLUGIN_EXPORT const Qs_PluginDesc *qs_plugin_entry(void)
{
    static const Qs_PluginDesc desc = {
        .id          = "com.quasar.builtin.renderer.pbr",
        .name        = "BuiltinRendererPBR",
        .version     = "1.0.0",
        .author      = "QuasarEngine",
        .description = "PBR renderer backend (Forward+, CSM shadows, bloom, ACES tonemap)",
        .api_version = QS_PLUGIN_API_VERSION,
        .on_load     = on_load,
        .on_unload   = on_unload,
    };
    return &desc;
}


