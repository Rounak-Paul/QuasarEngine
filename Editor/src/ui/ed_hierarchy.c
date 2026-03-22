#include "ed_hierarchy.h"
#include "editor.h"

#include <string.h>
#include <stdio.h>

/* ================================================================
   HIERARCHY PANEL — polished scene tree
   ================================================================ */

/* Nerd Font icons (Font Awesome range F000–F2E0) */
#define ICON_SCENE      "\xEF\x82\xAC"   /* U+F0AC globe     */
#define ICON_ENTITY     "\xEF\x84\x91"   /* U+F111 circle    */
#define ICON_MESH       "\xEF\x86\xB2"   /* U+F1B2 cube      */
#define ICON_LIGHT      "\xEF\x83\xAB"   /* U+F0EB lightbulb */
#define ICON_COMPONENT  "\xEF\x80\x93"   /* U+F013 cog       */

void ed_hierarchy(void *editor)
{
    (void)editor;

    Qs_Scene *scene = qs_scene_active();
    if (!scene) return;

    /* ---- Tree ---- */
    ca_tree_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "hierarchy-tree",
    });
    {
        /* Scene root node — always expanded */
        ca_tree_node_begin(&(Ca_TreeNodeDesc){
            .text     = qs_scene_name(scene),
            .expanded = true,
            .style    = "hierarchy-scene",
            .icon     = ICON_SCENE,
            .icon_color = ca_color(0.45f, 0.55f, 0.85f, 1.0f),
        });
        {
            /* Iterate all entities */
            for (Qs_Entity e = qs_scene_first(scene, qs_transform_type());
                 e != QS_ENTITY_INVALID;
                 e = qs_scene_next(scene, qs_transform_type(), e))
            {
                const char *name = qs_entity_name(scene, e);
                if (!name) name = "(unnamed)";

                /* Pick icon color based on entity type */
                bool has_mesh  = qs_entity_has(scene, e, qs_mesh_comp_type());
                bool has_light = qs_entity_has(scene, e, qs_light_comp_type());

                uint32_t dot_color;
                const char *dot_icon;
                if (has_light) {
                    dot_color = ca_color(0.95f, 0.85f, 0.35f, 1.0f);
                    dot_icon  = ICON_LIGHT;
                } else if (has_mesh) {
                    dot_color = ca_color(0.45f, 0.75f, 0.55f, 1.0f);
                    dot_icon  = ICON_MESH;
                } else {
                    dot_color = ca_color(0.55f, 0.55f, 0.70f, 1.0f);
                    dot_icon  = ICON_ENTITY;
                }

                /* Count child components to determine if entity has expandable content */
                int comp_count = 0;
                if (qs_entity_has(scene, e, qs_transform_type())) comp_count++;
                if (has_mesh)  comp_count++;
                if (has_light) comp_count++;
                bool is_leaf = (comp_count <= 1); /* only Transform = nothing interesting to expand */

                ca_tree_node_begin(&(Ca_TreeNodeDesc){
                    .text       = name,
                    .expanded   = false,
                    .style      = "hierarchy-entity",
                    .icon       = dot_icon,
                    .icon_color = dot_color,
                    .is_leaf    = is_leaf,
                });
                {
                    /* Component children (only shown when expanded) */
                    Qs_Transform *tf = (Qs_Transform *)qs_entity_get(
                                           scene, e, qs_transform_type());
                    if (tf) {
                        char buf[128];
                        snprintf(buf, sizeof(buf),
                                 "Transform  (%.1f, %.1f, %.1f)",
                                 tf->position[0], tf->position[1],
                                 tf->position[2]);
                        ca_tree_node_begin(&(Ca_TreeNodeDesc){
                            .text     = buf,
                            .style    = "hierarchy-component",
                            .is_leaf  = true,
                            .icon     = ICON_COMPONENT,
                            .icon_color = ca_color(0.40f, 0.40f, 0.55f, 0.8f),
                        });
                        ca_tree_node_end();
                    }

                    if (has_mesh) {
                        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_get(
                                              scene, e, qs_mesh_comp_type());
                        char buf[128];
                        snprintf(buf, sizeof(buf), "Mesh  [%s]",
                                 mc->mesh_name[0] ? mc->mesh_name : "none");
                        ca_tree_node_begin(&(Ca_TreeNodeDesc){
                            .text     = buf,
                            .style    = "hierarchy-component",
                            .is_leaf  = true,
                            .icon     = ICON_MESH,
                            .icon_color = ca_color(0.40f, 0.60f, 0.45f, 0.8f),
                        });
                        ca_tree_node_end();
                    }

                    if (has_light) {
                        ca_tree_node_begin(&(Ca_TreeNodeDesc){
                            .text     = "Light",
                            .style    = "hierarchy-component",
                            .is_leaf  = true,
                            .icon     = ICON_LIGHT,
                            .icon_color = ca_color(0.80f, 0.70f, 0.30f, 0.8f),
                        });
                        ca_tree_node_end();
                    }
                }
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
