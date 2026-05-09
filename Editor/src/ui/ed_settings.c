#include "ed_settings.h"
#include "editor.h"
#include "../ed_style.h"
#include "../ed_commands.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   SETTINGS WINDOW
   ================================================================

   Two-pane layout — left sidebar (128 px) with vertical tab buttons,
   right content area that swaps pages on tab click.

   Tabs:
     0  Theme & Scale   – UI scale slider + future theme controls
     1  Keybinds        – table of registered keybinds

   The window shares the editor's Ca_Instance so the instance-level
   scale slider takes effect on all windows (main, plugin manager,
   file browser, etc.) immediately.
   ================================================================ */

/* ---- icons ---- */
#define ICON_THEME    "\xEF\x81\xAB"   /* U+F06B  tag/theme  */
#define ICON_KEYBOARD "\xEF\x84\x9C"   /* U+F11C  keyboard   */

/* ---- tab ids ---- */
typedef enum {
    TAB_THEME_SCALE = 0,
    TAB_KEYBINDS,
    SETTINGS_TAB_COUNT,
} SettingsTab;

/* ---- module state ---- */

static Editor    *s_editor;
static Ca_Window *s_win;

/* static UI handles — set during build, used for tab switching */
static Ca_Button *s_tab_btns  [SETTINGS_TAB_COUNT];
static Ca_Label  *s_tab_labels[SETTINGS_TAB_COUNT];
static Ca_Div    *s_pages     [SETTINGS_TAB_COUNT];

static SettingsTab s_active_tab;

/* Scale slider widget handle — needed to push value changes back */
static Ca_Slider  *s_scale_slider;
static Ca_Label   *s_scale_label;

/* Keybind list body — rebuilt each frame */
static Ca_Div     *s_kb_body;

/* ================================================================
   TAB SWITCHING
   ================================================================ */

static void switch_settings_tab(SettingsTab tab)
{
    s_active_tab = tab;

    for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
        bool active = (i == (int)tab);
        ca_set_hidden(s_pages[i], !active);
        ca_set_style(s_tab_btns[i],
            active ? "st-tab st-tab-active" : "st-tab");
        ca_set_style(s_tab_labels[i],
            active ? "st-tab-label st-tab-label-active" : "st-tab-label");
    }
}

/* ================================================================
   SCALE SLIDER CALLBACK
   ================================================================ */

static void on_scale_change(Ca_Slider *sl, void *ud)
{
    (void)ud;
    float v = ca_slider_get(sl);

    /* Apply to all windows via the instance */
    Ca_Window   *main = qs_engine_window(editor_engine(s_editor));
    Ca_Instance *inst = ca_window_instance(main);
    if (inst) ca_instance_set_scale(inst, v);

    /* Update the label */
    if (s_scale_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f\xC3\x97", v);   /* "1.40×" */
        ca_set_text(s_scale_label, buf);
    }
}

/* ================================================================
   SIDEBAR TAB CALLBACKS
   ================================================================ */

static void on_tab_theme(Ca_Button *btn, void *ud)
{
    (void)btn; (void)ud;
    switch_settings_tab(TAB_THEME_SCALE);
}

static void on_tab_keybinds(Ca_Button *btn, void *ud)
{
    (void)btn; (void)ud;
    switch_settings_tab(TAB_KEYBINDS);
}

/* ================================================================
   KEYBINDS FRAME REBUILD
   ================================================================ */

static void collect_kb(const EdKeybindInfo *info, void *ud)
{
    (void)ud;

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "st-kb-row",
    });
    ca_text(&(Ca_TextDesc){ .text = info->name,  .style = "st-kb-name" });
    ca_text(&(Ca_TextDesc){ .text = info->chord, .style = "st-kb-chord" });
    ca_div_end();
}

static void settings_frame(void *ud)
{
    (void)ud;

    /* Rebuild keybind list if that tab is active (cheap no-op otherwise
       since the div is hidden and Causality skips hidden subtrees). */
    if (s_kb_body) {
        ca_reconcile_begin(s_kb_body);
        ed_keybinds_iter(collect_kb, NULL);
        ca_div_end();
    }
}

/* ================================================================
   WINDOW BUILD
   ================================================================ */

