#include "ed_hierarchy.h"
#include "editor.h"
#include "ed_layout.h"
#include "ca_theme.h"
#include "qs_primitive.h"

#include <string.h>
#include <stdio.h>

/* ================================================================
   HIERARCHY PANEL — scene entity tree
   ================================================================ */

#define MAX_ENTITY_NODES 4096

typedef struct {
    Editor   *editor;
    Qs_Entity entity;
    Qs_Entity proto_owner;
    Qs_Scene *inner_scene;
} HierarchyClickCtx;

static HierarchyClickCtx s_click_ctx[MAX_ENTITY_NODES];
static uint32_t           s_click_idx;
static Editor            *s_editor;
static Ca_Div            *s_root;

/* ---- Toolbar state ------------------------------------------------- */
static bool            s_collapse_all      = false;
static bool            s_expand_scene_root = false;
static bool            s_rendering_enabled = true;

/* ---- Inline rename state ------------------------------------------ */
/* QS_ENTITY_INVALID means "not renaming" */
static Qs_Entity       s_renaming       = QS_ENTITY_INVALID;
static char            s_rename_buf[256];        /* current text while editing  */
static char            s_rename_prev[256];       /* name before rename started  */
static bool            s_rename_focus_next;      /* request auto-focus on next frame */
static Ca_TextInput   *s_rename_input    = NULL; /* cached input widget pointer */

/* ================================================================
   Entity selection callback
   ================================================================ */

static void on_entity_select(Ca_TreeNode *tn, void *user_data)
{
    (void)tn;
    HierarchyClickCtx *ctx = (HierarchyClickCtx *)user_data;
    if (!ctx || !ctx->editor) return;
    if (ctx->proto_owner != QS_ENTITY_INVALID && ctx->inner_scene) {
        editor_set_proto_selection(ctx->editor, ctx->proto_owner,
                                   ctx->inner_scene, ctx->entity);
    } else {
        editor_set_selected_entity(ctx->editor, ctx->entity);
    }
}

/* ================================================================
   Entity helpers
   ================================================================ */

static Qs_Entity create_entity_with_parent(const char *name, Qs_Entity parent)
{
    Qs_Scene *scene = qs_scene_active();
    if (!scene) return QS_ENTITY_INVALID;
    Qs_Entity e = qs_entity_create(scene, name);
    if (e == QS_ENTITY_INVALID) return e;
    if (parent != QS_ENTITY_INVALID)
        qs_entity_set_parent(scene, e, parent);
    return e;
}

static Qs_Entity duplicate_entity(Qs_Scene *scene, Qs_Entity src)
{
    if (!qs_entity_valid(scene, src)) return QS_ENTITY_INVALID;

    const char *src_name = qs_entity_name(scene, src);
    char dup_name[256];
    snprintf(dup_name, sizeof(dup_name), "%s Copy", src_name ? src_name : "Entity");

    Qs_Entity parent = qs_entity_get_parent(scene, src);
    Qs_Entity dup = create_entity_with_parent(dup_name, parent);
    if (dup == QS_ENTITY_INVALID) return QS_ENTITY_INVALID;

    Qs_Transform *src_t = qs_entity_get(scene, src, qs_transform_type());
    Qs_Transform *dup_t = qs_entity_get(scene, dup, qs_transform_type());
    if (src_t && dup_t) *dup_t = *src_t;

    if (qs_entity_has(scene, src, qs_mesh_comp_type())) {
        Qs_MeshComp *src_m = qs_entity_get(scene, src, qs_mesh_comp_type());
        Qs_MeshComp *dup_m = qs_entity_add(scene, dup, qs_mesh_comp_type());
        if (src_m && dup_m) {
            dup_m->visible = src_m->visible;
            memcpy(dup_m->mesh_path,     src_m->mesh_path,     sizeof(dup_m->mesh_path));
            memcpy(dup_m->material_path, src_m->material_path, sizeof(dup_m->material_path));
        }
    }
    if (qs_entity_has(scene, src, qs_light_comp_type())) {
        Qs_LightComp *src_l = qs_entity_get(scene, src, qs_light_comp_type());
        Qs_LightComp *dup_l = qs_entity_add(scene, dup, qs_light_comp_type());
        if (src_l && dup_l) *dup_l = *src_l;
    }
    if (qs_entity_has(scene, src, qs_prototype_comp_type())) {
        Qs_PrototypeComp *src_p = qs_entity_get(scene, src, qs_prototype_comp_type());
        Qs_PrototypeComp *dup_p = qs_entity_add(scene, dup, qs_prototype_comp_type());
        if (src_p && dup_p) memcpy(dup_p->path, src_p->path, sizeof(dup_p->path));
    }
    return dup;
}

