#ifndef ED_IMPORT_DIALOG_H
#define ED_IMPORT_DIALOG_H

#include "editor.h"

/// Initialise the import dialog module.  Stores the editor pointer so
/// the dialog can reach the active project + engine when the user clicks
/// Import.  Call once during editor setup.
void ed_import_dialog_init(Editor *editor);

/// Open the import dialog as a top-level window.
/// `source_path` is the absolute path of the file the user picked
/// (e.g. an .gltf / .glb).  The matching importer is invoked
/// immediately (may block for a moment) and the result is presented
/// for the user to accept or cancel.
void ed_import_dialog_open(const char *source_path);

#endif
