#ifndef ED_INSPECTOR_H
#define ED_INSPECTOR_H

#include "quasar.h"

/// Builds the inspector panel UI elements (called once during editor init).
void ed_inspector(void *editor);

/// Updates the inspector panel each frame based on the selected entity.
void ed_inspector_update(void *editor);

/// Frees all static allocations held by the inspector (call before editor shutdown).
void ed_inspector_shutdown(void);

#endif