/* ================================================================
   Context menu definitions
   ================================================================ */

/* --- Scene root / empty-space menu --- */
enum {
    ROOT_CTX_EMPTY     = 0,
    ROOT_CTX_LIGHT     = 1,
    ROOT_CTX_CUBE      = 2,
    ROOT_CTX_SPHERE    = 3,
    ROOT_CTX_PLANE     = 4,
    ROOT_CTX_CYLINDER  = 5,
    /* 6 = "-" */
    ROOT_CTX_FRAME_ALL = 7,
};
static const char *s_root_ctx_items[] = {
    "Add Empty Entity",
    "Add Light",
    "Add Cube",
    "Add Sphere",
    "Add Plane",
    "Add Cylinder",
    "-",
    "Frame All",
};

/* --- Per-entity menu --- */
enum {
    ENT_CTX_CREATE_CHILD  = 0,
    ENT_CTX_CREATE_PARENT = 1,
    ENT_CTX_DUPLICATE     = 2,
    /* 3 = "-" */
    ENT_CTX_RENAME        = 4,
    /* 5 = "-" */
    ENT_CTX_DELETE        = 6,
};
static const char *s_entity_ctx_items[] = {
    "Create Child",    /* 0 */
    "Create Parent",   /* 1 */
    "Duplicate",       /* 2 */
    "-",               /* 3 */
    "Rename",          /* 4 */
    "-",               /* 5 */
    "Delete",          /* 6 */
};

/* ================================================================
   Inline rename helpers
   ================================================================ */

static void rename_begin(Qs_Entity entity, Qs_Scene *scene)
{
    const char *cur = qs_entity_name(scene, entity);
    snprintf(s_rename_prev, sizeof(s_rename_prev), "%s", cur ? cur : "");
    snprintf(s_rename_buf,  sizeof(s_rename_buf),  "%s", cur ? cur : "");
    s_renaming          = entity;
    s_rename_focus_next = true;
    s_rename_input      = NULL;
}

static void rename_commit(Qs_Scene *scene)
{
    if (s_renaming == QS_ENTITY_INVALID) return;
    if (qs_entity_valid(scene, s_renaming)) {
        const char *old_name = qs_entity_name(scene, s_renaming);
        if (!old_name || strcmp(old_name, s_rename_buf) != 0)
            editor_mark_dirty(s_editor);
        qs_entity_set_name(scene, s_renaming, s_rename_buf);
    }
    s_renaming     = QS_ENTITY_INVALID;
    s_rename_input = NULL;
}

static void rename_cancel(Qs_Scene *scene)
{
    if (s_renaming == QS_ENTITY_INVALID) return;
    /* Restore original name */
    if (qs_entity_valid(scene, s_renaming))
        qs_entity_set_name(scene, s_renaming, s_rename_prev);
    s_renaming     = QS_ENTITY_INVALID;
    s_rename_input = NULL;
}

static void on_rename_change(Ca_TextInput *input, void *user_data)
{
    (void)user_data;
    const char *text = ca_get_text(input);
    if (text) snprintf(s_rename_buf, sizeof(s_rename_buf), "%s", text);
    /* Live-update the entity name so inspector and everywhere else stays in
       sync as the user types — same as the inspector's on_entity_name_input. */
    Qs_Scene *scene = qs_scene_active();
    if (scene && s_renaming != QS_ENTITY_INVALID && qs_entity_valid(scene, s_renaming)) {
        const char *old_name = qs_entity_name(scene, s_renaming);
        if (!old_name || strcmp(old_name, s_rename_buf) != 0)
            editor_mark_dirty(s_editor);
        qs_entity_set_name(scene, s_renaming, s_rename_buf);
    }
}

