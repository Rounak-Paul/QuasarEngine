#ifndef ED_FILE_BROWSER_H
#define ED_FILE_BROWSER_H

#include "quasar.h"

/// File browser dialog mode.
typedef enum EdFBMode {
    ED_FB_OPEN_FILE,
    ED_FB_OPEN_FOLDER,
} EdFBMode;

/// Extension filter for file display.
typedef struct EdFBFilter {
    const char *label;       /* e.g. "Scene Files (*.qscene)"            */
    const char *extensions;  /* comma-separated, e.g. ".qscene,.json"    */
} EdFBFilter;

/// Callback invoked when the user confirms a selection.
typedef void (*EdFBCallback)(const char *path, void *user_data);

/// Descriptor for opening the file browser.
typedef struct EdFBDesc {
    EdFBMode            mode;
    const char         *title;         /* dialog title (NULL = default)   */
    const char         *initial_path;  /* starting dir (NULL = home dir)  */
    const EdFBFilter   *filters;       /* extension filters (NULL = all)  */
    int                 filter_count;
    EdFBCallback        on_confirm;
    void               *user_data;
} EdFBDesc;

/// Initialises the file browser module. Call once during editor setup.
void ed_file_browser_init(Ca_Instance *instance);

/// Opens the file browser dialog in a new window.
void ed_file_browser_open(const EdFBDesc *desc);

/// Closes the file browser dialog.
void ed_file_browser_close(void);

/// Returns true if the file browser is currently open.
bool ed_file_browser_is_open(void);

/// Per-frame housekeeping (detects external window close).
void ed_file_browser_update(void);

#endif
