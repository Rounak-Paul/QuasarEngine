#ifndef ED_MENU_BAR_H
#define ED_MENU_BAR_H

#include "quasar.h"

/// Builds the static menu bar host div into the current tree.
/// Must be called once during editor_build_ui.  Returns the host div
/// pointer that must be passed to ed_menu_bar_update every frame.
Ca_Div *ed_menu_bar(Ca_Window *window, void *editor);

/// Rebuilds the menu bar content for the current frame.
/// Includes the File menu plus a dynamic Plugins menu assembled from
/// all loaded plugins' on_editor_menu callbacks.
/// Must be called every editor frame (pass the div from ed_menu_bar).
void ed_menu_bar_update(Ca_Div *host, Ca_Window *window, void *editor);

#endif