/* ================================================================
   Context menu callbacks
   ================================================================ */

static void on_root_ctx(int item_index, void *user_data)
{
    Editor   *ed    = (Editor *)user_data;
    Qs_Scene *scene = qs_scene_active();
    if (!ed || !scene) return;

    Qs_Entity e = QS_ENTITY_INVALID;
    switch (item_index) {
    case ROOT_CTX_EMPTY:
        e = create_entity_with_parent("Entity", QS_ENTITY_INVALID);
        break;
    case ROOT_CTX_LIGHT:
        e = create_entity_with_parent("Light", QS_ENTITY_INVALID);
        if (e != QS_ENTITY_INVALID) qs_entity_add(scene, e, qs_light_comp_type());
        break;
    case ROOT_CTX_CUBE:
    case ROOT_CTX_SPHERE:
    case ROOT_CTX_PLANE:
    case ROOT_CTX_CYLINDER: {
        static const char *names[]  = { "Cube", "Sphere", "Plane", "Cylinder" };
        Qs_PrimitiveType   prim     = (Qs_PrimitiveType)(item_index - ROOT_CTX_CUBE);
        e = create_entity_with_parent(names[prim], QS_ENTITY_INVALID);
        if (e != QS_ENTITY_INVALID) {
            Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_add(scene, e, qs_mesh_comp_type());
            if (mc) {
                snprintf(mc->mesh_path,     sizeof(mc->mesh_path),     "%s", qs_primitive_path(prim));
                snprintf(mc->material_path, sizeof(mc->material_path), "@default");
            }
        }
        break;
    }
    case ROOT_CTX_FRAME_ALL:
        editor_focus_all(ed);
        return;
    default: return;
    }
    if (e != QS_ENTITY_INVALID)
        editor_set_selected_entity(ed, e);
    if (e != QS_ENTITY_INVALID)
        editor_mark_dirty(ed);
}

static void on_entity_ctx(int item_index, void *user_data)
{
    HierarchyClickCtx *ctx   = (HierarchyClickCtx *)user_data;
    Qs_Scene          *scene = qs_scene_active();
    if (!ctx || !ctx->editor || !scene) return;
    if (!qs_entity_valid(scene, ctx->entity)) return;

    switch (item_index) {

    case ENT_CTX_CREATE_CHILD: {
        Qs_Entity e = create_entity_with_parent("Entity", ctx->entity);
        if (e != QS_ENTITY_INVALID) {
            editor_set_selected_entity(ctx->editor, e);
            editor_mark_dirty(ctx->editor);
        }
        break;
    }

    case ENT_CTX_CREATE_PARENT: {
        /* Insert a new empty entity at the same level, then re-parent the
           target entity under it.  The new parent inherits the target's
           current parent (or root). */
        const char *src_name = qs_entity_name(scene, ctx->entity);
        char new_parent_name[256];
        snprintf(new_parent_name, sizeof(new_parent_name), "%s Parent",
                 src_name ? src_name : "Entity");
        Qs_Entity old_parent = qs_entity_get_parent(scene, ctx->entity);
        Qs_Entity wrapper    = create_entity_with_parent(new_parent_name, old_parent);
        if (wrapper != QS_ENTITY_INVALID) {
            qs_entity_set_parent(scene, ctx->entity, wrapper);
            editor_set_selected_entity(ctx->editor, wrapper);
            editor_mark_dirty(ctx->editor);
        }
        break;
    }

    case ENT_CTX_DUPLICATE: {
        Qs_Entity dup = duplicate_entity(scene, ctx->entity);
        if (dup != QS_ENTITY_INVALID) {
            editor_set_selected_entity(ctx->editor, dup);
            editor_mark_dirty(ctx->editor);
        }
        break;
    }

    case ENT_CTX_RENAME:
        /* Switch this entity's row to inline-edit mode next frame */
        rename_begin(ctx->entity, scene);
        editor_set_selected_entity(ctx->editor, ctx->entity);
        break;

    case ENT_CTX_DELETE:
        if (s_renaming == ctx->entity) {
            s_renaming     = QS_ENTITY_INVALID;
            s_rename_input = NULL;
        }
        if (editor_selected_entity(ctx->editor) == ctx->entity)
            editor_set_selected_entity(ctx->editor, QS_ENTITY_INVALID);
        qs_entity_destroy(scene, ctx->entity);
        editor_mark_dirty(ctx->editor);
        break;

    default: break;
    }
}

