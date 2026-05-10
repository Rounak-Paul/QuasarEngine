#include "ed_renderer_settings.h"
#include "../editor.h"

#include "causality.h"
#include "quasar.h"

/* ================================================================
   RENDERER SETTINGS WINDOW
   ================================================================

   Two sections:
     - Post Processing: Bloom and Vignette strength sliders.
     - Anti-Aliasing:   MSAA sample count selector.

   Reads and writes Qs_PostProcessSettings on the scene renderer via
   qs_renderer_set_post_process / qs_renderer_post_process.
   ================================================================ */

static Editor    *s_editor;
static Ca_Window *s_win;

/* ----------------------------------------------------------------
   Slider / select callbacks
   ---------------------------------------------------------------- */

static void on_bloom_strength_change(Ca_Slider *s, void *user_data)
{
    (void)user_data;
    Qs_Renderer *r = editor_scene_renderer(s_editor);
    if (!r) return;
    Qs_PostProcessSettings pp = *qs_renderer_post_process(r);
    pp.bloom_strength = ca_slider_get(s);
    qs_renderer_set_post_process(r, &pp);
}

static void on_bloom_threshold_change(Ca_Slider *s, void *user_data)
{
    (void)user_data;
    Qs_Renderer *r = editor_scene_renderer(s_editor);
    if (!r) return;
    Qs_PostProcessSettings pp = *qs_renderer_post_process(r);
    pp.bloom_threshold = ca_slider_get(s);
    qs_renderer_set_post_process(r, &pp);
}

static void on_vignette_change(Ca_Slider *s, void *user_data)
{
    (void)user_data;
    Qs_Renderer *r = editor_scene_renderer(s_editor);
    if (!r) return;
    Qs_PostProcessSettings pp = *qs_renderer_post_process(r);
    pp.vignette_strength = ca_slider_get(s);
    qs_renderer_set_post_process(r, &pp);
}

static void on_msaa_select(Ca_Select *sel, void *user_data)
{
    (void)user_data;
    static const uint32_t k_counts[4] = {1, 2, 4, 8};
    int idx = ca_select_get(sel);
    if (idx < 0 || idx >= 4) return;
    Qs_Renderer *r = editor_scene_renderer(s_editor);
    if (!r) return;
    Qs_PostProcessSettings pp = *qs_renderer_post_process(r);
    pp.msaa_sample_count = k_counts[idx];
    qs_renderer_set_post_process(r, &pp);
}

/* ----------------------------------------------------------------
   Window builder
   ---------------------------------------------------------------- */

static void build_window_ui(void)
{
    Qs_Renderer *r = editor_scene_renderer(s_editor);

    static const char    *k_labels[4] = {"Off", "2x", "4x", "8x"};
    static const uint32_t k_counts[4] = {1, 2, 4, 8};

    const Qs_PostProcessSettings *pp      = r ? qs_renderer_post_process(r) : NULL;
    uint32_t                      dev_max = r ? qs_renderer_max_msaa_samples(r) : 1;

    /* Count valid MSAA options based on device support */
    int n_opts = 1;
    for (int i = 1; i < 4; i++) {
        if (k_counts[i] <= dev_max) n_opts = i + 1;
    }

    /* Find currently selected MSAA index */
    uint32_t cur_msaa = pp ? pp->msaa_sample_count : 4;
    int sel_idx = 0;
    for (int i = 0; i < n_opts; i++) {
        if (k_counts[i] == cur_msaa) sel_idx = i;
    }

    ca_ui_begin(s_win, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-root",
    });
    {
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-page-header" });
        ca_text(&(Ca_TextDesc){ .text = "Renderer Settings", .style = "st-page-title" });
        ca_div_end();

        ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL, .style = "st-page-body" });
        {
            /* ---- Post Processing ---- */
            ca_text(&(Ca_TextDesc){ .text = "POST PROCESSING", .style = "st-section-header" });

            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-form-row" });
            ca_text(&(Ca_TextDesc){ .text = "Bloom Strength", .style = "rnd-field-label" });
            ca_slider(&(Ca_SliderDesc){
                .min       = 0.0f,
                .max       = 1.0f,
                .value     = pp ? pp->bloom_strength : 0.04f,
                .on_change = on_bloom_strength_change,
                .style     = "rnd-slider",
            });
            ca_div_end();

            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-form-row" });
            ca_text(&(Ca_TextDesc){ .text = "Bloom Threshold", .style = "rnd-field-label" });
            ca_slider(&(Ca_SliderDesc){
                .min       = 0.0f,
                .max       = 2.0f,
                .value     = pp ? pp->bloom_threshold : 0.4f,
                .on_change = on_bloom_threshold_change,
                .style     = "rnd-slider",
            });
            ca_div_end();

            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-form-row" });
            ca_text(&(Ca_TextDesc){ .text = "Vignette", .style = "rnd-field-label" });
            ca_slider(&(Ca_SliderDesc){
                .min       = 0.0f,
                .max       = 1.0f,
                .value     = pp ? pp->vignette_strength : 0.35f,
                .on_change = on_vignette_change,
                .style     = "rnd-slider",
            });
            ca_div_end();

            /* ---- Anti-Aliasing ---- */
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

/* ================================================================
   PUBLIC API
   ================================================================ */

void ed_renderer_settings_open(void *editor)
{
    s_editor = (Editor *)editor;
    if (s_win && ca_window_is_open(s_win)) return;

    Qs_Engine   *engine = editor_engine(s_editor);
    Ca_Instance *inst   = ca_window_instance(qs_engine_window(engine));
    if (!inst) return;

    s_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = "Renderer Settings",
        .width  = 320,
        .height = 220,
    });
    if (!s_win) return;

    build_window_ui();
}

void ed_renderer_settings_shutdown(void)
{
    if (s_win && ca_window_is_open(s_win))
        ca_window_close(s_win);
    s_win    = NULL;
    s_editor = NULL;
}
