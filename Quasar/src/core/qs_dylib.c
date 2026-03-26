#include "qs_dylib.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <dlfcn.h>
  #include <unistd.h>
  #ifdef __APPLE__
    #include <mach-o/dyld.h>
  #endif
#endif

/* ================================================================
   INTERNAL
   ================================================================ */

struct Qs_Dylib {
#ifdef _WIN32
    HMODULE handle;
#else
    void   *handle;
#endif
};

static char s_error_buf[512];

/* ================================================================
   API
   ================================================================ */

Qs_Dylib *qs_dylib_open(const char *path)
{
    if (!path) return NULL;

    Qs_Dylib *lib = calloc(1, sizeof(Qs_Dylib));
    if (!lib) return NULL;

#ifdef _WIN32
    lib->handle = LoadLibraryA(path);
    if (!lib->handle) {
        DWORD err = GetLastError();
        snprintf(s_error_buf, sizeof(s_error_buf),
                 "LoadLibrary failed (error %lu): %s", err, path);
        free(lib);
        return NULL;
    }
#else
    lib->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib->handle) {
        const char *err = dlerror();
        snprintf(s_error_buf, sizeof(s_error_buf), "%s", err ? err : "unknown error");
        free(lib);
        return NULL;
    }
#endif

    s_error_buf[0] = '\0';
    return lib;
}

void qs_dylib_close(Qs_Dylib *lib)
{
    if (!lib) return;
#ifdef _WIN32
    FreeLibrary(lib->handle);
#else
    dlclose(lib->handle);
#endif
    free(lib);
}

void *qs_dylib_sym(Qs_Dylib *lib, const char *name)
{
    if (!lib || !name) return NULL;
#ifdef _WIN32
    void *sym = (void *)GetProcAddress(lib->handle, name);
    if (!sym) {
        DWORD err = GetLastError();
        snprintf(s_error_buf, sizeof(s_error_buf),
                 "GetProcAddress failed (error %lu): %s", err, name);
    } else {
        s_error_buf[0] = '\0';
    }
    return sym;
#else
    dlerror(); /* clear */
    void *sym = dlsym(lib->handle, name);
    const char *err = dlerror();
    if (err) {
        snprintf(s_error_buf, sizeof(s_error_buf), "%s", err);
        return NULL;
    }
    s_error_buf[0] = '\0';
    return sym;
#endif
}

const char *qs_dylib_error(void)
{
    return s_error_buf[0] ? s_error_buf : NULL;
}

bool qs_dylib_exe_dir(char *out_dir, size_t capacity)
{
    if (!out_dir || capacity == 0) return false;

#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (len == 0 || len >= sizeof(path)) return false;
    char *sep = strrchr(path, '\\');
    if (!sep) sep = strrchr(path, '/');
    if (!sep) return false;
    *sep = '\0';
    snprintf(out_dir, capacity, "%s", path);
    return true;

#elif defined(__APPLE__)
    char path[4096];
    uint32_t size = (uint32_t)sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return false;
    char resolved[4096];
    if (!realpath(path, resolved)) return false;
    char *sep = strrchr(resolved, '/');
    if (!sep) return false;
    *sep = '\0';
    snprintf(out_dir, capacity, "%s", resolved);
    return true;

#else /* Linux */
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) return false;
    path[n] = '\0';
    char *sep = strrchr(path, '/');
    if (!sep) return false;
    *sep = '\0';
    snprintf(out_dir, capacity, "%s", path);
    return true;
#endif
}
