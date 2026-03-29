#include "ed_plugin_manager.h"
#include "editor.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   PLUGIN MANAGER — a separate Ca_Window listing all plugins.

   The window is created on demand (ed_plugin_manager_open).
   Its content div is torn down and rebuilt every frame using the
   window's on_frame callback so enable/disable/reload state is
   always current.  There is no overlay div and no modal inside the
   main editor window tree — the manager is entirely self-contained.
   ================================================================ */

/* Nerd Font icons */
#define ICON_PLUGIN    "\xEF\x87\x9E"   /* U+F1DE sliders     */
#define ICON_RELOAD    "\xEF\x80\xA1"   /* U+F021 refresh     */
#define ICON_ENABLED   "\xEF\x81\x99"   /* U+F059 circle-info */

/* ---- Module state ---- */

static Editor    *s_editor;
static Ca_Window *s_win;
static Ca_Div    *s_content_div;

/* ---- Toggle callbacks ---- */

typedef struct PluginToggleCtx {
    char id[256];
} PluginToggleCtx;

#define MAX_PLUGINS 64
static PluginToggleCtx s_toggle_ctx[MAX_PLUGINS];
static uint32_t        s_toggle_ctx_count;

typedef struct PluginReloadCtx {
    char id[256];
} PluginReloadCtx;

static PluginReloadCtx s_reload_ctx[MAX_PLUGINS];
static uint32_t        s_reload_ctx_count;

static void on_enable_toggle(Ca_Toggle *t, void *user_data)
{
    if (!s_editor) return;
    PluginToggleCtx *ctx = (PluginToggleCtx *)user_data;
    Qs_PluginManager *pm = qs_engine_plugin_manager(editor_engine(s_editor));
    if (!pm) return;
    bool on = ca_toggle_get(t);
    if (on)
        qs_plugin_enable(pm, ctx->id);
    else
        qs_plugin_disable(pm, ctx->id);
}

static void on_reload(Ca_Button *btn, void *user_data)
{
    (void)btn;
    if (!s_editor) return;
    PluginReloadCtx *ctx = (PluginReloadCtx *)user_data;
    Qs_PluginManager *pm = qs_engine_plugin_manager(editor_engine(s_editor));
    if (pm) qs_plugin_reload(pm, ctx->id);
}

static void on_close_win(Ca_Button *btn, void *user_data)
{
    (void)btn; (void)user_data;
    if (s_win) ca_window_close(s_win);
}

/* ================================================================
   INIT — call once after engine start to store context
   ================================================================ */

void ed_plugin_manager_init(void *editor)
{
    s_editor = (Editor *)editor;
}

/* ================================================================
   PER-FRAME CONTENT REBUILD — registered as window on_frame callback
   ================================================================ */

