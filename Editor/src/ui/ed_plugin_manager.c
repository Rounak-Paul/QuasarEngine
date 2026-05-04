#include "ed_plugin_manager.h"
#include "editor.h"
#include "../ed_style.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   PLUGIN MANAGER WINDOW  –  table layout
   ================================================================ */

#define ICON_RELOAD "\xEF\x80\xA1"  /* U+F021 refresh */

/* ---- Module state ---- */

static Editor    *s_editor;
static Ca_Window *s_win;
static Ca_Div    *s_body;

/* ---- Callback context ---- */

#define MAX_PLUGINS 64

typedef struct { char id[256]; } PluginCtx;

static PluginCtx s_toggle_ctx[MAX_PLUGINS];
static PluginCtx s_reload_ctx[MAX_PLUGINS];
static uint32_t  s_ctx_count;

/* ---- Callbacks ---- */

static void on_toggle(Ca_Toggle *t, void *user_data)
{
    if (!s_editor) return;
    PluginCtx *ctx = user_data;
    Qs_PluginManager *pm = qs_engine_plugin_manager(editor_engine(s_editor));
    if (!pm) return;
    if (ca_toggle_get(t))
        qs_plugin_enable(pm, ctx->id);
    else
        qs_plugin_disable(pm, ctx->id);
}

static void on_reload(Ca_Button *btn, void *user_data)
{
    (void)btn;
    if (!s_editor) return;
    PluginCtx *ctx = user_data;
    Qs_PluginManager *pm = qs_engine_plugin_manager(editor_engine(s_editor));
    if (pm) qs_plugin_reload(pm, ctx->id);
}

/* ---- Init ---- */

void ed_plugin_manager_init(void *editor)
{
    s_editor = (Editor *)editor;
}

/* ---- Helpers ---- */

static const char *status_label(bool enabled, bool loaded)
{
    if (loaded)  return "Active";
    if (enabled) return "Loading";
    return "Disabled";
}

static const char *status_style(bool enabled, bool loaded)
{
    if (loaded)  return "pm-status pm-status-active";
    if (enabled) return "pm-status pm-status-loading";
    return "pm-status pm-status-disabled";
}

/* ---- Per-frame content rebuild ---- */

static void plugin_manager_frame(void *data)
{
    (void)data;
    if (!s_body) return;

    Qs_PluginManager *pm = s_editor
        ? qs_engine_plugin_manager(editor_engine(s_editor)) : NULL;

    s_ctx_count = 0;
    ca_reconcile_begin(s_body);

    if (!pm || qs_plugin_count(pm) == 0) {
        ca_text(&(Ca_TextDesc){
            .text  = pm ? "No plugins loaded." : "Plugin manager unavailable.",
            .style = "pm-empty",
        });
        ca_div_end();
        return;
    }

    uint32_t count = qs_plugin_count(pm);
    for (uint32_t i = 0; i < count && s_ctx_count < MAX_PLUGINS; i++) {
        const Qs_PluginState *state = qs_plugin_state_at(pm, i);
        if (!state) continue;

        const char *pid     = qs_plugin_state_id(state);
        const char *name    = qs_plugin_state_name(state);
        const char *version = qs_plugin_state_version(state);
        const char *author  = qs_plugin_state_author(state);
        bool        enabled = qs_plugin_state_enabled(state);
        bool        loaded  = qs_plugin_state_loaded(state);

        if (!pid)  pid  = "unknown";
        if (!name) name = pid;

        uint32_t ci = s_ctx_count++;
        snprintf(s_toggle_ctx[ci].id, sizeof(s_toggle_ctx[ci].id), "%s", pid);
        snprintf(s_reload_ctx[ci].id, sizeof(s_reload_ctx[ci].id), "%s", pid);

        char row_key[128];
        snprintf(row_key, sizeof(row_key), "pm-row-%s", pid);

        /* ---- Table row ---- */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .id        = row_key,
            .style     = (i & 1) ? "pm-row pm-row-alt" : "pm-row",
        });
        {
            /* Col: Status */
            ca_text(&(Ca_TextDesc){
                .text  = status_label(enabled, loaded),
                .style = status_style(enabled, loaded),
            });

            /* Col: Name */
            ca_text(&(Ca_TextDesc){
                .text  = name,
                .style = "pm-cell-name",
            });

            /* Col: Version */
            ca_text(&(Ca_TextDesc){
                .text  = version ? version : "\xe2\x80\x94",
                .style = "pm-cell-version",
            });

            /* Col: Author */
            ca_text(&(Ca_TextDesc){
                .text  = author ? author : "\xe2\x80\x94",
                .style = "pm-cell-author",
            });

            /* Spacer */
            ca_div_begin(&(Ca_DivDesc){ .style = "pm-spacer" });
            ca_div_end();

            /* Col: Actions */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "pm-cell-actions",
            });
            {
                char reload_id[128], toggle_id[128];
                snprintf(reload_id, sizeof(reload_id), "pm-rel-%s", pid);
                snprintf(toggle_id, sizeof(toggle_id), "pm-tgl-%s", pid);

                if (loaded) {
                    ca_btn_begin(&(Ca_BtnDesc){
                        .text       = ICON_RELOAD,
                        .id         = reload_id,
                        .style      = "pm-reload-btn",
                        .on_click   = on_reload,
                        .click_data = &s_reload_ctx[ci],
                    });
                    ca_btn_end();
                }

                ca_toggle(&(Ca_ToggleDesc){
                    .on          = enabled,
                    .id          = toggle_id,
                    .style       = "pm-toggle",
                    .on_change   = on_toggle,
                    .change_data = &s_toggle_ctx[ci],
                });
            }
            ca_div_end();
        }
        ca_div_end();
    }

    ca_div_end();
}

/* ---- Open ---- */

void ed_plugin_manager_open(void)
{
    if (s_win && ca_window_is_open(s_win)) return;
    if (!s_editor) return;

    Ca_Window  *main_win = qs_engine_window(editor_engine(s_editor));
    if (!main_win) return;
    Ca_Instance *inst = ca_window_instance(main_win);
    if (!inst) return;

    s_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = "Plugin Manager",
        .width  = 560,
        .height = 360,
    });
    if (!s_win) return;

    ca_window_set_scale(s_win, ED_UI_SCALE);

    ca_ui_begin(s_win, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "pm-root",
    });
    {
        /* Column header */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "pm-header",
        });
        {
            ca_text(&(Ca_TextDesc){ .text = "Status",  .style = "pm-hdr pm-col-status" });
            ca_text(&(Ca_TextDesc){ .text = "Name",    .style = "pm-hdr pm-col-name" });
            ca_text(&(Ca_TextDesc){ .text = "Version", .style = "pm-hdr pm-col-version" });
            ca_text(&(Ca_TextDesc){ .text = "Author",  .style = "pm-hdr pm-col-author" });
            ca_div_begin(&(Ca_DivDesc){ .style = "pm-spacer" });
            ca_div_end();
            ca_text(&(Ca_TextDesc){ .text = "Actions", .style = "pm-hdr pm-col-actions" });
        }
        ca_div_end();

        /* Separator */
        ca_hr(&(Ca_HrDesc){ .style = "pm-separator" });

        /* Scrollable body */
        s_body = ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "pm-body",
        });
        ca_div_end();
    }
    ca_ui_end();

    ca_window_set_on_frame(s_win, plugin_manager_frame, NULL);
}
