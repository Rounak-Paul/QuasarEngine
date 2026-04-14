#ifndef ED_PROJECT_LAUNCHER_H
#define ED_PROJECT_LAUNCHER_H

#include <stdbool.h>
#include <stddef.h>

/// Runs the project launcher window.  Blocks until the user selects a project.
/// On success, writes the project directory path to out_path and returns true.
/// Returns false if the user closed the launcher without selecting a project.
bool ed_project_launcher_run(char *out_path, size_t out_path_size);

#endif
