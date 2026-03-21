#include "quasar/quasar.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define QS_VERSION_MAJOR 0
#define QS_VERSION_MINOR 1
#define QS_VERSION_PATCH 0

struct Qs_Engine {
    char* app_name;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
};

Qs_Engine* qs_engine_create(const Qs_EngineDesc* desc) {
    Qs_Engine* engine = calloc(1, sizeof(Qs_Engine));
    if (!engine) return NULL;

    if (desc && desc->app_name) {
        size_t len = strlen(desc->app_name);
        engine->app_name = malloc(len + 1);
        if (engine->app_name) {
            memcpy(engine->app_name, desc->app_name, len + 1);
        }
    }

    if (desc) {
        engine->version_major = desc->version_major;
        engine->version_minor = desc->version_minor;
        engine->version_patch = desc->version_patch;
    }

    printf("Quasar Engine %s initialized (%s)\n",
           qs_version_string(),
           engine->app_name ? engine->app_name : "unnamed");

    return engine;
}

void qs_engine_destroy(Qs_Engine* engine) {
    if (!engine) return;
    free(engine->app_name);
    free(engine);
}

const char* qs_version_string(void) {
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             QS_VERSION_MAJOR, QS_VERSION_MINOR, QS_VERSION_PATCH);
    return version;
}
