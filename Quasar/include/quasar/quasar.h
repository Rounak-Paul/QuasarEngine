#ifndef QUASAR_H
#define QUASAR_H

#include <stdint.h>

typedef struct Qs_Engine Qs_Engine;

typedef struct Qs_EngineDesc {
    const char* app_name;
    uint32_t    version_major;
    uint32_t    version_minor;
    uint32_t    version_patch;
} Qs_EngineDesc;

/// Creates a new Quasar Engine instance.
Qs_Engine* qs_engine_create(const Qs_EngineDesc* desc);

/// Destroys a Quasar Engine instance and frees all resources.
void qs_engine_destroy(Qs_Engine* engine);

/// Returns the engine version string.
const char* qs_version_string(void);

#endif
