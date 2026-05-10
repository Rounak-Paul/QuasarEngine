/*
 * ed_project_settings.c — Project Settings window.
 *
 * Two-pane layout: left sidebar (tabs), right content.
 * Follows the same pattern as ed_settings.c.
 *
 * Tabs:
 *   0  Render Graph  — Ca_NodeGraph canvas + properties panel
 */

#include "ed_project_settings.h"
#include "ed_node_graph.h"
#include "../editor.h"
#include "../ed_style.h"

#include "causality.h"
#include "quasar.h"

/* ---- tab ids ---- */
typedef enum {
    PS_TAB_RENDER_GRAPH = 0,
    PS_TAB_COUNT,
} PsTab;

/* ---- module state ---- */
static Editor    *s_editor;
static Ca_Window *s_win;

static Ca_Button *s_tab_btns  [PS_TAB_COUNT];
static Ca_Label  *s_tab_labels[PS_TAB_COUNT];
static Ca_Div    *s_pages     [PS_TAB_COUNT];

static PsTab s_active_tab;

/* ================================================================
   TAB SWITCHING
   ================================================================ */

static void switch_ps_tab(PsTab tab)
{
    s_active_tab = tab;
    for (int i = 0; i < PS_TAB_COUNT; i++) {
        bool active = (i == (int)tab);
        ca_set_hidden(s_pages[i], !active);
        ca_set_style(s_tab_btns[i],
            active ? "st-tab st-tab-active" : "st-tab");
        ca_set_style(s_tab_labels[i],
            active ? "st-tab-label st-tab-label-active" : "st-tab-label");
    }
}

/* ================================================================
   TAB CALLBACKS
   ================================================================ */

static void on_tab_render_graph(Ca_Button *btn, void *ud)
{
    (void)btn; (void)ud;
    switch_ps_tab(PS_TAB_RENDER_GRAPH);
}

/* ================================================================
   WINDOW BUILD
   ================================================================ */

static void build_window_ui(void)
{
    s_active_tab = PS_TAB_RENDER_GRAPH;

    ca_ui_begin(s_win, &(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "st-root",
    });

    /* ====== LEFT SIDEBAR ====== */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-sidebar",
    });

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-sidebar-header",
    });
    ca_text(&(Ca_TextDesc){ .text = "PROJECT", .style = "st-sidebar-title" });
    ca_div_end();

    static const char *labels[PS_TAB_COUNT] = { "Render Graph" };
    static void (*cbs[PS_TAB_COUNT])(Ca_Button *, void *) = { on_tab_render_graph };

    for (int i = 0; i < PS_TAB_COUNT; i++) {
        bool first = (i == 0);
        s_tab_btns[i] = ca_btn_begin(&(Ca_BtnDesc){
            .on_click = cbs[i],
            .style    = first ? "st-tab st-tab-active" : "st-tab",
        });
        s_tab_labels[i] = ca_text(&(Ca_TextDesc){
            .text  = labels[i],
            .style = first ? "st-tab-label st-tab-label-active" : "st-tab-label",
        });
        ca_btn_end();
    }

    ca_div_end(); /* sidebar */

    /* ====== RIGHT CONTENT ====== */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-content",
    });

    /* ---- Tab 0: Render Graph ---- */
    s_pages[PS_TAB_RENDER_GRAPH] = ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-page",
        .hidden    = false,
    });
    {
        /* Compact page header */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "st-page-header",
        });
        ca_text(&(Ca_TextDesc){ .text = "Render Graph", .style = "st-page-title" });
        ca_div_end();

        /* Node graph fills the rest */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "ng-root",
        });
        ed_node_graph_build();
        ca_div_end();
    }
    ca_div_end(); /* page PS_TAB_RENDER_GRAPH */

    ca_div_end(); /* content */
    ca_ui_end();
}

/* ================================================================
   PUBLIC API
   ================================================================ */

void ed_project_settings_init(void *editor)
{
    s_editor = (Editor *)editor;
    ed_node_graph_init(editor);
}

void ed_project_settings_open(void)
{
    if (s_win && ca_window_is_open(s_win)) return;
    if (!s_editor) return;

    Ca_Window  *main_win = qs_engine_window(editor_engine(s_editor));
    if (!main_win) return;
    Ca_Instance *inst = ca_window_instance(main_win);
    if (!inst) return;

    s_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = "Project Settings",
        .width  = 980,
        .height = 660,
    });
    if (!s_win) return;

    float scale = ca_window_get_scale(main_win);
    ca_window_set_scale(s_win, scale);

    build_window_ui();
}

void ed_project_settings_shutdown(void)
{
    ed_node_graph_shutdown();
    s_win    = NULL;
    s_editor = NULL;
}
