#include "ed_hierarchy.h"
#include "editor.h"
#include "ca_theme.h"

#include <string.h>
#include <stdio.h>

/* ================================================================
   HIERARCHY PANEL — scene entity tree
   ================================================================ */

/* Nerd Font icons (Font Awesome range F000–F2E0) */
#define ICON_SCENE      "\xEF\x82\xAC"   /* U+F0AC globe     */
#define ICON_ENTITY     "\xEF\x84\x91"   /* U+F111 circle    */
#define ICON_MESH       "\xEF\x86\xB2"   /* U+F1B2 cube      */
#define ICON_LIGHT      "\xEF\x83\xAB"   /* U+F0EB lightbulb */

/* We need a way to pass both the editor pointer and entity ID to the callback.
   Use a small static ring buffer of callback contexts. */
#define MAX_ENTITY_NODES 256

typedef struct {
    Editor   *editor;
    Qs_Entity entity;
} HierarchyClickCtx;

static HierarchyClickCtx s_click_ctx[MAX_ENTITY_NODES];
static uint32_t           s_click_idx;

static void on_entity_select(Ca_TreeNode *tn, void *user_data)
{
    (void)tn;
    HierarchyClickCtx *ctx = (HierarchyClickCtx *)user_data;
    if (ctx && ctx->editor)
        editor_set_selected_entity(ctx->editor, ctx->entity);
}

void ed_hierarchy(void *editor)
{
    Editor   *ed    = (Editor *)editor;
    Qs_Scene *scene = qs_scene_active();
    if (!scene) return;

    s_click_idx = 0;

    /* ---- Tree ---- */
    ca_tree_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "hierarchy-tree",
    });
    {
        /* Scene root node — always expanded */
        ca_tree_node_begin(&(Ca_TreeNodeDesc){
            .text       = qs_scene_name(scene),
            .expanded   = true,
            .style      = "hierarchy-scene",
            .icon       = ICON_SCENE,
            .icon_color = CA_THEME_ACCENT,
        });
        {
            Qs_Entity selected = editor_selected_entity(ed);

            /* Iterate all entities */
            for (Qs_Entity e = qs_scene_first(scene, qs_transform_type());
                 e != QS_ENTITY_INVALID;
                 e = qs_scene_next(scene, qs_transform_type(), e))
            {
                const char *name = qs_entity_name(scene, e);
                if (!name) name = "(unnamed)";

                /* Pick icon based on entity's primary component */
                bool has_mesh  = qs_entity_has(scene, e, qs_mesh_comp_type());
                bool has_light = qs_entity_has(scene, e, qs_light_comp_type());

                uint32_t dot_color;
                const char *dot_icon;
                if (has_light) {
                    dot_color = CA_THEME_WARNING;
                    dot_icon  = ICON_LIGHT;
                } else if (has_mesh) {
                    dot_color = CA_THEME_SUCCESS;
                    dot_icon  = ICON_MESH;
                } else {
                    dot_color = CA_THEME_TEXT_MUTED;
                    dot_icon  = ICON_ENTITY;
                }

                /* Set up click context */
                HierarchyClickCtx *ctx = NULL;
                if (s_click_idx < MAX_ENTITY_NODES) {
                    ctx = &s_click_ctx[s_click_idx++];
                    ctx->editor = ed;
                    ctx->entity = e;
                }

                /* Highlight selected entity */
                const char *style = (e == selected)
                    ? "hierarchy-entity hierarchy-selected"
                    : "hierarchy-entity";

                ca_tree_node_begin(&(Ca_TreeNodeDesc){
                    .text        = name,
                    .style       = style,
                    .icon        = dot_icon,
                    .icon_color  = dot_color,
                    .is_leaf     = true,
                    .on_toggle   = ctx ? on_entity_select : NULL,
                    .toggle_data = ctx,
                });
                ca_tree_node_end();
            }
        }
        ca_tree_node_end();
    }
    ca_tree_end();

    /* ---- Add Entity button ---- */
    ca_btn(&(Ca_BtnDesc){
        .text      = "+  Add Entity",
        .style     = "hierarchy-add-btn",
    });
}

void ed_hierarchy_update(void *editor)
{
    (void)editor;
}
