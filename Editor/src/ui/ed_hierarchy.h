#ifndef ED_HIERARCHY_H
#define ED_HIERARCHY_H

#include "quasar.h"

/// Builds the scene hierarchy tree inside the current container.
void ed_hierarchy(void *editor);

/// Updates the hierarchy panel each frame (refreshes entity/component list).
void ed_hierarchy_update(void *editor);

#endif
