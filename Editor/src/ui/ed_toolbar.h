#ifndef ED_TOOLBAR_H
#define ED_TOOLBAR_H

#include "quasar.h"

/// Initialises toolbar state.  Call once after the editor is fully created.
void ed_toolbar_init(void *editor);

/// Emits the toolbar UI for this frame.
void ed_toolbar(Ca_Window *window, void *editor);

#endif
