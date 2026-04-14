#ifndef ED_INSPECTOR_H
#define ED_INSPECTOR_H

#include "quasar.h"

/// Builds the inspector panel UI elements (called once during editor init).
void ed_inspector(void *editor);

/// Updates the inspector panel each frame based on the selected entity.
void ed_inspector_update(void *editor);

#endif