static void plugin_manager_win_frame(void *data)
{
    (void)data;
    if (!s_content_div) return;

    Qs_PluginManager *pm = s_editor
        ? qs_engine_plugin_manager(editor_engine(s_editor)) : NULL;

    /* Rebuild context arrays — max one per plugin */
    s_toggle_ctx_count = 0;
    s_reload_ctx_count = 0;

    ca_reconcile_begin(s_content_div);

    if (!pm) {
        ca_text(&(Ca_TextDesc){
            .text  = "No plugin manager.",
            .style = "plugin-manager-empty",
        });
        ca_div_end();
        return;
    }

    uint32_t count = qs_plugin_count(pm);
    if (count == 0) {
        ca_text(&(Ca_TextDesc){
            .text  = "No plugins discovered.",
            .style = "plugin-manager-empty",
        });
        ca_div_end();
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        const Qs_PluginState *state = qs_plugin_state_at(pm, i);
        if (!state) continue;

        const char *plugin_id = qs_plugin_state_id(state);
        if (!plugin_id) plugin_id = "plugin";

        const Qs_PluginDesc *desc = qs_plugin_state_desc(state);
        bool enabled = qs_plugin_state_enabled(state);
        bool loaded  = qs_plugin_state_loaded(state);

        /* Allocate stable toggle/reload context slots */
        PluginToggleCtx *tctx = NULL;
        PluginReloadCtx *rctx = NULL;
        if (s_toggle_ctx_count < MAX_PLUGINS) {
            tctx = &s_toggle_ctx[s_toggle_ctx_count++];
            const char *id = qs_plugin_state_id(state);
            snprintf(tctx->id, sizeof(tctx->id), "%s",
                     id ? id : "");
        }
        if (s_reload_ctx_count < MAX_PLUGINS) {
            rctx = &s_reload_ctx[s_reload_ctx_count++];
            const char *id = qs_plugin_state_id(state);
            snprintf(rctx->id, sizeof(rctx->id), "%s",
                     id ? id : "");
        }

        /* Row container */
        char row_id[96];
        snprintf(row_id, sizeof(row_id), "plugin-row-%s", plugin_id);
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .id        = row_id,
            .style     = "plugin-row",
        });
        {
            /* Top row: name + toggle + reload */
            char header_id[96];
            snprintf(header_id, sizeof(header_id), "plugin-row-header-%s", plugin_id);
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .id        = header_id,
                .style     = "plugin-row-header",
            });
            {
                /* Plugin name */
                const char *name = desc ? desc->name : qs_plugin_state_id(state);
                char label[128];
                snprintf(label, sizeof(label), "%s  %s",
                         ICON_PLUGIN, name ? name : "(unknown)");
                char name_id[96];
                snprintf(name_id, sizeof(name_id), "plugin-name-%s", plugin_id);
                ca_text(&(Ca_TextDesc){
                    .text  = label,
                    .id    = name_id,
                    .style = "plugin-name",
                });

                /* Version badge */
                if (desc && desc->version) {
                    char ver[64];
                    snprintf(ver, sizeof(ver), "v%s", desc->version);
                    char ver_id[96];
                    snprintf(ver_id, sizeof(ver_id), "plugin-version-%s", plugin_id);
                    ca_text(&(Ca_TextDesc){
                        .text  = ver,
                        .id    = ver_id,
                        .style = "plugin-version",
                    });
                }

                /* Spacer */
                ca_div_begin(&(Ca_DivDesc){ .style = "plugin-spacer" });
                ca_div_end();

                /* Status label */
                char status_id[96];
                snprintf(status_id, sizeof(status_id), "plugin-status-%s", plugin_id);
                ca_text(&(Ca_TextDesc){
                    .text  = loaded ? "loaded" : (enabled ? "loading..." : "disabled"),
                    .id    = status_id,
                    .style = loaded ? "plugin-status-loaded"
                                    : (enabled ? "plugin-status-loading"
                                               : "plugin-status-disabled"),
                });

                /* Reload button — only for loaded plugins */
                if (loaded && rctx) {
                    char reload_id[96];
                    snprintf(reload_id, sizeof(reload_id), "plugin-reload-%s", plugin_id);
                    ca_btn(&(Ca_BtnDesc){
                        .text       = ICON_RELOAD,
                        .id         = reload_id,
                        .style      = "plugin-reload-btn",
                        .on_click   = on_reload,
                        .click_data = rctx,
                    });
                }

                /* Enable toggle */
                if (tctx) {
                    char toggle_id[96];
                    snprintf(toggle_id, sizeof(toggle_id), "plugin-toggle-%s", plugin_id);
                    ca_toggle(&(Ca_ToggleDesc){
                        .on          = enabled,
                        .id          = toggle_id,
                        .on_change   = on_enable_toggle,
                        .change_data = tctx,
                    });
                }
            }
            ca_div_end(); /* plugin-row-header */

            /* Description */
            if (desc && desc->description) {
                char desc_id[96];
                snprintf(desc_id, sizeof(desc_id), "plugin-desc-%s", plugin_id);
                ca_text(&(Ca_TextDesc){
                    .text  = desc->description,
                    .id    = desc_id,
                    .style = "plugin-description",
                });
            }

            /* Author */
            if (desc && desc->author) {
                char author_buf[128];
                snprintf(author_buf, sizeof(author_buf),
                         "Author: %s", desc->author);
                char author_id[96];
                snprintf(author_id, sizeof(author_id), "plugin-author-%s", plugin_id);
                ca_text(&(Ca_TextDesc){
                    .text  = author_buf,
                    .id    = author_id,
                    .style = "plugin-author",
                });
            }
        }
        ca_div_end(); /* plugin-row */

        ca_hr(&(Ca_HrDesc){ .color = 0 });
    }

    ca_div_end(); /* content div */
}

/* ================================================================
   OPEN — creates the plugin manager window on demand
   ================================================================ */

void ed_plugin_manager_open(void)
{
    /* If already open, do nothing (window is alive) */
    if (s_win && ca_window_is_open(s_win)) return;

    if (!s_editor) return;
    Ca_Window *main_win = qs_engine_window(editor_engine(s_editor));
    if (!main_win) return;
    Ca_Instance *inst = ca_window_instance(main_win);
    if (!inst) return;

    s_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = ICON_PLUGIN "  Plugin Manager",
        .width  = 540,
        .height = 480,
    });
    if (!s_win) return;

    /* Build the static shell — title label + scrollable content area */
    ca_ui_begin(s_win, &(Ca_DivDesc){
        .direction  = CA_VERTICAL,
        .style      = "plugin-manager-root",
    });
    {
        /* Title bar */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "plugin-manager-titlebar",
        });
        {
            ca_text(&(Ca_TextDesc){
                .text  = ICON_PLUGIN "  Plugin Manager",
                .style = "plugin-manager-title",
            });
            ca_div_begin(&(Ca_DivDesc){ .style = "plugin-spacer" });
            ca_div_end();
            ca_btn(&(Ca_BtnDesc){
                .text       = "Close",
                .style      = "plugin-manager-close-btn",
                .on_click   = on_close_win,
                .click_data = NULL,
            });
        }
        ca_div_end();

        ca_hr(&(Ca_HrDesc){ .color = 0 });

        /* Dynamic content area — rebuilt each frame via on_frame */
        s_content_div = ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "plugin-manager-content",
        });
        ca_div_end();
    }
    ca_ui_end();

    /* Register per-frame rebuild callback */
    ca_window_set_on_frame(s_win, plugin_manager_win_frame, NULL);
}
