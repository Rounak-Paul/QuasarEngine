#include "ed_hierarchy.h"
#include "editor.h"
#include "ed_icons.h"
#include "ca_theme.h"

#include <string.h>
#include <stdio.h>

/* ================================================================
   HIERARCHY PANEL — scene entity tree
   ================================================================ */

/* We need a way to pass both the editor pointer and entity ID to the callback.
   Use a small static ring buffer of callback contexts. */
#define MAX_ENTITY_NODES 4096

typedef struct {
    Editor   *editor;
    Qs_Entity entity;
} HierarchyClickCtx;

static HierarchyClickCtx s_click_ctx[MAX_ENTITY_NODES];
static uint32_t           s_click_idx;
static Editor            *s_editor;
static Ca_Div            *s_root;

static void on_entity_select(Ca_TreeNode *tn, void *user_data)
{
    (void)tn;
    HierarchyClickCtx *ctx = (HierarchyClickCtx *)user_data;
    if (ctx && ctx->editor)
        editor_set_selected_entity(ctx->editor, ctx->entity);
}

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
    if (has_light) {
        dot_color = CA_THEME_WARNING;
        dot_icon  = ICON_LIGHT;
    } else if (has_proto) {
        dot_color = CA_THEME_ACCENT;
        dot_icon  = ICON_PROTOTYPE;
    } else if (has_mesh) {
        dot_color = CA_THEME_SUCCESS;
        dot_icon  = ICON_MESH;
    } else {
        dot_color = CA_THEME_TEXT_MUTED;
        dot_icon  = ICON_ENTITY;
    }

    HierarchyClickCtx *ctx = NULL;
    if (s_click_idx < MAX_ENTITY_NODES) {
        ctx = &s_click_ctx[s_click_idx++];
        ctx->editor = ed;
        ctx->entity = entity;
    }

    const char *style = (entity == selected)
        ? "hierarchy-entity hierarchy-selected"
        : "hierarchy-entity";

    char node_id[64];
    snprintf(node_id, sizeof(node_id), "hier-entity-%u", (unsigned)entity);

    /* Check if this entity has children */
    bool has_children = false;
    for (Qs_Entity e = qs_scene_first(scene, qs_transform_type());
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, qs_transform_type(), e))
    {
        if (qs_entity_get_parent(scene, e) == entity) { has_children = true; break; }
    }

    ca_tree_node_begin(&(Ca_TreeNodeDesc){
        .text        = name,
        .id          = node_id,
        .style       = style,
        .icon        = dot_icon,
        .icon_color  = dot_color,
        .is_leaf     = !has_children,
        .on_toggle   = ctx ? on_entity_select : NULL,
        .toggle_data = ctx,
    });

    if (has_children)
        render_entity_children(ed, scene, entity, selected);

    ca_tree_node_end();
}

static void build_hierarchy(Editor *ed, Qs_Scene *scene)
{
    s_click_idx = 0;

    /* ---- Tree ---- */
    ca_tree_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .id        = "hierarchy-tree",
        .style     = "hierarchy-tree",
    });
    {
        /* Scene root node — always expanded */
        ca_tree_node_begin(&(Ca_TreeNodeDesc){
            .text       = qs_scene_name(scene),
            .expanded   = true,
            .id         = "hierarchy-scene-root",
            .style      = "hierarchy-scene",
            .icon       = ICON_SCENE,
            .icon_color = CA_THEME_ACCENT,
        });
        {
            Qs_Entity selected = editor_selected_entity(ed);

            /* Render only root entities; children rendered recursively */
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

    /* ---- Add Entity button ---- */
    ca_btn(&(Ca_BtnDesc){
        .text      = "+  Add Entity",
        .id        = "hierarchy-add-entity",
        .style     = "hierarchy-add-btn",
    });
}

void ed_hierarchy(void *editor)
{
    s_editor = (Editor *)editor;
    s_root = ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .id        = "hierarchy-root",
    });
    ca_div_end();
}

void ed_hierarchy_update(void *editor)
{
    Editor *ed = (Editor *)editor;
    if (!ed || !s_root) return;
    Qs_Scene *scene = qs_scene_active();
    if (!scene) return;

    ca_reconcile_begin(s_root);
    build_hierarchy(ed, scene);
    ca_div_end();
}
