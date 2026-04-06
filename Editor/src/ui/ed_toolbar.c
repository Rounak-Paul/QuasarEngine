#include "ed_toolbar.h"
#include "ed_icon_btn.h"
#include "editor.h"

#include "quasar.h"

#include <string.h>
#include <stdio.h>

/* ================================================================
   Extension toolbar state — persists across frames.
   Stores per-extension-per-item active state and click context so
   trampoline callbacks have access to stable data.
   ================================================================ */
#define TOOLBAR_MAX_EXTENSIONS 32
#define TOOLBAR_MAX_ITEMS_PER_EXT QS_TOOLBAR_MAX_ITEMS

typedef struct ExtItemCtx {
    Qs_Engine   *engine;
    void       (*on_click)(Qs_Engine *engine, bool *active);
    bool        *active;
} ExtItemCtx;

typedef struct ExtToolbarState {
    bool        active[TOOLBAR_MAX_ITEMS_PER_EXT];
    ExtItemCtx  ctx[TOOLBAR_MAX_ITEMS_PER_EXT];
    bool        initialised;
} ExtToolbarState;

static ExtToolbarState s_ext_states[TOOLBAR_MAX_EXTENSIONS];

static Ca_Div   *s_toolbar_div;
static Qs_Engine *s_toolbar_engine;

void ed_toolbar_init(void *editor)
{
    (void)editor;
    memset(s_ext_states, 0, sizeof(s_ext_states));
    s_toolbar_div    = NULL;
    s_toolbar_engine = NULL;
}

/* ---- Extension item trampoline ---- */
static void on_ext_item_click(Ca_Button *btn, void *user_data)
{
    ExtItemCtx *ctx = (ExtItemCtx *)user_data;
    if (!ctx || !ctx->active) return;
    *ctx->active = !(*ctx->active);
    ca_set_style(btn, *ctx->active ? "toolbar-icon-btn active"
                                   : "toolbar-icon-btn");
    if (ctx->on_click)
        ctx->on_click(ctx->engine, ctx->active);
}

/* Populates the toolbar div with extension icon buttons.
   Assumes the caller has already entered s_toolbar_div as the current parent. */
static void toolbar_populate(Qs_Engine *engine)
{
    uint32_t ext_count = qs_engine_ext_count(engine, QS_EXT_EDITOR_TOOLBAR);
    if (ext_count > TOOLBAR_MAX_EXTENSIONS)
        ext_count = TOOLBAR_MAX_EXTENSIONS;

    for (uint32_t ei = 0; ei < ext_count; ++ei) {
        const Qs_ToolbarExt *ext  = qs_engine_ext_interface(engine, QS_EXT_EDITOR_TOOLBAR, ei);
        void                *data = qs_engine_ext_data(engine, QS_EXT_EDITOR_TOOLBAR, ei);
        if (!ext || !ext->get_items) continue;

        Qs_ToolbarItem items[TOOLBAR_MAX_ITEMS_PER_EXT];
        int count = TOOLBAR_MAX_ITEMS_PER_EXT;
        ext->get_items(data, engine, items, &count);
        if (count <= 0) continue;

        ExtToolbarState *estate = &s_ext_states[ei];

        for (int ii = 0; ii < count; ++ii) {
            Qs_ToolbarItem *item = &items[ii];
            if (!item->icon || !item->id) continue;

            if (!estate->initialised)
                estate->active[ii] = item->active;

            ExtItemCtx *ctx = &estate->ctx[ii];
            ctx->engine   = engine;
            ctx->on_click = item->on_click;
            ctx->active   = &estate->active[ii];

            char css_id[64];
            snprintf(css_id, sizeof(css_id), "ext-tb-%u-%d", ei, ii);

            ed_icon_btn(&(EdIconBtnDesc){
                .icon       = item->icon,
                .id         = css_id,
                .tooltip    = item->tooltip,
                .active     = estate->active[ii],
                .on_click   = on_ext_item_click,
                .click_data = ctx,
            });
        }

        if (!estate->initialised) estate->initialised = true;
    }
}

/* Builder callback — invoked by Causality when the div is invalidated.
   The div is already cleared and entered as the current parent. */
static void toolbar_builder(Ca_Div *div, void *user_data)
{
    (void)div;
    Qs_Engine *engine = (Qs_Engine *)user_data;
    if (engine)
        toolbar_populate(engine);
}

void ed_toolbar(Ca_Window *window, void *editor)
{
    (void)window;

    s_toolbar_div = ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "toolbar",
    });

    Qs_Engine *engine = editor_engine(editor);
    s_toolbar_engine = engine;
    if (!engine) { ca_div_end(); return; }

    ca_div_set_builder(s_toolbar_div, toolbar_builder, engine);
    toolbar_populate(engine);

    ca_div_end();
}

void ed_toolbar_rebuild(void)
{
    if (!s_toolbar_div) return;

    memset(s_ext_states, 0, sizeof(s_ext_states));
    ca_div_invalidate(s_toolbar_div);
}