/* ================================================================
   Toolbar callbacks
   ================================================================ */

static void on_collapse_all_click(Ca_Button *btn, void *data)
{
    (void)btn; (void)data;
    s_collapse_all = true;
}

static void on_new_entity_click(Ca_Button *btn, void *data)
{
    (void)btn;
    Editor *ed = (Editor *)data;
    Qs_Scene *scene = qs_scene_active();
    if (!scene) return;
    Qs_Entity e = qs_entity_create(scene, "Entity");
    if (e != QS_ENTITY_INVALID) {
        editor_set_selected_entity(ed, e);
        s_expand_scene_root = true;
        editor_mark_dirty(ed);
    }
}

static void set_scene_rendering(Qs_Scene *scene, bool visible)
{
    if (!scene) return;

    /* Toggle all mesh components in this scene */
    for (Qs_Entity e = qs_scene_first(scene, qs_mesh_comp_type());
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, qs_mesh_comp_type(), e))
    {
        Qs_MeshComp *mc = qs_entity_get(scene, e, qs_mesh_comp_type());
        if (mc) mc->visible = visible;
    }

    /* Recurse into prototype inner scenes */
    for (Qs_Entity e = qs_scene_first(scene, qs_prototype_comp_type());
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, qs_prototype_comp_type(), e))
    {
        Qs_PrototypeComp *pc = qs_entity_get(scene, e, qs_prototype_comp_type());
        if (pc && pc->inner) set_scene_rendering(pc->inner, visible);
    }
}

static void on_toggle_rendering_click(Ca_Button *btn, void *data)
{
    (void)btn; (void)data;
    Qs_Scene *scene = qs_scene_active();
    if (!scene) return;
    s_rendering_enabled = !s_rendering_enabled;
    set_scene_rendering(scene, s_rendering_enabled);
}

/* ================================================================
   Tree rendering
   ================================================================ */

static void render_entity_node(Editor *ed, Qs_Scene *scene, Qs_Entity entity, Qs_Entity selected);

static void render_entity_children(Editor *ed, Qs_Scene *scene, Qs_Entity parent, Qs_Entity selected)
{
    for (Qs_Entity e = qs_scene_first(scene, qs_transform_type());
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, qs_transform_type(), e))
    {
        if (qs_entity_get_parent(scene, e) == parent)
            render_entity_node(ed, scene, e, selected);
    }
}

