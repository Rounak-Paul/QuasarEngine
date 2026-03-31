#include "ed_toolbar.h"
#include "ed_icon_btn.h"
#include "editor.h"

#include "quasar.h"

#include <string.h>
#include <stdio.h>

/* U+EB9C — codicon grid icon used as wireframe indicator */
#define TOOLBAR_ICON_WIREFRAME  "\xEE\xAE\x9C"

/* ================================================================
   Plugin toolbar state — persists across frames.
   Stores per-plugin-per-item active state and click context so
   trampoline callbacks have access to stable data.
   ================================================================ */
#define TOOLBAR_MAX_PLUGINS 32

typedef struct PluginItemCtx {
    Qs_Engine   *engine;
    void       (*plugin_on_click)(Qs_Engine *engine, bool *active);
    bool        *active;
} PluginItemCtx;

typedef struct PluginToolbarState {
    bool          active[QS_PLUGIN_TOOLBAR_MAX_ITEMS];
    PluginItemCtx ctx[QS_PLUGIN_TOOLBAR_MAX_ITEMS];
    bool          initialised;
} PluginToolbarState;

static PluginToolbarState s_plugin_states[TOOLBAR_MAX_PLUGINS];

void ed_toolbar_init(void *editor)
{
    (void)editor;
    memset(s_plugin_states, 0, sizeof(s_plugin_states));
}

/* ---- Wireframe button ---- */
static void on_wireframe_click(Ca_Button *btn, void *user_data)
{
    (void)btn;
    Qs_Renderer *renderer = (Qs_Renderer *)user_data;
    if (!renderer) return;
    qs_renderer_set_wireframe(renderer, !qs_renderer_wireframe(renderer));
}

/* ---- Plugin item trampoline ---- */
static void on_plugin_item_click(Ca_Button *btn, void *user_data)
{
    (void)btn;
    PluginItemCtx *ctx = (PluginItemCtx *)user_data;
    if (!ctx || !ctx->active) return;
    /* Toggle active state first, then notify the plugin */
    *ctx->active = !(*ctx->active);
    if (ctx->plugin_on_click)
        ctx->plugin_on_click(ctx->engine, ctx->active);
}

void ed_toolbar(Ca_Window *window, void *editor)
{
    (void)window;

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "toolbar",
    });

    /* ---- Wireframe toggle (editor-owned) ---- */
    Qs_Renderer *renderer = editor_scene_renderer(editor);
    ed_icon_btn(&(EdIconBtnDesc){
        .icon       = TOOLBAR_ICON_WIREFRAME,
        .id         = "toolbar-wireframe",
        .tooltip    = "Wireframe",
        .active     = renderer && qs_renderer_wireframe(renderer),
        .on_click   = on_wireframe_click,
        .click_data = renderer,
    });

    /* ---- Plugin-contributed toolbar items (left-aligned) ---- */
    Qs_Engine        *engine = editor_engine(editor);
    Qs_PluginManager *pm     = engine ? qs_engine_plugin_manager(engine) : NULL;
    if (!pm) { ca_div_end(); return; }

    uint32_t plugin_count = qs_plugin_count(pm);
    if (plugin_count > TOOLBAR_MAX_PLUGINS)
        plugin_count = TOOLBAR_MAX_PLUGINS;

    for (uint32_t pi = 0; pi < plugin_count; ++pi) {
        const Qs_PluginState *ps = qs_plugin_state_at(pm, pi);
        if (!qs_plugin_state_loaded(ps)) continue;

        const Qs_PluginDesc *desc = qs_plugin_state_desc(ps);
        if (!desc || !desc->on_editor_toolbar) continue;

        Qs_ToolbarItem items[QS_PLUGIN_TOOLBAR_MAX_ITEMS];
        int count = QS_PLUGIN_TOOLBAR_MAX_ITEMS;
        desc->on_editor_toolbar(engine, items, &count);
        if (count <= 0) continue;

        PluginToolbarState *pstate = &s_plugin_states[pi];

        for (int ii = 0; ii < count; ++ii) {
            Qs_ToolbarItem *item = &items[ii];
            if (!item->icon || !item->id) continue;

            /* Seed active state on first encounter */
            if (!pstate->initialised)
                pstate->active[ii] = item->active;

            /* Refresh trampoline context each frame so hot-reloaded callbacks work */
            PluginItemCtx *ctx = &pstate->ctx[ii];
            ctx->engine          = engine;
            ctx->plugin_on_click = item->on_click;
            ctx->active          = &pstate->active[ii];

            char css_id[64];
            snprintf(css_id, sizeof(css_id), "plugin-tb-%u-%d", pi, ii);

            ed_icon_btn(&(EdIconBtnDesc){
                .icon       = item->icon,
                .id         = css_id,
                .tooltip    = item->tooltip,
                .active     = pstate->active[ii],
                .on_click   = on_plugin_item_click,
                .click_data = ctx,
            });
        }

        if (!pstate->initialised) pstate->initialised = true;
    }

    ca_div_end();
}