static void build_settings_ui(float current_scale)
{
    s_active_tab = TAB_THEME_SCALE;

    ca_ui_begin(s_win, &(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "st-root",
    });

    /* ====== LEFT SIDEBAR ====== */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-sidebar",
    });

    /* Branding header */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-sidebar-header",
    });
    ca_text(&(Ca_TextDesc){ .text = "SETTINGS", .style = "st-sidebar-title" });
    ca_div_end();

    /* Tab buttons */
    static const char *labels[SETTINGS_TAB_COUNT] = {
        "Theme & Scale",
        "Keybinds",
    };
    static void (*cbs[SETTINGS_TAB_COUNT])(Ca_Button *, void *) = {
        on_tab_theme,
        on_tab_keybinds,
    };

    for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
        bool first = (i == 0);
        s_tab_btns[i] = ca_btn_begin(&(Ca_BtnDesc){
            .on_click = cbs[i],
            .style    = first ? "st-tab st-tab-active" : "st-tab",
        });
        s_tab_labels[i] = ca_text(&(Ca_TextDesc){
            .text  = labels[i],
            .style = first ? "st-tab-label st-tab-label-active"
                           : "st-tab-label",
        });
        ca_btn_end();
    }

    ca_div_end(); /* sidebar */

    /* ====== RIGHT CONTENT ====== */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-content",
    });

    /* ---- Tab 0: Theme & Scale ---- */
    s_pages[TAB_THEME_SCALE] = ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-page",
        .hidden    = false,
    });
    {
        /* Page header */
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-page-header" });
        ca_text(&(Ca_TextDesc){ .text = "Theme & Scale", .style = "st-page-title" });
        ca_div_end();

        /* Body */
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL, .style = "st-page-body" });
        {
            /* ---- Section: UI Scale ---- */
            ca_text(&(Ca_TextDesc){ .text = "UI Scale", .style = "st-section-header" });

            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-form-row" });
            {
                ca_text(&(Ca_TextDesc){ .text = "Scale", .style = "st-form-label" });

                /* Slider */
                s_scale_slider = ca_slider(&(Ca_SliderDesc){
                    .id          = "st-scale-slider",
                    .style       = "st-slider",
                    .min         = 0.5f,
                    .max         = 3.0f,
                    .value       = current_scale,
                    .on_change   = on_scale_change,
                });

                /* Value label */
                char init_lbl[16];
                snprintf(init_lbl, sizeof(init_lbl), "%.2f\xC3\x97", current_scale);
                s_scale_label = ca_text(&(Ca_TextDesc){
                    .text  = init_lbl,
                    .style = "st-scale-value",
                });
            }
            ca_div_end();
        }
        ca_div_end(); /* page-body */
    }
    ca_div_end(); /* page TAB_THEME_SCALE */

    /* ---- Tab 1: Keybinds ---- */
    s_pages[TAB_KEYBINDS] = ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-page",
        .hidden    = true,
    });
    {
        /* Page header */
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-page-header" });
        ca_text(&(Ca_TextDesc){ .text = "Keybinds", .style = "st-page-title" });
        ca_div_end();

        /* Column header */
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "st-kb-header" });
        ca_text(&(Ca_TextDesc){ .text = "Action",   .style = "st-kb-hdr st-kb-name" });
        ca_text(&(Ca_TextDesc){ .text = "Shortcut", .style = "st-kb-hdr st-kb-chord" });
        ca_div_end();

        ca_hr(&(Ca_HrDesc){ .style = "st-separator" });

        /* Scrollable body — rebuilt each frame by settings_frame() */
        s_kb_body = ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "st-kb-body",
        });
        ca_div_end();
    }
    ca_div_end(); /* page TAB_KEYBINDS */

    ca_div_end(); /* content */
    ca_ui_end();

    ca_window_set_on_frame(s_win, settings_frame, NULL);
}

/* ================================================================
   PUBLIC API
   ================================================================ */

void ed_settings_init(void *editor)
{
    s_editor = (Editor *)editor;
}

void ed_settings_open(void)
{
    if (s_win && ca_window_is_open(s_win)) return;
    if (!s_editor) return;

    Ca_Window  *main_win = qs_engine_window(editor_engine(s_editor));
    if (!main_win) return;
    Ca_Instance *inst = ca_window_instance(main_win);
    if (!inst) return;

    float current_scale = ca_instance_get_scale(inst);

    s_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = "Settings",
        .width  = 520,
        .height = 380,
    });
    if (!s_win) return;

    build_settings_ui(current_scale);
}
