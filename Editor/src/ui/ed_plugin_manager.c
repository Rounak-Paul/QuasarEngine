#include "ed_plugin_manager.h"
#include "editor.h"
#include "../ed_style.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   PLUGIN MANAGER WINDOW  –  two-pane layout
   ================================================================
   Follows the Causality retained-mode pattern: build the widget
   tree ONCE at window creation (keeping all handles), then push
   state changes via ca_set_* from callbacks only.  No per-frame
   tree rebuilds.
   ================================================================ */

#define ICON_RELOAD  "\xEF\x80\xA1"  /* U+F021  refresh */
#define ICON_SEARCH  "\xEF\x80\x82"  /* U+F002  search  */
#define ICON_DOT     "\xE2\x97\x8F"  /* U+25CF  ●       */

#define MAX_PLUGINS 64

/* ---- Module state ---- */

static Editor       *s_editor;
static Ca_Window    *s_win;
static Ca_TextInput *s_search_input;
static char          s_selected_id[256];

/* ---- List item pool (pre-allocated at build time) ---- */

typedef struct {
    Ca_Button *btn;
    Ca_Label  *dot;
    Ca_Label  *name_lbl;
    Ca_Label  *ver_lbl;
    char       plugin_id[256];
    char       btn_id[32];
} PluginListItem;

static PluginListItem s_items[MAX_PLUGINS];

/* ---- Detail pane widget handles ---- */

static Ca_Div    *s_det_empty;       /* "select a plugin" placeholder     */
static Ca_Div    *s_det_content;     /* full detail view                  */
static Ca_Label  *s_det_name;
static Ca_Label  *s_det_status;
static Ca_Label  *s_det_desc;
static Ca_Div    *s_det_ver_row;
static Ca_Label  *s_det_ver_val;
static Ca_Div    *s_det_auth_row;
static Ca_Label  *s_det_auth_val;
static Ca_Label  *s_det_id_val;
static Ca_Button *s_det_reload_btn;
static Ca_Button *s_det_enable_btn;

/* ---- Init ---- */

void ed_plugin_manager_init(void *editor)
{
    s_editor = (Editor *)editor;
}

/* ---- Helpers ---- */

