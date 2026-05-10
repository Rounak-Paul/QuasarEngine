#ifndef ED_RENDERER_SETTINGS_H
#define ED_RENDERER_SETTINGS_H

/// Opens the renderer settings window (bloom, vignette, MSAA).
/// Has no effect if the window is already open.
void ed_renderer_settings_open(void *editor);

/// Module shutdown — close and destroy the window if open.
void ed_renderer_settings_shutdown(void);

#endif /* ED_RENDERER_SETTINGS_H */
