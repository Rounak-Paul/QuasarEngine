#ifndef ED_PICK_H
#define ED_PICK_H

#include "quasar.h"

/// Creates GPU resources for the entity pick pass (pipeline, images, staging buffer).
void ed_pick_init(Qs_Engine *engine);

/// Destroys pick pass GPU resources.
void ed_pick_shutdown(Qs_Engine *engine);

/// Attaches the pick render node to a renderer.
void ed_pick_attach(Qs_Renderer *renderer);

/// Reads back the entity ID at normalised viewport coordinates (0..1) from the
/// most recent pick pass.  Returns QS_ENTITY_INVALID if nothing was hit.
Qs_Entity ed_pick_entity_at(Qs_Engine *engine, float norm_x, float norm_y);

#endif /* ED_PICK_H */
