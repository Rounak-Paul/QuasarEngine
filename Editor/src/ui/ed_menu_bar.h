#ifndef ED_MENU_BAR_H
#define ED_MENU_BAR_H

#include "quasar.h"

/// Synchronises the editor's menus into the Causality title bar.
/// Rebuilds only when the plugin fingerprint changes so that open
/// dropdowns are not interrupted mid-interaction.
/// Call once per frame from on_frame.
void ed_menu_bar_sync(Ca_Window *window, void *editor);

#endif

