#include "ed_hierarchy.h"
#include "editor.h"

#include <string.h>
#include <stdio.h>

/* ================================================================
   HIERARCHY PANEL
   ================================================================ */

void ed_hierarchy(void *editor)
{
    (void)editor;

    Qs_Scene *scene = qs_scene_active();
    if (!scene) return;

    ca_tree_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "hierarchy-tree",
    });
    {
        /* Scene root node — always expanded */
        char scene_label[96];
        snprintf(scene_label, sizeof(scene_label), "%s", qs_scene_name(scene));

        ca_tree_node_begin(&(Ca_TreeNodeDesc){
            .text     = scene_label,
            .expanded = true,
            .style    = "hierarchy-scene",
        });
        {
            /* Iterate all entities (every entity has Transform) */
            for (Qs_Entity e = qs_scene_first(scene, qs_transform_type());
                 e != QS_ENTITY_INVALID;
                 e = qs_scene_next(scene, qs_transform_type(), e))
            {
                const char *name = qs_entity_name(scene, e);
                if (!name) name = "(unnamed)";

                ca_tree_node_begin(&(Ca_TreeNodeDesc){
                    .text     = name,
                    .expanded = false,
                    .style    = "hierarchy-entity",
                });
                {
                    /* Show Transform component */
                    Qs_Transform *tf = (Qs_Transform *)qs_entity_get(
                                           scene, e, qs_transform_type());
                    if (tf) {
                        char buf[128];
                        snprintf(buf, sizeof(buf),
                                 "Transform  (%.1f, %.1f, %.1f)",
                                 tf->position[0], tf->position[1],
                                 tf->position[2]);
                        ca_tree_node_begin(&(Ca_TreeNodeDesc){
                            .text  = buf,
                            .style = "hierarchy-component",
                        });
                        ca_tree_node_end();
                    }

                    /* Show MeshComp if present */
                    if (qs_entity_has(scene, e, qs_mesh_comp_type())) {
                        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_get(
                                              scene, e, qs_mesh_comp_type());
                        char buf[128];
                        snprintf(buf, sizeof(buf), "MeshComp  [%s]",
                                 mc->mesh_name[0] ? mc->mesh_name : "none");
                        ca_tree_node_begin(&(Ca_TreeNodeDesc){
                            .text  = buf,
                            .style = "hierarchy-component",
                        });
                        ca_tree_node_end();
                    }

                    /* Show LightComp if present */
                    if (qs_entity_has(scene, e, qs_light_comp_type())) {
                        ca_tree_node_begin(&(Ca_TreeNodeDesc){
                            .text  = "LightComp",
                            .style = "hierarchy-component",
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
}

void ed_hierarchy_update(void *editor)
{
    (void)editor;
}
