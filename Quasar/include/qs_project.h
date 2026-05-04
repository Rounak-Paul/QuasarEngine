#ifndef QS_PROJECT_H
#define QS_PROJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Qs_Project Qs_Project;

/// Descriptor for creating a new project.
typedef struct Qs_ProjectDesc {
    const char *name;     ///< Project display name.
    const char *path;     ///< Directory path to create the project in.
} Qs_ProjectDesc;

/// Creates a new project: writes .quasar file, creates subdirectories
/// (Assets/, scenes/, scripts/).  Returns the project handle, or NULL on failure.
Qs_Project *qs_project_create(const Qs_ProjectDesc *desc);

/// Opens an existing project from a directory containing a .quasar file.
/// Returns the project handle, or NULL if the directory is not a valid project.
Qs_Project *qs_project_open(const char *project_dir);

/// Persists the project file (asset_db, etc.) to disk.
bool qs_project_save(const Qs_Project *project);

/// Returns the project display name.
const char *qs_project_name(const Qs_Project *project);

/// Returns the project root directory path.
const char *qs_project_path(const Qs_Project *project);

/// Destroys the project handle (does NOT delete project files).
void qs_project_destroy(Qs_Project *project);

/* ================================================================
   ASSET DATABASE — registry of imported prototypes (.qproto files)
   ================================================================
   The .quasar file persists a list of imported prototype paths
   (project-relative).  The editor and runtime use this to populate
   pickers and to verify asset availability at startup.
   ================================================================ */

/// Number of registered prototypes in the asset DB.
uint32_t qs_project_prototype_count(const Qs_Project *project);

/// Project-relative path of the i-th prototype, or NULL if out of range.
const char *qs_project_prototype_path(const Qs_Project *project, uint32_t index);

/// Register a prototype (project-relative or absolute path) in the asset DB.
/// Duplicates are ignored.  Caller must qs_project_save() to persist.
/// Returns true if added (or already present).
bool qs_project_register_prototype(Qs_Project *project, const char *path);

/// Remove a prototype from the asset DB.  Caller must qs_project_save().
bool qs_project_unregister_prototype(Qs_Project *project, const char *path);

/// Lite asset DB check: logs warnings for any registered prototype whose
/// file no longer exists on disk.  Returns the number of missing entries.
uint32_t qs_project_check_assets(const Qs_Project *project);

/// Convert a path that may be absolute or already project-relative into a
/// project-relative form (no leading slash).  Output is unmodified if the
/// path is not under the project directory.
void qs_project_make_relative(const Qs_Project *project,
                              const char *path,
                              char *out, size_t out_size);

/// Resolve a project-relative path to an absolute path.
/// If `path` is already absolute, it is copied as-is.
void qs_project_resolve(const Qs_Project *project,
                        const char *path,
                        char *out, size_t out_size);

/* ================================================================
   ASSET SCAN — filesystem discovery of packed assets
   ================================================================
   Walk the project directory recursively and collect every
   .qstex / .qsmat / .qsmesh file.  Results are returned as
   project-relative paths.  Call before opening asset pickers.
   ================================================================ */

/// Scan the project directory for .qstex / .qsmat / .qsmesh files.
/// Populates the internal texture/material/mesh path lists.
/// Safe to call multiple times (clears previous results first).
void qs_project_scan_assets(Qs_Project *project);

/// Number of .qstex files found by the last qs_project_scan_assets() call.
uint32_t    qs_project_texture_count  (const Qs_Project *project);
/// Project-relative path to the i-th .qstex file, or NULL if out of range.
const char *qs_project_texture_path   (const Qs_Project *project, uint32_t index);

/// Number of .qsmat files found by the last qs_project_scan_assets() call.
uint32_t    qs_project_material_count (const Qs_Project *project);
/// Project-relative path to the i-th .qsmat file, or NULL if out of range.
const char *qs_project_material_path  (const Qs_Project *project, uint32_t index);

/// Number of .qsmesh files found by the last qs_project_scan_assets() call.
uint32_t    qs_project_mesh_count     (const Qs_Project *project);
/// Project-relative path to the i-th .qsmesh file, or NULL if out of range.
const char *qs_project_mesh_path      (const Qs_Project *project, uint32_t index);

#endif
