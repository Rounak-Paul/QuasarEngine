#include "editor.h"
#include "ui/ed_menu_bar.h"
#include "ui/ed_toolbar.h"
#include "ui/ed_layout.h"
#include "ui/ed_status_bar.h"

#include <stdlib.h>

struct Editor {
    Qs_Engine     *engine;
    Ca_Instance   *instance;
    Ca_Window     *window;
    Ca_Stylesheet *stylesheet;
};

/* ---- Editor CSS theme ---- */

static const char *g_editor_css =

    /* Root — dark indigo background */
    ".editor-root {"
    "  background: #1a1a2e;"
    "  gap: 0px;"
    "  overflow: hidden;"
    "}"

    /* Menu bar — slim, dark */
    ".menu-bar {"
    "  background: #16162a;"
    "  width: 100%;"
    "  height: 22px;"
    "}"

    /* Menu bar header items */
    ".menu-bar-item {"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  align-items: center;"
    "}"

    /* Toolbar — icon row */
    ".toolbar {"
    "  background: #16162a;"
    "  width: 100%;"
    "  height: 18px;"
    "  align-items: center;"
    "  padding-left: 4px;"
    "  padding-right: 4px;"
    "  border-width: 1px;"
    "  border-color: #12122a;"
    "}"

    /* Panels */
    ".panel {"
    "  background: #16162a;"
    "  overflow: hidden;"
    "}"

    ".panel-viewport {"
    "  background: #1a1a2e;"
    "}"

    ".panel-bottom {"
    "  background: #16162a;"
    "}"

    /* Panel tab bars (row of tabs at top of each panel) */
    ".panel-tab-bar {"
    "  background: #12122a;"
    "  height: 24px;"
    "  width: 100%;"
    "  align-items: center;"
    "  padding-left: 4px;"
    "  gap: 2px;"
    "}"

    /* Panel tabs (individual tab labels) */
    ".panel-tab {"
    "  color: #555566;"
    "  font-size: 11px;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "}"

    ".active {"
    "  color: #b0b0cc;"
    "}"

    /* Status bar */
    ".status-bar {"
    "  background: #12122a;"
    "  width: 100%;"
    "  height: 20px;"
    "  align-items: center;"
    "  padding-left: 8px;"
    "}"

    ".status-text {"
    "  color: #6a6a88;"
    "  font-size: 11px;"
    "}"
;

static void editor_build_ui(Editor *ed)
{
    ca_ui_begin(ed->window, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "editor-root",
    });

    ed_menu_bar(ed->window, ed);
    ed_toolbar(ed->window, ed);
    ed_layout(ed->window, ed);
    ed_status_bar(ed->window, ed);

    ca_ui_end();
}

Editor *editor_create(const EditorDesc *desc)
{
    Editor *ed = calloc(1, sizeof(Editor));
    if (!ed) return NULL;

    ed->engine = qs_engine_create(&(Qs_EngineDesc){
        .app_name      = desc->title ? desc->title : "Quasar Editor",
        .version_major = 0,
        .version_minor = 1,
        .version_patch = 0,
    });
    if (!ed->engine) {
        free(ed);
        return NULL;
    }

    ed->instance = ca_instance_create(&(Ca_InstanceDesc){
        .app_name     = desc->title ? desc->title : "Quasar Editor",
        .font_size_px = 14.0f,
    });
    if (!ed->instance) {
        qs_engine_destroy(ed->engine);
        free(ed);
        return NULL;
    }

    ed->stylesheet = ca_css_parse(g_editor_css);
    if (ed->stylesheet)
        ca_instance_set_stylesheet(ed->instance, ed->stylesheet);

    ed->window = ca_window_create(ed->instance, &(Ca_WindowDesc){
        .title  = desc->title ? desc->title : "Quasar Editor",
        .width  = desc->width  > 0 ? desc->width  : 1280,
        .height = desc->height > 0 ? desc->height : 720,
    });
    if (!ed->window) {
        if (ed->stylesheet) ca_css_destroy(ed->stylesheet);
        ca_instance_destroy(ed->instance);
        qs_engine_destroy(ed->engine);
        free(ed);
        return NULL;
    }

    editor_build_ui(ed);
    return ed;
}

int editor_run(Editor *ed)
{
    if (!ed) return 1;
    return ca_instance_exec(ed->instance);
}

void editor_request_exit(Editor *ed)
{
    if (ed && ed->window)
        ca_window_close(ed->window);
}

void editor_destroy(Editor *ed)
{
    if (!ed) return;
    if (ed->stylesheet) ca_css_destroy(ed->stylesheet);
    qs_engine_destroy(ed->engine);
    free(ed);
}
