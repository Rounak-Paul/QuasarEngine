#ifndef ED_MENU_BAR_H
#define ED_MENU_BAR_H

#include "quasar.h"

/// Initial menu bar setup — call once after editor creation.
void ed_menu_bar_init(Ca_Window *window, void *editor);

/// Marks the menu bar for rebuild on the next frame.
/// Call when extensions change (plugin enable/disable/reload).
void ed_menu_bar_invalidate(void);

/// Rebuilds the menu bar if invalidated.  Call once per frame from on_frame.
void ed_menu_bar_sync(void);

#endif

