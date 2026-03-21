#ifndef ED_LAYOUT_H
#define ED_LAYOUT_H

#include "causality.h"

void ed_layout(Ca_Window *window, void *editor);

/// Refreshes the console panel with latest log entries.
void ed_console_update(void *editor);

#endif
