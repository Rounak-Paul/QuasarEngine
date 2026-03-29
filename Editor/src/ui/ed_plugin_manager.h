#ifndef ED_PLUGIN_MANAGER_H
#define ED_PLUGIN_MANAGER_H

#include "quasar.h"

/// Stores editor context.  Call once at editor startup (not during UI build).
void ed_plugin_manager_init(void *editor);

/// Opens the Plugin Manager as a separate window.
/// Creates the window if it is not already open.
void ed_plugin_manager_open(void);

#endif
