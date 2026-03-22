#ifndef ED_MENU_BAR_H
#define ED_MENU_BAR_H

#include "quasar.h"

/// Builds the editor menu bar into the current UI tree.
/// Pass the editor pointer as context for menu actions.
void ed_menu_bar(Ca_Window *window, void *editor);

#endif