static bool matches_search(const char *name, const char *query)
{
    if (!query || query[0] == '\0') return true;
    if (!name) return false;
    size_t qlen = strlen(query);
    size_t nlen = strlen(name);
    if (qlen > nlen) return false;
    for (size_t i = 0; i <= nlen - qlen; i++) {
        size_t j;
        for (j = 0; j < qlen; j++) {
            char a = name[i + j], b = query[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (j == qlen) return true;
    }
    return false;
}

/* ---- Sync functions (imperatively push state into widgets) ---- */

static void sync_list(const char *query)
{
    Qs_PluginManager *pm = s_editor
        ? qs_engine_plugin_manager(editor_engine(s_editor)) : NULL;

    uint32_t slot = 0;
    if (pm) {
        uint32_t count = qs_plugin_count(pm);
        for (uint32_t i = 0; i < count && slot < MAX_PLUGINS; i++) {
            const Qs_PluginState *state = qs_plugin_state_at(pm, i);
            if (!state) continue;

            const char *pid  = qs_plugin_state_id(state);
            const char *name = qs_plugin_state_name(state);
            if (!pid)  pid  = "unknown";
            if (!name) name = pid;
            if (!matches_search(name, query)) continue;

            bool enabled  = qs_plugin_state_enabled(state);
            bool loaded   = qs_plugin_state_loaded(state);
            bool selected = s_selected_id[0]
                         && strcmp(s_selected_id, pid) == 0;

            const char *ver = qs_plugin_state_version(state);

            const char *dot_style = loaded  ? "pm-dot pm-dot-active"
                                  : enabled ? "pm-dot pm-dot-loading"
                                            : "pm-dot pm-dot-disabled";
            const char *btn_style = selected
                ? "pm-list-item pm-list-item-selected"
                : "pm-list-item";

            snprintf(s_items[slot].plugin_id, 256, "%s", pid);
            ca_set_text(s_items[slot].name_lbl, name);
            ca_set_text(s_items[slot].ver_lbl,  ver ? ver : "");
            ca_set_style(s_items[slot].dot, dot_style);
            ca_set_style(s_items[slot].btn, btn_style);
            ca_set_hidden(s_items[slot].btn, false);
            slot++;
        }
    }

    for (uint32_t i = slot; i < MAX_PLUGINS; i++)
        ca_set_hidden(s_items[i].btn, true);
}

static void sync_detail(void)
{
    Qs_PluginManager *pm = s_editor
        ? qs_engine_plugin_manager(editor_engine(s_editor)) : NULL;

    if (!pm || s_selected_id[0] == '\0') {
        ca_set_hidden(s_det_empty,   false);
        ca_set_hidden(s_det_content, true);
        return;
    }

    const Qs_PluginState *state = NULL;
    uint32_t count = qs_plugin_count(pm);
    for (uint32_t i = 0; i < count; i++) {
        const Qs_PluginState *st = qs_plugin_state_at(pm, i);
        if (!st) continue;
        const char *pid = qs_plugin_state_id(st);
        if (pid && strcmp(pid, s_selected_id) == 0) { state = st; break; }
    }

    if (!state) {
        ca_set_hidden(s_det_empty,   false);
        ca_set_hidden(s_det_content, true);
        return;
    }

    const char *pid         = qs_plugin_state_id(state);
    const char *name        = qs_plugin_state_name(state);
    const char *version     = qs_plugin_state_version(state);
    const char *author      = qs_plugin_state_author(state);
    bool        enabled     = qs_plugin_state_enabled(state);
    bool        loaded      = qs_plugin_state_loaded(state);
    const Qs_PluginDesc *d  = qs_plugin_state_desc(state);
    const char *description = d ? d->description : NULL;

    if (!pid)  pid  = "unknown";
    if (!name) name = pid;

    const char *status_text  = loaded  ? "Active"
                             : enabled ? "Loading"
                                       : "Disabled";
    const char *status_style = loaded  ? "pm-status pm-status-active"
                             : enabled ? "pm-status pm-status-loading"
                                       : "pm-status pm-status-disabled";

    ca_set_text(s_det_name,   name);
    ca_set_text(s_det_status, status_text);
    ca_set_style(s_det_status, status_style);

    bool has_desc = description && description[0];
    ca_set_text(s_det_desc, has_desc ? description : "No description provided.");
    ca_set_style(s_det_desc, has_desc ? "pm-detail-desc"
                                      : "pm-detail-desc pm-detail-desc-none");

    if (version) {
        ca_set_text(s_det_ver_val, version);
        ca_set_hidden(s_det_ver_row, false);
    } else {
        ca_set_hidden(s_det_ver_row, true);
    }

    if (author) {
        ca_set_text(s_det_auth_val, author);
        ca_set_hidden(s_det_auth_row, false);
    } else {
        ca_set_hidden(s_det_auth_row, true);
    }

    ca_set_text(s_det_id_val, pid);

    ca_set_hidden(s_det_reload_btn, !loaded);
    ca_set_text(s_det_enable_btn, enabled ? "Disable" : "Enable");
    ca_set_style(s_det_enable_btn,
        enabled ? "pm-action-btn pm-action-disable"
                : "pm-action-btn pm-action-enable");

    ca_set_hidden(s_det_empty,   true);
    ca_set_hidden(s_det_content, false);
}

/* ---- Callbacks ---- */

static void on_plugin_select(Ca_Button *btn, void *user_data)
{
    (void)btn;
    PluginListItem *item = user_data;
    snprintf(s_selected_id, sizeof(s_selected_id), "%s", item->plugin_id);

    const char *query = s_search_input ? ca_get_text(s_search_input) : "";
    if (!query) query = "";
    sync_list(query);
    sync_detail();
}

static void on_enable_toggle(Ca_Button *btn, void *user_data)
{
    (void)btn; (void)user_data;
    if (!s_editor || s_selected_id[0] == '\0') return;
    Qs_PluginManager *pm = qs_engine_plugin_manager(editor_engine(s_editor));
    if (!pm) return;

    uint32_t count = qs_plugin_count(pm);
    for (uint32_t i = 0; i < count; i++) {
        const Qs_PluginState *state = qs_plugin_state_at(pm, i);
        if (!state) continue;
        const char *pid = qs_plugin_state_id(state);
        if (!pid || strcmp(pid, s_selected_id) != 0) continue;
        if (qs_plugin_state_enabled(state))
            qs_plugin_disable(pm, s_selected_id);
        else
            qs_plugin_enable(pm, s_selected_id);
        break;
    }

    const char *query = s_search_input ? ca_get_text(s_search_input) : "";
    if (!query) query = "";
    sync_list(query);
    sync_detail();
}

static void on_reload(Ca_Button *btn, void *user_data)
{
    (void)btn; (void)user_data;
    if (!s_editor || s_selected_id[0] == '\0') return;
    Qs_PluginManager *pm = qs_engine_plugin_manager(editor_engine(s_editor));
    if (pm) qs_plugin_reload(pm, s_selected_id);

    const char *query = s_search_input ? ca_get_text(s_search_input) : "";
    if (!query) query = "";
    sync_list(query);
    sync_detail();
}

static void on_search_change(Ca_TextInput *input, void *user_data)
{
    (void)user_data;
    const char *query = ca_get_text(input);
    if (!query) query = "";
    sync_list(query);
}

/* ---- Open ---- */

void ed_plugin_manager_open(void)
{
    if (s_win && ca_window_is_open(s_win)) return;
    if (!s_editor) return;

    Ca_Window   *main_win = qs_engine_window(editor_engine(s_editor));
    if (!main_win) return;
    Ca_Instance *inst = ca_window_instance(main_win);
    if (!inst) return;

    s_selected_id[0] = '\0';

    s_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = "Plugin Manager",
        .width  = 680,
        .height = 440,
    });
    if (!s_win) return;

    ca_window_set_scale(s_win, ED_UI_SCALE);

    ca_ui_begin(s_win, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "pm-root",
    });
    {
        /* ---- Top bar ---- */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "pm-top-bar",
        });
        ca_text(&(Ca_TextDesc){ .text = "Plugin Manager", .style = "pm-top-title" });
        ca_div_end();

        /* ---- Main split ---- */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "pm-panes",
        });
        {
            /* ---- Left sidebar ---- */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "pm-left",
            });
            {
                /* Search bar */
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_HORIZONTAL,
                    .style     = "pm-search-bar",
                });
                s_search_input = ca_input(&(Ca_InputDesc){
                    .placeholder = ICON_SEARCH "  Search plugins...",
                    .style       = "pm-search-input",
                    .on_change   = on_search_change,
                });
                ca_div_end();

                /* Plugin list — pre-allocate MAX_PLUGINS slots, all hidden */
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_VERTICAL,
                    .style     = "pm-list",
                });
                for (uint32_t i = 0; i < MAX_PLUGINS; i++) {
                    snprintf(s_items[i].btn_id, sizeof(s_items[i].btn_id),
                             "pml-%u", i);
                    s_items[i].plugin_id[0] = '\0';

                    s_items[i].btn = ca_btn_begin(&(Ca_BtnDesc){
                        .id         = s_items[i].btn_id,
                        .style      = "pm-list-item",
                        .direction  = CA_HORIZONTAL,
                        .on_click   = on_plugin_select,
                        .click_data = &s_items[i],
                        .hidden     = true,
                    });
                    s_items[i].dot = ca_text(&(Ca_TextDesc){
                        .text  = ICON_DOT,
                        .style = "pm-dot pm-dot-disabled",
                    });
                    ca_div_begin(&(Ca_DivDesc){
                        .direction = CA_VERTICAL,
                        .style     = "pm-list-item-info",
                    });
                    s_items[i].name_lbl = ca_text(&(Ca_TextDesc){
                        .text  = "",
                        .style = "pm-list-item-name",
                    });
                    s_items[i].ver_lbl = ca_text(&(Ca_TextDesc){
                        .text  = "",
                        .style = "pm-list-item-ver",
                    });
                    ca_div_end();
                    ca_btn_end();
                }
                ca_div_end(); /* pm-list */
            }
            ca_div_end(); /* pm-left */

            /* Vertical divider */
            ca_div_begin(&(Ca_DivDesc){ .style = "pm-vert-divider" });
            ca_div_end();

            /* ---- Right detail pane ---- */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "pm-right",
            });
            {
                /* Empty state */
                s_det_empty = ca_div_begin(&(Ca_DivDesc){
                    .style = "pm-detail-empty-wrap",
                });
                ca_text(&(Ca_TextDesc){
                    .text  = "Select a plugin to view details.",
                    .style = "pm-detail-empty",
                });
                ca_div_end();

                /* Detail content (hidden until a plugin is selected) */
                s_det_content = ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_VERTICAL,
                    .style     = "pm-detail-content",
                    .hidden    = true,
                });
                {
                    /* Header bar */
                    ca_div_begin(&(Ca_DivDesc){
                        .direction = CA_HORIZONTAL,
                        .style     = "pm-detail-header",
                    });
                    s_det_name   = ca_text(&(Ca_TextDesc){ .text = "", .style = "pm-detail-name" });
                    s_det_status = ca_text(&(Ca_TextDesc){ .text = "", .style = "pm-status" });
                    ca_div_end();

                    ca_div_begin(&(Ca_DivDesc){ .style = "pm-detail-sep" }); ca_div_end();

                    /* Scrollable body */
                    ca_div_begin(&(Ca_DivDesc){
                        .direction = CA_VERTICAL,
                        .style     = "pm-detail-body",
                    });
                    {
                        s_det_desc = ca_text(&(Ca_TextDesc){
                            .text  = "",
                            .style = "pm-detail-desc",
                            .wrap  = true,
                        });

                        ca_div_begin(&(Ca_DivDesc){ .style = "pm-prop-sep" }); ca_div_end();

                        s_det_ver_row = ca_div_begin(&(Ca_DivDesc){
                            .direction = CA_HORIZONTAL,
                            .style     = "pm-prop-row",
                        });
                        ca_text(&(Ca_TextDesc){ .text = "Version", .style = "pm-prop-label" });
                        s_det_ver_val = ca_text(&(Ca_TextDesc){ .text = "", .style = "pm-prop-value" });
                        ca_div_end();

                        s_det_auth_row = ca_div_begin(&(Ca_DivDesc){
                            .direction = CA_HORIZONTAL,
                            .style     = "pm-prop-row",
                        });
                        ca_text(&(Ca_TextDesc){ .text = "Author", .style = "pm-prop-label" });
                        s_det_auth_val = ca_text(&(Ca_TextDesc){ .text = "", .style = "pm-prop-value" });
                        ca_div_end();

                        ca_div_begin(&(Ca_DivDesc){
                            .direction = CA_HORIZONTAL,
                            .style     = "pm-prop-row",
                        });
                        ca_text(&(Ca_TextDesc){ .text = "ID", .style = "pm-prop-label" });
                        s_det_id_val = ca_text(&(Ca_TextDesc){ .text = "", .style = "pm-prop-id" });
                        ca_div_end();
                    }
                    ca_div_end(); /* pm-detail-body */

                    ca_div_begin(&(Ca_DivDesc){ .style = "pm-detail-sep" }); ca_div_end();

                    /* Footer */
                    ca_div_begin(&(Ca_DivDesc){
                        .direction = CA_HORIZONTAL,
                        .style     = "pm-detail-footer",
                    });
                    {
                        s_det_reload_btn = ca_btn_begin(&(Ca_BtnDesc){
                            .text     = ICON_RELOAD "  Reload",
                            .style    = "pm-action-btn pm-action-reload",
                            .on_click = on_reload,
                            .hidden   = true,
                        });
                        ca_btn_end();

                        s_det_enable_btn = ca_btn_begin(&(Ca_BtnDesc){
                            .text     = "Enable",
                            .style    = "pm-action-btn pm-action-enable",
                            .on_click = on_enable_toggle,
                        });
                        ca_btn_end();
                    }
                    ca_div_end(); /* pm-detail-footer */
                }
                ca_div_end(); /* s_det_content */
            }
            ca_div_end(); /* pm-right */
        }
        ca_div_end(); /* pm-panes */
    }
    ca_ui_end();

    /* Initial population */
    sync_list("");
    sync_detail();
}

