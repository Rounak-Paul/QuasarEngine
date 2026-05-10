/*
 * ed_project_settings.c — Project Settings window.
 *
 * A standalone window containing the render graph node graph view
 * and its associated properties panel.  The node graph is built once
 * and the widget layout is persistent (Causality retained mode).
 */

#include "ed_project_settings.h"
#include "ed_node_graph.h"
#include "../editor.h"
#include "../ed_style.h"

#include "causality.h"
#include "quasar.h"

static Editor    *s_editor;
static Ca_Window *s_win;

/* ================================================================
   WINDOW BUILD
   ================================================================ */

static void build_window_ui(void)
{
    ca_ui_begin(s_win, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "st-root",
    });

    /* Header bar */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "st-page-header",
    });
    ca_text(&(Ca_TextDesc){ .text = "Project Settings", .style = "st-page-title" });
    ca_div_begin(&(Ca_DivDesc){ .style = "pm-spacer" }); ca_div_end();
    ca_text(&(Ca_TextDesc){ .text = "Render Graph", .style = "ng-breadcrumb" });
    ca_div_end(); /* header */

    /* Node graph fills the rest */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "ng-root",
    });
    ed_node_graph_build();
    ca_div_end();

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
        .width  = 940,
        .height = 640,
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
