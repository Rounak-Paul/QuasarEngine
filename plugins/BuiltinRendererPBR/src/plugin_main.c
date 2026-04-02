#include "quasar.h"
#include "qs_plugin.h"
#include "pbr_internal.h"

#include <stdio.h>

/* Backends defined in their respective pbr_*.c files */
extern const Qs_RendererBackend pbr_renderer_backend;

/* ---- Plugin globals ---- */
static Qs_Engine  *s_engine          = NULL;
static Ca_Window  *s_renderer_win    = NULL;
static Ca_Label   *s_lbl_fps         = NULL;
static Ca_Label   *s_lbl_frametime   = NULL;
static Ca_Label   *s_lbl_bloom       = NULL;
static Ca_Label   *s_lbl_vignette    = NULL;

/* ---- on_frame: update stat labels ---- */

static void renderer_win_frame(void *data)
{
    (void)data;
    float dt  = s_engine ? qs_engine_dt(s_engine) : 0.0f;
    float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
    float ms  = dt * 1000.0f;

    char buf[64];
    snprintf(buf, sizeof(buf), "FPS:         %.1f", fps);
    ca_set_text(s_lbl_fps, buf);

    snprintf(buf, sizeof(buf), "Frame Time:  %.2f ms", ms);
    ca_set_text(s_lbl_frametime, buf);

    PbrPostProcessSettings *pp = pbr_post_process_settings();
    if (pp) {
        snprintf(buf, sizeof(buf), "Bloom:       %.3f", pp->bloom_strength);
        ca_set_text(s_lbl_bloom, buf);

        snprintf(buf, sizeof(buf), "Vignette:    %.3f", pp->vignette_strength);
        ca_set_text(s_lbl_vignette, buf);
    }
}

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

/* ---- Window builder ---- */

static void open_renderer_window(void *user_data)
{
    (void)user_data;
    if (s_renderer_win && ca_window_is_open(s_renderer_win)) return;
    if (!s_engine) return;

    Ca_Instance *inst = ca_window_instance(qs_engine_window(s_engine));
    if (!inst) return;

    PbrPostProcessSettings *pp = pbr_post_process_settings();

    s_renderer_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = "Renderer",
        .width  = 360,
        .height = 400,
    });
    if (!s_renderer_win) return;

    ca_ui_begin(s_renderer_win, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "renderer-settings-panel",
    });
    {
        ca_hr(&(Ca_HrDesc){ .color = 0 });

        /* ---- Settings section ---- */
        ca_text(&(Ca_TextDesc){
            .text  = "Settings",
            .style = "renderer-section-label",
        });

        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "renderer-setting-row",
        });
        ca_text(&(Ca_TextDesc){
            .text  = "Bloom Strength",
            .style = "renderer-setting-label",
        });
        ca_slider(&(Ca_SliderDesc){
            .min       = 0.0f,
            .max       = 1.0f,
            .value     = pp ? pp->bloom_strength : 0.0f,
            .on_change = on_bloom_change,
        });
        ca_div_end();

        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "renderer-setting-row",
        });
        ca_text(&(Ca_TextDesc){
            .text  = "Vignette Strength",
            .style = "renderer-setting-label",
        });
        ca_slider(&(Ca_SliderDesc){
            .min       = 0.0f,
            .max       = 1.0f,
            .value     = pp ? pp->vignette_strength : 0.0f,
            .on_change = on_vignette_change,
        });
        ca_div_end();

        ca_hr(&(Ca_HrDesc){ .color = 0 });

        /* ---- Stats section ---- */
        ca_text(&(Ca_TextDesc){
            .text  = "Stats",
            .style = "renderer-section-label",
        });

        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "renderer-stats-content",
        });
        s_lbl_fps      = ca_text(&(Ca_TextDesc){ .text = "FPS:",        .style = "renderer-stat-row" });
        s_lbl_frametime = ca_text(&(Ca_TextDesc){ .text = "Frame Time:", .style = "renderer-stat-row" });
        s_lbl_bloom    = ca_text(&(Ca_TextDesc){ .text = "Bloom:",       .style = "renderer-stat-row" });
        s_lbl_vignette = ca_text(&(Ca_TextDesc){ .text = "Vignette:",    .style = "renderer-stat-row" });
        ca_div_end();
    }
    ca_ui_end();

    ca_window_set_on_frame(s_renderer_win, renderer_win_frame, NULL);
}

/* ---- Plugin lifecycle ---- */

static void on_load(Qs_Engine *engine)
{
    s_engine = engine;
    qs_renderer_backend_register(&pbr_renderer_backend);
}

static void on_unload(Qs_Engine *engine)
{
    (void)engine;
    if (s_renderer_win && ca_window_is_open(s_renderer_win))
        ca_window_close(s_renderer_win);
    s_renderer_win    = NULL;
    s_lbl_fps        = NULL;
    s_lbl_frametime  = NULL;
    s_lbl_bloom      = NULL;
    s_lbl_vignette   = NULL;
    s_engine         = NULL;
    qs_renderer_backend_unregister("PBRRenderer");
}

/* ---- Editor menu ---- */

static void on_editor_menu(Qs_Engine *engine, Ca_MenuItemDesc *items, int *count)
{
    (void)engine;
    items[0] = (Ca_MenuItemDesc){ "Renderer Panel...", open_renderer_window, NULL };
    *count = 1;
}

QS_PLUGIN_EXPORT const Qs_PluginDesc *qs_plugin_entry(void)
{
    static const Qs_PluginDesc desc = {
        .id             = "com.quasar.builtin.renderer.pbr",
        .name           = "BuiltinRendererPBR",
        .version        = "1.0.0",
        .author         = "QuasarEngine",
        .description    = "PBR renderer backend (Forward+, CSM shadows, bloom, ACES tonemap)",
        .api_version    = QS_PLUGIN_API_VERSION,
        .on_load        = on_load,
        .on_unload      = on_unload,
        .on_editor_menu = on_editor_menu,
    };
    return &desc;
}


