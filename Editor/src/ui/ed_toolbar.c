#include "ed_toolbar.h"
#include "ed_layout.h"
#include "ed_gizmo.h"
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
static void     *s_toolbar_editor;

/* ---- Wireframe / normals state ---- */
#define ICON_WIREFRAME  "\xEF\x82\x96"   /* U+F096 */
#define ICON_NORMALS    "\xEF\x85\x8E"   /* U+F14E */

static Ca_Button *s_wireframe_btn;
static Ca_Button *s_normals_btn;
static bool       s_wireframe_active;
static bool       s_normals_active;

static void on_wireframe_click(Ca_Button *btn, void *data)
{
    s_wireframe_active = !s_wireframe_active;
    ed_icon_btn_set_active(btn, s_wireframe_active);
    Qs_Renderer *r = editor_scene_renderer((Editor *)data);
    if (r) qs_renderer_set_wireframe(r, s_wireframe_active);
}

static void on_normals_click(Ca_Button *btn, void *data)
{
    s_normals_active = !s_normals_active;
    ed_icon_btn_set_active(btn, s_normals_active);
    Qs_Renderer *r = editor_scene_renderer((Editor *)data);
    if (r) {
        uint32_t flags = qs_renderer_debug_flags(r);
        if (s_normals_active) flags |= 0x1u;
        else flags &= ~0x1u;
        qs_renderer_set_debug_flags(r, flags);
    }
}

/* ---- Gizmo mode button state ---- */
#define ICON_GIZMO_TRANSLATE "\xEF\x81\x87"   /* U+F047  arrows (move)     */
#define ICON_GIZMO_ROTATE    "\xEF\x80\x9E"   /* U+F01E  redo (rotate)     */
#define ICON_GIZMO_SCALE     "\xEF\x81\xA5"   /* U+F065  expand (scale)    */

static Ca_Button *s_gizmo_btns[3];

static void update_gizmo_btn_styles(void)
{
    EdGizmoMode m = ed_gizmo_mode();
    for (int i = 0; i < 3; i++)
        ed_icon_btn_set_active(s_gizmo_btns[i], (int)m == i);
}

static void on_gizmo_translate(Ca_Button *btn, void *data)
{ (void)btn; (void)data; ed_gizmo_set_mode(ED_GIZMO_TRANSLATE); update_gizmo_btn_styles(); }
static void on_gizmo_rotate(Ca_Button *btn, void *data)
{ (void)btn; (void)data; ed_gizmo_set_mode(ED_GIZMO_ROTATE);    update_gizmo_btn_styles(); }
static void on_gizmo_scale(Ca_Button *btn, void *data)
{ (void)btn; (void)data; ed_gizmo_set_mode(ED_GIZMO_SCALE);     update_gizmo_btn_styles(); }

void ed_toolbar_init(void *editor)
{
    (void)editor;
    memset(s_ext_states, 0, sizeof(s_ext_states));
    s_toolbar_div    = NULL;
    s_toolbar_editor = NULL;
    s_wireframe_btn  = NULL;
    s_normals_btn    = NULL;
    s_wireframe_active = false;
    s_normals_active   = false;
}

/* ---- Extension item trampoline ---- */
static void on_ext_item_click(Ca_Button *btn, void *user_data)
{
    ExtItemCtx *ctx = (ExtItemCtx *)user_data;
    if (!ctx || !ctx->active) return;
    *ctx->active = !(*ctx->active);
    ed_icon_btn_set_active(btn, *ctx->active);
    if (ctx->on_click)
        ctx->on_click(ctx->engine, ctx->active);
}

/* Populates the toolbar div with gizmo mode buttons, a separator, built-in
   renderer toggles, another separator, then extension icon buttons.
   Assumes the caller has already entered s_toolbar_div as the current parent. */
static void toolbar_populate(void *editor)
{
    Qs_Engine *engine = editor_engine((Editor *)editor);
    /* ---- Gizmo mode buttons ---- */
    EdGizmoMode mode = ed_gizmo_mode();
    static const struct { const char *icon; const char *id; const char *tip; EdGizmoMode m; Ca_ClickFn fn; }
    gizmo_defs[3] = {
        { ICON_GIZMO_TRANSLATE, "gizmo-translate", "Translate (1)", ED_GIZMO_TRANSLATE, on_gizmo_translate },
        { ICON_GIZMO_ROTATE,    "gizmo-rotate",    "Rotate (2)",    ED_GIZMO_ROTATE,    on_gizmo_rotate    },
        { ICON_GIZMO_SCALE,     "gizmo-scale",     "Scale (3)",     ED_GIZMO_SCALE,     on_gizmo_scale     },
    };
    for (int i = 0; i < 3; i++) {
        s_gizmo_btns[i] = ed_icon_btn(&(EdIconBtnDesc){
            .icon       = gizmo_defs[i].icon,
            .id         = gizmo_defs[i].id,
            .tooltip    = gizmo_defs[i].tip,
            .on_click   = gizmo_defs[i].fn,
        });
        ed_icon_btn_set_active(s_gizmo_btns[i], mode == gizmo_defs[i].m);
    }

    /* Vertical separator */
    ca_div_begin(&(Ca_DivDesc){ .style = "toolbar-separator" });
    ca_div_end();

    /* ---- Built-in renderer toggles ---- */
    s_wireframe_btn = ed_icon_btn(&(EdIconBtnDesc){
        .icon     = ICON_WIREFRAME,
        .id       = "renderer-wireframe",
        .tooltip  = "Wireframe",
        .on_click = on_wireframe_click,
        .click_data = editor,
    });
    ed_icon_btn_set_active(s_wireframe_btn, s_wireframe_active);

    s_normals_btn = ed_icon_btn(&(EdIconBtnDesc){
        .icon     = ICON_NORMALS,
        .id       = "renderer-normals",
        .tooltip  = "Show Normals",
        .on_click = on_normals_click,
        .click_data = editor,
    });
    ed_icon_btn_set_active(s_normals_btn, s_normals_active);

    /* Vertical separator before extension buttons */
    ca_div_begin(&(Ca_DivDesc){ .style = "toolbar-separator" });
    ca_div_end();

    /* ---- Extension buttons ---- */
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

            Ca_Button *ebtn = ed_icon_btn(&(EdIconBtnDesc){
                .icon       = item->icon,
                .id         = css_id,
                .tooltip    = item->tooltip,
                .on_click   = on_ext_item_click,
                .click_data = ctx,
            });
            ed_icon_btn_set_active(ebtn, estate->active[ii]);
        }

        if (!estate->initialised) estate->initialised = true;
    }
}

/* Builder callback — invoked by Causality when the div is invalidated.
   The div is already cleared and entered as the current parent. */
static void toolbar_builder(Ca_Div *div, void *user_data)
{
    (void)div;
    if (user_data)
        toolbar_populate(user_data);
}

void ed_toolbar(Ca_Window *window, void *editor)
{
    (void)window;

    s_toolbar_div = ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "toolbar",
    });

    if (!editor) { ca_div_end(); return; }
    s_toolbar_editor = editor;

    ca_div_set_builder(s_toolbar_div, toolbar_builder, editor);

    ca_div_end();
}

void ed_toolbar_rebuild(void)
{
    if (!s_toolbar_div) return;

    memset(s_ext_states, 0, sizeof(s_ext_states));
    ca_div_invalidate(s_toolbar_div);
}

void ed_toolbar_sync_gizmo(void)
{
    update_gizmo_btn_styles();
}