static void render_entity_node(Editor *ed, Qs_Scene *scene, Qs_Entity entity, Qs_Entity selected)
{
    const char *name = qs_entity_name(scene, entity);
    if (!name) name = "(unnamed)";

    bool has_mesh  = qs_entity_has(scene, entity, qs_mesh_comp_type());
    bool has_light = qs_entity_has(scene, entity, qs_light_comp_type());
    bool has_proto = qs_entity_has(scene, entity, qs_prototype_comp_type());

    uint32_t    dot_color;
    const char *dot_icon;
    if (has_light)       { dot_color = CA_THEME_WARNING;    dot_icon = ICON_LIGHT;     }
    else if (has_proto)  { dot_color = CA_THEME_ACCENT;     dot_icon = ICON_PROTOTYPE; }
    else if (has_mesh)   { dot_color = CA_THEME_SUCCESS;    dot_icon = ICON_MESH;      }
    else                 { dot_color = CA_THEME_TEXT_MUTED; dot_icon = ICON_ENTITY;    }

    HierarchyClickCtx *ctx = NULL;
    if (s_click_idx < MAX_ENTITY_NODES) {
        ctx = &s_click_ctx[s_click_idx++];
        ctx->editor      = ed;
        ctx->entity      = entity;
        ctx->proto_owner = QS_ENTITY_INVALID;
        ctx->inner_scene = NULL;
    }

    const char *style = (entity == selected)
        ? "hierarchy-entity hierarchy-selected"
        : "hierarchy-entity";

    char node_id[64];
    snprintf(node_id, sizeof(node_id), "hier-entity-%u", (unsigned)entity);

    bool has_children = false;
    for (Qs_Entity e = qs_scene_first(scene, qs_transform_type());
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, qs_transform_type(), e))
    {
        if (qs_entity_get_parent(scene, e) == entity) { has_children = true; break; }
    }

    bool is_renaming = (entity == s_renaming);

    Ca_TreeNode *tn_entity = ca_tree_node_begin(&(Ca_TreeNodeDesc){
        .text        = is_renaming ? "" : name,  /* hide label while editing */
        .id          = node_id,
        .style       = style,
        .icon        = is_renaming ? NULL : dot_icon,
        .icon_color  = dot_color,
        .is_leaf     = !has_children,
        .on_toggle   = ctx ? on_entity_select : NULL,
        .toggle_data = ctx,
    });
    if (s_collapse_all && tn_entity) ca_tree_node_set_expanded(tn_entity, false);
    /* Tooltip shows full entity name (useful when truncated) */
    if (!is_renaming)
        ca_tooltip(&(Ca_TooltipDesc){ .text = name });

    /* Inline rename: show a text input inside the header row */
    if (is_renaming) {
        char input_id[80];
        snprintf(input_id, sizeof(input_id), "hier-rename-%u", (unsigned)entity);
        s_rename_input = ca_input(&(Ca_InputDesc){
            .text      = s_rename_buf,
            .id        = input_id,
            .style     = "hierarchy-rename-input",
            .on_change = on_rename_change,
        });

        /* Auto-focus the field on the first frame it appears */
        if (s_rename_focus_next && s_rename_input) {
            ca_input_focus(s_rename_input);
            s_rename_focus_next = false;
        }
    }

    /* Entity-specific right-click menu. ca_context_menu now always attaches
       to children[0] (the header row) when inside a tree node — so it
       correctly targets the header row regardless of how many child entity
       nodes exist from prior frames. */
    ca_context_menu(&(Ca_CtxMenuDesc){
        .items       = s_entity_ctx_items,
        .item_count  = (int)(sizeof(s_entity_ctx_items) / sizeof(*s_entity_ctx_items)),
        .on_select   = on_entity_ctx,
        .select_data = ctx,
    });

    if (has_children)
        render_entity_children(ed, scene, entity, selected);

    ca_tree_node_end();
}

