#ifndef QS_PROJECT_H
#define QS_PROJECT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct Qs_Project Qs_Project;

/// Descriptor for creating a new project.
typedef struct Qs_ProjectDesc {
    const char *name;     ///< Project display name.
    const char *path;     ///< Directory path to create the project in.
} Qs_ProjectDesc;

/// Creates a new project: writes .quasar file, creates subdirectories
/// (assets/, scenes/, scripts/).  Returns the project handle, or NULL on failure.
Qs_Project *qs_project_create(const Qs_ProjectDesc *desc);

/// Opens an existing project from a directory containing a .quasar file.
/// Returns the project handle, or NULL if the directory is not a valid project.
Qs_Project *qs_project_open(const char *project_dir);

/// Returns the project display name.
const char *qs_project_name(const Qs_Project *project);

/// Returns the project root directory path.
const char *qs_project_path(const Qs_Project *project);

/// Destroys the project handle (does NOT delete project files).
void qs_project_destroy(Qs_Project *project);

#endif
