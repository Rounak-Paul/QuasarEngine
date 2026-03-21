#include "quasar.h"

#define QS_VERSION_MAJOR 0
#define QS_VERSION_MINOR 1
#define QS_VERSION_PATCH 0

struct Qs_Engine {
    char* app_name;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    Qs_EventBus* event_bus;
    Qs_JobSystem* job_system;
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

    engine->event_bus = qs_event_bus_create();
    if (!engine->event_bus) {
        free(engine->app_name);
        free(engine);
        return NULL;
    }

    engine->job_system = qs_job_system_create(&(Qs_JobSystemDesc){ .num_threads = 0 });
    if (!engine->job_system) {
        qs_event_bus_destroy(engine->event_bus);
        free(engine->app_name);
        free(engine);
        return NULL;
    }

    printf("Quasar Engine %s initialized (%s) [%u worker threads]\n",
           qs_version_string(),
           engine->app_name ? engine->app_name : "unnamed",
           qs_job_system_thread_count(engine->job_system));

    qs_event_fire(engine->event_bus, QS_EVENT_ENGINE_INIT, NULL, 0);
    return engine;
}

void qs_engine_destroy(Qs_Engine* engine) {
    if (!engine) return;
    qs_event_fire(engine->event_bus, QS_EVENT_ENGINE_SHUTDOWN, NULL, 0);
    qs_job_system_destroy(engine->job_system);
    qs_event_bus_destroy(engine->event_bus);
    free(engine->app_name);
    free(engine);
}

Qs_EventBus* qs_engine_event_bus(Qs_Engine* engine) {
    return engine ? engine->event_bus : NULL;
}

Qs_JobSystem* qs_engine_job_system(Qs_Engine* engine) {
    return engine ? engine->job_system : NULL;
}

const char* qs_version_string(void) {
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             QS_VERSION_MAJOR, QS_VERSION_MINOR, QS_VERSION_PATCH);
    return version;
}
