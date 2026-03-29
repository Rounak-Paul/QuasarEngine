#include "quasar.h"
#include "qs_plugin.h"
#include "vk_renderer_internal.h"

#include <stdio.h>

/* Backends defined in their respective vk_*.c files */
extern const Qs_RendererBackend vk_renderer_backend;

/* ---- Plugin globals ---- */
static Qs_Engine  *s_engine         = NULL;
static Ca_Window  *s_settings_win   = NULL;
static Ca_Window  *s_stats_win      = NULL;
static Ca_Div     *s_stats_content  = NULL;  /* content div rebuilt each frame */

/* ---- Stats window on_frame: rebuild dynamic content ---- */

static void stats_win_frame(void *data)
{
    (void)data;
    if (!s_stats_content) return;

    float dt  = s_engine ? qs_engine_dt(s_engine) : 0.0f;
    float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
    float ms  = dt * 1000.0f;
    VkPostProcessSettings *pp = vk_post_process_settings();

    ca_div_clear(s_stats_content);

    char fps_buf[64];
    snprintf(fps_buf, sizeof(fps_buf), "FPS:         %.1f", fps);
    ca_text(&(Ca_TextDesc){ .text = fps_buf, .style = "renderer-stat-row" });

    char ms_buf[64];
    snprintf(ms_buf, sizeof(ms_buf), "Frame Time:  %.2f ms", ms);
    ca_text(&(Ca_TextDesc){ .text = ms_buf, .style = "renderer-stat-row" });

    if (pp) {
        char bloom_buf[64];
        snprintf(bloom_buf, sizeof(bloom_buf), "Bloom:       %.3f", pp->bloom_strength);
        ca_text(&(Ca_TextDesc){ .text = bloom_buf, .style = "renderer-stat-row" });

        char vig_buf[64];
        snprintf(vig_buf, sizeof(vig_buf), "Vignette:    %.3f", pp->vignette_strength);
        ca_text(&(Ca_TextDesc){ .text = vig_buf, .style = "renderer-stat-row" });
    }

    ca_div_end(); /* s_stats_content */
}

/* ---- Slider callbacks (settings window) ---- */

static void on_bloom_change(Ca_Slider *s, void *user_data)
{
    (void)user_data;
    vk_post_process_settings()->bloom_strength = ca_slider_get(s);
}

static void on_vignette_change(Ca_Slider *s, void *user_data)
{
    (void)user_data;
    vk_post_process_settings()->vignette_strength = ca_slider_get(s);
}

/* ---- Close callbacks ---- */

static void on_close_settings(Ca_Button *btn, void *user_data)
{
    (void)btn; (void)user_data;
    if (s_settings_win) ca_window_close(s_settings_win);
}

static void on_close_stats(Ca_Button *btn, void *user_data)
{
    (void)btn; (void)user_data;
    if (s_stats_win) ca_window_close(s_stats_win);
}

/* ---- Window builders ---- */

static void open_settings_window(void)
{
    if (s_settings_win && ca_window_is_open(s_settings_win)) return;
    if (!s_engine) return;

    Ca_Instance *inst = ca_window_instance(qs_engine_window(s_engine));
    if (!inst) return;

    VkPostProcessSettings *pp = vk_post_process_settings();

    s_settings_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = "Renderer Settings",
        .width  = 340,
        .height = 200,
    });
    if (!s_settings_win) return;

    ca_ui_begin(s_settings_win, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "renderer-settings-panel",
    });
    {
        /* Title bar */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "plugin-manager-titlebar",
        });
        ca_text(&(Ca_TextDesc){
            .text  = "Renderer Settings",
            .style = "plugin-manager-title",
        });
        ca_div_begin(&(Ca_DivDesc){ .style = "plugin-spacer" });
        ca_div_end();
        ca_btn(&(Ca_BtnDesc){
            .text       = "Close",
            .style      = "plugin-manager-close-btn",
            .on_click   = on_close_settings,
            .click_data = NULL,
        });
        ca_div_end();

        ca_hr(&(Ca_HrDesc){ .color = 0 });

        /* Bloom strength */
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

        /* Vignette strength */
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
    }
    ca_ui_end();
}

static void open_stats_window(void)
{
    if (s_stats_win && ca_window_is_open(s_stats_win)) return;
    if (!s_engine) return;

    Ca_Instance *inst = ca_window_instance(qs_engine_window(s_engine));
    if (!inst) return;

    s_stats_win     = NULL;
    s_stats_content = NULL;

    s_stats_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = "Render Stats",
        .width  = 280,
        .height = 160,
    });
    if (!s_stats_win) return;

    /* Build static shell with an empty content div rebuilt each frame */
    ca_ui_begin(s_stats_win, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "renderer-stats-panel",
    });
    {
        /* Title bar */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "plugin-manager-titlebar",
        });
        ca_text(&(Ca_TextDesc){
            .text  = "Render Stats",
            .style = "plugin-manager-title",
        });
        ca_div_begin(&(Ca_DivDesc){ .style = "plugin-spacer" });
        ca_div_end();
        ca_btn(&(Ca_BtnDesc){
            .text       = "Close",
            .style      = "plugin-manager-close-btn",
            .on_click   = on_close_stats,
            .click_data = NULL,
        });
        ca_div_end();

        ca_hr(&(Ca_HrDesc){ .color = 0 });

        /* Dynamic content rebuilt by stats_win_frame each tick */
        s_stats_content = ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "renderer-stats-content",
        });
        ca_div_end();
    }
    ca_ui_end();

    ca_window_set_on_frame(s_stats_win, stats_win_frame, NULL);
}

/* ---- Menu action callbacks ---- */

static void action_open_settings(void *user_data)
{
    (void)user_data;
    open_settings_window();
}

static void action_open_stats(void *user_data)
{
    (void)user_data;
    open_stats_window();
}

/* ---- Plugin lifecycle ---- */

static void on_load(Qs_Engine *engine)
{
    s_engine = engine;
    qs_renderer_backend_register(&vk_renderer_backend);
}

static void on_unload(Qs_Engine *engine)
{
    (void)engine;
    if (s_settings_win && ca_window_is_open(s_settings_win))
        ca_window_close(s_settings_win);
    if (s_stats_win && ca_window_is_open(s_stats_win))
        ca_window_close(s_stats_win);
    s_settings_win  = NULL;
    s_stats_win     = NULL;
    s_stats_content = NULL;
    s_engine        = NULL;
    qs_renderer_backend_unregister("VulkanRenderer");
}

/* ---- Editor menu ---- */

static void on_editor_menu(Qs_Engine *engine, Ca_MenuItemDesc *items, int *count)
{
    (void)engine;
    int n = 0;
    items[n++] = (Ca_MenuItemDesc){ "Renderer Settings...", action_open_settings, NULL };
    items[n++] = (Ca_MenuItemDesc){ "Render Stats...",      action_open_stats,    NULL };
    *count = n;
}

QS_PLUGIN_EXPORT const Qs_PluginDesc *qs_plugin_entry(void)
{
    static const Qs_PluginDesc desc = {
        .id             = "com.quasar.builtin.renderer.pbr",
        .name           = "BuiltinRendererPBR",
        .version        = "1.0.0",
        .author         = "QuasarEngine",
        .description    = "Vulkan PBR renderer backend (Forward+, CSM shadows, bloom, ACES tonemap)",
        .api_version    = QS_PLUGIN_API_VERSION,
        .on_load        = on_load,
        .on_unload      = on_unload,
        .on_editor_menu = on_editor_menu,
    };
    return &desc;
}

