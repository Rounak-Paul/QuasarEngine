#ifndef ED_GIZMO_H
#define ED_GIZMO_H

#include "quasar.h"

/// Gizmo manipulation mode.
typedef enum EdGizmoMode {
    ED_GIZMO_TRANSLATE = 0,
    ED_GIZMO_ROTATE    = 1,
    ED_GIZMO_SCALE     = 2,
} EdGizmoMode;

/// Creates GPU resources for gizmo rendering (pipeline, buffers).
void ed_gizmo_init(Qs_Engine *engine);

/// Destroys gizmo GPU resources.
void ed_gizmo_shutdown(Qs_Engine *engine);

/// Attaches the gizmo render node to a renderer.  Call after creating or
/// recreating the scene renderer.
void ed_gizmo_attach(Qs_Renderer *renderer);

/// Per-frame update: handles entity picking, gizmo interaction, and builds
/// gizmo geometry for the current frame.
void ed_gizmo_update(void *editor, float dt);

/// Returns the current gizmo mode.
EdGizmoMode ed_gizmo_mode(void);

/// Sets the current gizmo mode.
void ed_gizmo_set_mode(EdGizmoMode mode);

#endif /* ED_GIZMO_H */
