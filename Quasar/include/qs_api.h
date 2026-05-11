/*
 * qs_api.h  —  Public symbol visibility macro for the Quasar engine DLL.
 *
 * Usage:
 *   - Functions and data exported from Quasar.dll are declared QS_API.
 *   - Define QUASAR_BUILD (private, via CMake) when compiling the DLL itself
 *     so that declarations resolve to dllexport / visibility("default").
 *   - Consumers (Editor, Runtime, plugins) see dllimport / nothing.
 *
 * Platform handling:
 *   Windows  — dllexport when building, dllimport when consuming.
 *   GCC/Clang — __attribute__((visibility("default"))); requires that the
 *               library is compiled with -fvisibility=hidden if you want
 *               precise control.  On Linux the default is already "all
 *               symbols visible", so QS_API is a no-op in practice.
 */
#ifndef QS_API_H
#define QS_API_H

#ifdef _WIN32
  #ifdef QUASAR_BUILD
    #define QS_API __declspec(dllexport)
  #else
    #define QS_API __declspec(dllimport)
  #endif
#else
  #define QS_API __attribute__((visibility("default")))
#endif

#endif /* QS_API_H */
