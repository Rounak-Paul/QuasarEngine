#pragma once

/*
 * ed_node_graph.h — Render graph node graph canvas widget.
 *
 * Displays the engine's render graph as an interactive node graph.
 * Fixed nodes (shadow, pbr, tonemap) are shown as non-movable.
 * Dynamic plugin nodes are draggable.  Clicking a node opens its
 * settings in the right-hand properties panel.
 *
 * Usage:
 *   ed_node_graph_init(editor);          // once at startup
 *   // inside a ca_div_begin / ca_div_end:
 *   ed_node_graph_build();               // emits the split canvas+props widget
 *   ed_node_graph_shutdown();            // on cleanup
 */

#include "causality.h"

void ed_node_graph_init    (void *editor);
void ed_node_graph_build   (void);    /* call inside a parent div */
void ed_node_graph_shutdown(void);
