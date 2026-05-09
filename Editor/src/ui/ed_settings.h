#ifndef ED_SETTINGS_H
#define ED_SETTINGS_H

#include "quasar.h"

/// Stores editor context.  Call once at editor startup (not during UI build).
void ed_settings_init(void *editor);

/// Opens the Settings window.  Creates it if not already open.
void ed_settings_open(void);

#endif