static void build_hierarchy(Editor *ed, Qs_Scene *scene)
{
    s_click_idx = 0;

    /* ---- Toolbar ---- */
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL,
                                .id        = "hierarchy-toolbar",
                                .style     = "hierarchy-toolbar" });
    {
        ed_icon_btn(&(EdIconBtnDesc){
            .icon       = ICON_COMPRESS,
            .id         = "hier-btn-collapse",
            .tooltip    = "Collapse All",
            .on_click   = on_collapse_all_click,
        });
        ed_icon_btn(&(EdIconBtnDesc){
            .icon       = ICON_PLUS,
            .id         = "hier-btn-new-entity",
            .tooltip    = "New Entity",
            .on_click   = on_new_entity_click,
            .click_data = ed,
        });
        /* Push eye button to the right */
        ca_div_begin(&(Ca_DivDesc){ .style = "hierarchy-toolbar-spacer" });
        ca_div_end();
        Ca_Button *render_btn = ed_icon_btn(&(EdIconBtnDesc){
            .icon       = s_rendering_enabled ? ICON_EYE : ICON_EYE_SLASH,
            .id         = "hier-btn-toggle-render",
            .tooltip    = s_rendering_enabled ? "Disable Scene Rendering" : "Enable Scene Rendering",
            .on_click   = on_toggle_rendering_click,
        });
        ed_icon_btn_set_active(render_btn, !s_rendering_enabled);
    }
    ca_div_end();

    ca_tree_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .id        = "hierarchy-tree",
        .style     = "hierarchy-tree",
    });
    {
        Ca_TreeNode *tn_scene = ca_tree_node_begin(&(Ca_TreeNodeDesc){
            .text       = qs_scene_name(scene),
            .expanded   = true,
            .id         = "hierarchy-scene-root",
            .style      = "hierarchy-scene",
            .icon       = ICON_SCENE,
            .icon_color = CA_THEME_ACCENT,
        });
        if (s_collapse_all && tn_scene) ca_tree_node_set_expanded(tn_scene, false);
        if (s_expand_scene_root && tn_scene) ca_tree_node_set_expanded(tn_scene, true);
        ca_tooltip(&(Ca_TooltipDesc){ .text = qs_scene_name(scene) });
        ca_context_menu(&(Ca_CtxMenuDesc){
            .items       = s_root_ctx_items,
            .item_count  = (int)(sizeof(s_root_ctx_items) / sizeof(*s_root_ctx_items)),
            .on_select   = on_root_ctx,
            .select_data = ed,
        });
        {
            Qs_Entity selected = editor_selected_entity(ed);
            for (Qs_Entity e = qs_scene_first(scene, qs_transform_type());
                 e != QS_ENTITY_INVALID;
                 e = qs_scene_next(scene, qs_transform_type(), e))
            {
                if (qs_entity_get_parent(scene, e) == QS_ENTITY_INVALID)
                    render_entity_node(ed, scene, e, selected);
            }
        }
        ca_tree_node_end();
    }
    ca_tree_end();

    s_collapse_all = false;
    s_expand_scene_root = false;

    /* Attach root menu to tree container after ca_tree_end so the div
       already exists as a child (child_count > 0 guard passes).
       Empty-area right-clicks fall through to this menu. */
    ca_context_menu(&(Ca_CtxMenuDesc){
        .items       = s_root_ctx_items,
        .item_count  = (int)(sizeof(s_root_ctx_items) / sizeof(*s_root_ctx_items)),
        .on_select   = on_root_ctx,
        .select_data = ed,
    });
}

/* ================================================================
   Keyboard handling for inline rename (Enter = commit, Esc = cancel)
   Called each frame from ed_hierarchy_update.
   ================================================================ */

static void rename_handle_keys(Qs_Scene *scene)
{
    if (s_renaming == QS_ENTITY_INVALID || !s_rename_input) return;

    /* GLFW_KEY_ENTER=257, GLFW_KEY_KP_ENTER=335, GLFW_KEY_ESCAPE=256 */
    if (ca_input_key_pressed(s_rename_input, 257) ||
        ca_input_key_pressed(s_rename_input, 335)) {
        rename_commit(scene);
        return;
    }
    if (ca_input_key_pressed(s_rename_input, 256)) {
        rename_cancel(scene);
        return;
    }

    /* Commit when the input loses focus (user clicked elsewhere) */
    if (!ca_input_is_focused(s_rename_input))
        rename_commit(scene);
}

/* ================================================================
   Public API
   ================================================================ */

void ed_hierarchy(void *editor)
{
    s_editor = (Editor *)editor;
    s_root   = ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .id        = "hierarchy-root",
        .style     = "hierarchy-root",
    });
    ca_div_end();
}

void ed_hierarchy_update(void *editor)
{
    Editor *ed = (Editor *)editor;
    if (!ed || !s_root) return;
    Qs_Scene *scene = qs_scene_active();
    if (!scene) return;

    /* Handle rename keyboard events before rebuilding (uses last frame's input) */
    rename_handle_keys(scene);

    ca_reconcile_begin(s_root);
    build_hierarchy(ed, scene);
    ca_div_end();
}


