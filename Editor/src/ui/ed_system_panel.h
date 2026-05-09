/*
 * ed_system_panel.h — System / Memory diagnostics panel for the Quasar Editor.
 *
 * Build once during editor layout init, then call ed_system_panel_update()
 * every frame to refresh the live memory stats displayed in the widget tree.
 */

#ifndef ED_SYSTEM_PANEL_H
#define ED_SYSTEM_PANEL_H

#include "quasar.h"

/// Builds the static widget tree for the System panel.
/// Must be called exactly once during editor UI construction.
void ed_system_panel(void);

/// Refreshes all live-updating labels/progress bars from the current memory stats.
/// Call once per frame while the System tab is visible.
/// Pass the engine pointer so GPU stats can be queried each frame.
void ed_system_panel_update(Qs_Engine *engine);

#endif /* ED_SYSTEM_PANEL_H */
