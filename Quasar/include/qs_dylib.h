#ifndef QS_DYLIB_H
#define QS_DYLIB_H

#include <stdbool.h>
#include <stddef.h>
#include "qs_api.h"

/// Opaque handle to a loaded dynamic library.
typedef struct Qs_Dylib Qs_Dylib;

/// Opens a dynamic library at the given path. Returns NULL on failure.
/// The handle must be released with qs_dylib_close.
QS_API Qs_Dylib   *qs_dylib_open(const char *path);

/// Closes a previously opened library handle and releases its resources.
QS_API void        qs_dylib_close(Qs_Dylib *lib);

/// Resolves a named symbol from an open library.
/// Returns NULL if the symbol is not found.
QS_API void       *qs_dylib_sym(Qs_Dylib *lib, const char *name);

/// Returns a human-readable string describing the last error, or NULL.
/// Valid until the next qs_dylib_* call on any library.
QS_API const char *qs_dylib_error(void);

/// Writes the directory containing the current executable into out_dir.
/// Returns true on success, false on failure.
/// out_dir receives a null-terminated path with no trailing separator.
QS_API bool        qs_dylib_exe_dir(char *out_dir, size_t capacity);

#endif
